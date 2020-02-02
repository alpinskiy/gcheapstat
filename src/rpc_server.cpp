#include "rpc_server.h"

#include "mtstat_calculator.h"

HRESULT RpcStubCalculateMtStat(handle_t handle, DWORD pid, PSIZE_T size) {
  size_t ret = 0;
  auto hr = RpcServerProxy::CalculateMtStat(pid, &ret);
  *size = ret;
  return hr;
}

boolean RpcStubGetMtStat(handle_t handle, SIZE_T offset, UINT size,
                         MtStat mtstat[]) {
  return RpcServerProxy::GetMtStat(offset, size, mtstat);
}

HRESULT RpcStubGetMtName(handle_t handle, UINT_PTR addr, LPBSTR name) {
  return RpcServerProxy::GetMtName(addr, name);
}

void RpcStubCancel(handle_t handle) { Cancel(); }

RpcServerProxy::RpcServerProxy(RpcServer *rpc_server) : Proxy{rpc_server} {}

HRESULT RpcServerProxy::CalculateMtStat(DWORD pid, size_t *size) {
  auto lock = Mutex.lock_exclusive();
  return Instance ? Instance->CalculateMtStat(pid, size) : E_POINTER;
}

boolean RpcServerProxy::GetMtStat(size_t offset, DWORD size, MtStat stat[]) {
  auto lock = Mutex.lock_exclusive();
  return Instance ? Instance->GetMtStat(offset, size, stat) : FALSE;
}

HRESULT RpcServerProxy::GetMtName(uintptr_t addr, LPBSTR name) {
  auto lock = Mutex.lock_exclusive();
  return Instance ? Instance->GetMtName(addr, name) : E_POINTER;
}

HRESULT RpcServer::Run(PWSTR pipename) {
  if (!pipename) return E_INVALIDARG;
  HRESULT hr;
  wchar_t server_pipename[MAX_PATH];
  auto fail =
      FAILED(hr = StringCchPrintfW(server_pipename, MAX_PATH, kPipeNameFormat,
                                   GetCurrentProcessId())) ||
      FAILED(hr = RpcInitializeServer(server_pipename,
                                      RpcStubServer_v0_0_s_ifspec));
  if (fail) return hr;
  hr = RpcInitializeClient(pipename, &application_binding_);
  if (FAILED(hr)) return hr;
  RpcServerProxy proxy{this};
  DWORD pid = 0;
  hr = TryExceptRpc(pid, &RpcProxyExchangePid, application_binding_.get(),
                    GetCurrentProcessId());
  if (FAILED(hr)) return hr;
  wil::unique_process_handle application_process{
      OpenProcess(SYNCHRONIZE, FALSE, pid)};
  if (!application_process) return HRESULT_FROM_WIN32(GetLastError());
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
  auto hr = process_context_.GetMtName(addr, buffer_size_, buffer_, &needed);
  if (SUCCEEDED(hr)) *name = _bstr_t{buffer_}.Detach();
  return hr;
}