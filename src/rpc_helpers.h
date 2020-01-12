#pragma once

HRESULT RpcInitializeClient(PWSTR pipename, RPC_BINDING_HANDLE *hbinding);
HRESULT RpcInitializeServer(PWSTR pipename, RPC_IF_HANDLE iface);

template <typename F, typename... T>
HRESULT TryExceptRpc(F &&f, T &&... params) {
  HRESULT ret;
  __try {
    HRESULT hr{f(std::forward<T>(params)...)};
    ret = hr;
  } __except (RpcExceptionFilter(RpcExceptionCode())) {
    ret = RpcExceptionCode();
  }
  return ret;
}

template <typename T, typename F, typename... P>
HRESULT TryExceptRpc(T &t, F &&f, P &&... params) {
  HRESULT hr;
  __try {
    t = f(std::forward<P>(params)...);
    hr = S_OK;
  } __except (RpcExceptionFilter(RpcExceptionCode())) {
    hr = RpcExceptionCode();
  }
  return hr;
}