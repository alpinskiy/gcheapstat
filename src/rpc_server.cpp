#include "rpc_server.h"

#include "mtstat_calculator.h"
#include "rpc_helpers.h"

RpcServer *RpcServer::Instance;
auto constexpr kPipeNameFormat = L"\\pipe\\gcheapstat_pid%" PRIu32;

HRESULT RpcStubCalculateMtStat(handle_t handle, DWORD pid, PSIZE_T size) {
  size_t ret = 0;
  auto hr = RpcServer::Instance->CalculateMtStat(pid, &ret);
  if (size) *size = ret;
  return hr;
}

boolean RpcStubGetMtStat(handle_t handle, SIZE_T offset, UINT size,
                         MtStat mtstat[]) {
  return RpcServer::Instance->GetMtStat(offset, size, mtstat);
}

HRESULT RpcStubGetMtName(handle_t handle, UINT_PTR addr, LPBSTR name) {
  return RpcServer::Instance->GetMtName(addr, name);
}

void RpcStubCancel(handle_t handle) { Cancel(); }

HRESULT RpcServer::Run(PWSTR application_pipename) {
  if (!application_pipename) return E_INVALIDARG;
  struct ScopedInstance {
    explicit ScopedInstance(RpcServer *ptr) { RpcServer::Instance = ptr; }
    ~ScopedInstance() { RpcServer::Instance = nullptr; }
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
  if (!application_process) return HRESULT_FROM_WIN32(GetLastError());
  struct Output : IOutput {
    explicit Output(RPC_BINDING_HANDLE handle) : handle{handle} {}
    void Print(PCWSTR str) override {
      TryExceptRpc(&RpcProxyLogError, handle, _bstr_t{str}.GetBSTR());
    }
    RPC_BINDING_HANDLE handle;
  } output{application_binding.get()};
  auto logger = RegisterLoggerOutput(&output);
  auto waitres = WaitForSingleObject(application_process.get(), INFINITE);
  return (waitres == WAIT_OBJECT_0) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

HRESULT RpcServer::CalculateMtStat(DWORD pid, size_t *size) {
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

boolean RpcServer::GetMtStat(size_t offset, DWORD size, MtStat mtstat[]) {
  if (mtstat_.size() < offset || (mtstat_.size() - offset) < size) return FALSE;
  auto first = mtstat_.cbegin();
  std::advance(first, offset);
  auto last = first;
  std::advance(last, size);
  std::copy(first, last, mtstat);
  return TRUE;
}

HRESULT RpcServer::GetMtName(uintptr_t addr, LPBSTR name) {
  if (!name) return E_INVALIDARG;
  UINT needed = 0;
  auto hr =
      process_context_.GetMtName(addr, ARRAYSIZE(buffer_), buffer_, &needed);
  if (SUCCEEDED(hr)) *name = _bstr_t{buffer_}.Detach();
  return hr;
}