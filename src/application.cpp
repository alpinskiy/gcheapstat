#include "application.h"

#include "clr_data_access.h"
#include "logger.h"
#include "rpc_helpers.h"
#include "runas_localsystem.h"

HRESULT ProcessContext::Initialize(DWORD pid) {
  DWORD desired_access = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ;
  process_handle.reset(OpenProcess(desired_access, FALSE, pid));
  if (!process_handle) return HRESULT_FROM_WIN32(GetLastError());
  ProcessInfo info;
  auto hr = info.Initialize(process_handle.get());
  if (FAILED(hr)) return hr;
  if (!info.managed) return E_NOINTERFACE;
  dac_module_handle.reset(LoadDataAccessModule(&info));
  if (!dac_module_handle) return HRESULT_FROM_WIN32(GetLastError());
  wil::com_ptr<IXCLRDataProcess> xclr_data_process;
  hr = CreateXCLRDataProcess(process_handle.get(), &info,
                             dac_module_handle.get(), &xclr_data_process);
  if (FAILED(hr)) return hr;
  return xclr_data_process.try_query_to(&sos_dac_interface) ? S_OK
                                                            : E_NOINTERFACE;
}

HRESULT ProcessContext::GetMtName(uintptr_t addr, uint32_t size, PWSTR name,
                                  uint32_t *needed) {
  return sos_dac_interface
             ? sos_dac_interface->GetMethodTableName(addr, size, name, needed)
             : E_NOINTERFACE;
}

std::atomic<DWORD> Application::ServerPid;
auto constexpr kPipeNameFormat = L"\\pipe\\gcheapstat_pid%" PRIu32;

DWORD RpcStubExchangePid(handle_t handle, DWORD pid) {
  Application::ServerPid.store(pid);
  return GetCurrentProcessId();
}

void RpcStubLogError(handle_t handle, BSTR message) { LogError(message); }

HRESULT Application::CalculateMtStat(DWORD pid, std::vector<MtStat> &mtstat) {
  ProcessContext process_context;
  auto hr = process_context.Initialize(pid);
  if (SUCCEEDED(hr)) {
    hr = ::CalculateMtStat(process_context.process_handle.get(),
                           process_context.sos_dac_interface.get(), mtstat);
    if (SUCCEEDED(hr)) {
      std::swap(process_context, process_context_);
      context_kind_ = ContextKind::Local;
      return hr;
    }
  }
  if (hr != E_ACCESSDENIED) return hr;
  hr = ServerCalculateMtStat(pid, mtstat);
  if (SUCCEEDED(hr)) context_kind_ = ContextKind::Remote;
  return hr;
}

HRESULT Application::GetMtName(uintptr_t addr, uint32_t size, PWSTR name,
                               uint32_t *needed) {
  switch (context_kind_) {
    case ContextKind::Local:
      return process_context_.GetMtName(addr, size, name, needed);
    case ContextKind::Remote: {
      _bstr_t bstr;
      auto hr = TryExceptRpc(&RpcProxyGetMtName, server_binding_.get(), addr,
                             bstr.GetAddress());
      if (SUCCEEDED(hr)) {
        hr = StringCchCopyW(name, size, bstr.GetBSTR());
        if (hr == STRSAFE_E_INSUFFICIENT_BUFFER) hr = S_FALSE;
        if (needed) *needed = bstr.length() + 1;
      }
      return hr;
    }
    default:
      return E_FAIL;
  }
}

HRESULT Application::ServerCalculateMtStat(DWORD pid,
                                           std::vector<MtStat> &mtstat) {
  auto hr = RunServerAsLocalSystem();
  if (FAILED(hr)) return hr;
  SIZE_T size = 0;
  hr =
      TryExceptRpc(&RpcProxyCalculateMtStat, server_binding_.get(), pid, &size);
  if (FAILED(hr)) return hr;
  mtstat.resize(size);
  UINT size32;
  SIZE_T size32max = (std::numeric_limits<DWORD>::max)();
  for (SIZE_T offset = 0; offset < size; offset += size32) {
    size32 = static_cast<UINT>((std::min)(size - offset, size32max));
    boolean ok = FALSE;
    hr = TryExceptRpc(ok, &RpcProxyGetMtStat, server_binding_.get(), offset,
                      size32, &mtstat[0] + offset);
    if (!ok) return FAILED(hr) ? hr : E_UNEXPECTED;
  }
  return S_OK;
}

