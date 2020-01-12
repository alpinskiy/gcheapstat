#include "rpc_helpers.h"

#include "rpc_h.h"

HRESULT RpcInitializeClient(PWSTR pipename, RPC_BINDING_HANDLE *hbinding) {
  // https://docs.microsoft.com/en-us/windows/win32/rpc/the-client-application
  WCHAR protseq[] = L"ncacn_np";
  wil::unique_rpc_wstr string_binding;
  auto status = RpcStringBindingComposeW(nullptr, protseq, nullptr, pipename,
                                         nullptr, &string_binding);
  if (status == RPC_S_OK)
    status = RpcBindingFromStringBindingW(string_binding.get(), hbinding);
  return HRESULT_FROM_WIN32(status);
}

HRESULT RpcInitializeServer(PWSTR pipename, RPC_IF_HANDLE iface) {
  WCHAR protseq[] = L"ncacn_np";
  auto status = RpcServerUseProtseqEpW(protseq, 1, pipename, nullptr);
  if (status == RPC_S_OK) {
    status = RpcServerRegisterIf(iface, nullptr, nullptr);
    if (status == RPC_S_OK) status = RpcServerListen(1, 1, 1);
  }
  return HRESULT_FROM_WIN32(status);
}
