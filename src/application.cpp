#include "application.h"

#include "clr_data_access.h"
#include "mtstat_calculator.h"
#include "rpc_helpers.h"
#include "runas_localsystem.h"

std::atomic<DWORD> AppCore::ServerPid;
auto constexpr kPipeNameFormat = L"\\pipe\\gcheapstat_pid%" PRIu32;

DWORD RpcStubExchangePid(handle_t handle, DWORD pid) {
  AppCore::ServerPid.store(pid);
  return GetCurrentProcessId();
}

void RpcStubLogError(handle_t handle, BSTR message) { LogError(message); }

HRESULT AppCore::CalculateMtStat(DWORD pid, std::vector<MtStat> &mtstat) {
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

HRESULT AppCore::GetMtName(uintptr_t addr, uint32_t size, PWSTR name,
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

HRESULT AppCore::ServerCalculateMtStat(DWORD pid, std::vector<MtStat> &mtstat) {
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

HRESULT AppCore::RunServerAsLocalSystem() {
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

PVOID __RPC_API MIDL_user_allocate(size_t size) { return malloc(size); }
void __RPC_API MIDL_user_free(PVOID p) { free(p); }