HRESULT Application::RunServerAsLocalSystem() {
  if (ServerPid) return S_FALSE;
  // Run RPC server
  HRESULT hr;
  wchar_t pipename[MAX_PATH];
  auto fail =
      FAILED(hr = StringCchPrintfW(pipename, ARRAYSIZE(pipename),
                                   kPipeNameFormat, GetCurrentProcessId())) ||
      FAILED(hr = RpcInitializeServer(pipename, RpcStubClient_v0_0_s_ifspec));
  if (fail) return hr;
  // Spawn a new process running under LocalSystem account
  wchar_t filepath[MAX_PATH];
  auto length = GetModuleFileNameW(nullptr, filepath, ARRAYSIZE(filepath) - 1);
  auto error = GetLastError();
  if (!length || error == ERROR_INSUFFICIENT_BUFFER) {
    hr = HRESULT_FROM_WIN32(error);
    return FAILED(hr) ? hr : E_FAIL;
  }
  filepath[length] = 0;
  wchar_t cmdline[MAX_PATH];
  fail = FAILED(hr = StringCchPrintfW(cmdline, ARRAYSIZE(cmdline),
                                      L"\"%s\" --pipe \"%s\"", filepath,
                                      pipename)) ||
         FAILED(hr = RunAsLocalSystem(cmdline));
  if (fail) return hr;
  // Wait for the spawned process connect
  DWORD server_pid;
  for (; !(server_pid = ServerPid.load()); Sleep(1))
    if (IsCancelled()) return S_FALSE;
  // Connect back to the spawned process
  hr = StringCchPrintfW(pipename, ARRAYSIZE(pipename), kPipeNameFormat,
                        server_pid);
  if (SUCCEEDED(hr)) hr = RpcInitializeClient(pipename, &server_binding_);
  return hr;
}

Server *Server::Instance;

HRESULT RpcStubCalculateMtStat(handle_t handle, DWORD pid, PSIZE_T size) {
  size_t ret = 0;
  auto hr = Server::Instance->CalculateMtStat(pid, &ret);
  if (size) *size = ret;
  return hr;
}

boolean RpcStubGetMtStat(handle_t handle, SIZE_T offset, UINT size,
                         MtStat mtstat[]) {
  return Server::Instance->GetMtStat(offset, size, mtstat);
}

HRESULT RpcStubGetMtName(handle_t handle, UINT_PTR addr, LPBSTR name) {
  return Server::Instance->GetMtName(addr, name);
}

void RpcStubCancel(handle_t handle) { Cancel(); }

HRESULT Server::Run(PWSTR application_pipename) {
  if (!application_pipename) return E_INVALIDARG;
  struct ScopedInstance {
    explicit ScopedInstance(Server *ptr) { Server::Instance = ptr; }
    ~ScopedInstance() { Server::Instance = nullptr; }
  } guard{this};
  HRESULT hr;
  wchar_t server_pipename[MAX_PATH];
  auto fail =
      FAILED(hr = StringCchPrintfW(server_pipename, MAX_PATH, kPipeNameFormat,
                                   GetCurrentProcessId())) ||
      FAILED(hr = RpcInitializeServer(server_pipename,
                                      RpcStubServer_v0_0_s_ifspec));
  if (fail) return hr;
  wil::unique_rpc_binding application_binding;
  hr = RpcInitializeClient(application_pipename, &application_binding);
  if (FAILED(hr)) return hr;
  DWORD pid = 0;
  hr = TryExceptRpc(pid, &RpcProxyExchangePid, application_binding.get(),
                    GetCurrentProcessId());
  if (FAILED(hr)) return hr;
  wil::unique_process_handle application_process{
      OpenProcess(SYNCHRONIZE, FALSE, pid)};
  auto waitres = WaitForSingleObject(application_process.get(), INFINITE);
  return (waitres == WAIT_OBJECT_0) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

HRESULT Server::CalculateMtStat(DWORD pid, size_t *size) {
  ProcessContext process_context;
  auto hr = process_context.Initialize(pid);
  if (FAILED(hr)) return hr;
  hr = ::CalculateMtStat(process_context.process_handle.get(),
                         process_context.sos_dac_interface.get(), mtstat_);
  if (FAILED(hr)) return hr;
  std::swap(process_context, process_context_);
  if (size) *size = mtstat_.size();
  return hr;
}

boolean Server::GetMtStat(size_t offset, DWORD size, MtStat mtstat[]) {
  if (mtstat_.size() < offset || (mtstat_.size() - offset) < size) return FALSE;
  auto first = mtstat_.cbegin();
  std::advance(first, offset);
  auto last = first;
  std::advance(last, size);
  std::copy(first, last, mtstat);
  return TRUE;
}

HRESULT Server::GetMtName(uintptr_t addr, LPBSTR name) {
  if (!name) return E_INVALIDARG;
  UINT needed = 0;
  auto hr =
      process_context_.GetMtName(addr, ARRAYSIZE(buffer_), buffer_, &needed);
  if (SUCCEEDED(hr)) *name = _bstr_t{buffer_}.Detach();
  return hr;
}

PVOID __RPC_API MIDL_user_allocate(size_t size) { return malloc(size); }
void __RPC_API MIDL_user_free(PVOID p) { free(p); }