#pragma once

HRESULT RpcInitializeClient(PWSTR pipename, RPC_BINDING_HANDLE *hbinding);
HRESULT RpcInitializeServer(PWSTR pipename, RPC_IF_HANDLE iface);

template <typename F, typename... P>
inline std::enable_if_t<
    std::is_same<std::result_of_t<F && (P && ...)>, HRESULT>::value, HRESULT>
TryExceptRpc(F &&f, P &&... params) {
  HRESULT hr;
  __try {
    hr = std::forward<F>(f)(std::forward<P>(params)...);
  } __except (RpcExceptionFilter(RpcExceptionCode())) {
    hr = RpcExceptionCode();
  }
  return hr;
}

template <typename F, typename... P>
inline std::enable_if_t<
    std::is_same<std::result_of_t<F && (P && ...)>, void>::value, HRESULT>
TryExceptRpc(F &&f, P &&... params) {
  HRESULT hr = S_OK;
  __try {
    std::forward<F>(f)(std::forward<P>(params)...);
  } __except (RpcExceptionFilter(RpcExceptionCode())) {
    hr = RpcExceptionCode();
  }
  return hr;
}

template <typename F, typename... P>
inline HRESULT TryExceptRpc(std::result_of_t<F(P...)> &t, F &&f,
                            P &&... params) {
  HRESULT hr = S_OK;
  __try {
    t = std::forward<F>(f)(std::forward<P>(params)...);
  } __except (RpcExceptionFilter(RpcExceptionCode())) {
    hr = RpcExceptionCode();
  }
  return hr;
}