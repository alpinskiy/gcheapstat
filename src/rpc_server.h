#pragma once
#include "common.h"
#include "process_context.h"
#include "rpc_h.h"
#include "rpc_helpers.h"

class RpcServer {
 public:
  template <size_t N>
  RpcServer(wchar_t (&buffer)[N])
      : buffer_{buffer}, buffer_size_{N}, application_binding_{nullptr} {}
  HRESULT Run(PWSTR pipename);

 private:
  HRESULT CalculateMtStat(DWORD pid, size_t *size);
  boolean GetMtStat(size_t offset, DWORD size, MtStat stat[]);
  HRESULT GetMtName(uintptr_t addr, LPBSTR name);

  template <typename... Args>
  void LogError(PCWSTR format, Args &&... args) {
    auto hr = StringCchPrintfW(buffer_, buffer_size_, format,
                               std::forward<Args>(args)...);
    _ASSERT(SUCCEEDED(hr));
    TryExceptRpc(&RpcProxyLogError, application_binding_.get(),
                 _bstr_t{buffer_}.GetBSTR());
  }

  ProcessContext process_context_;
  std::vector<MtStat> mtstat_;
  wchar_t *buffer_;
  size_t buffer_size_;
  wil::unique_rpc_binding application_binding_;
  friend class RpcServerProxy;
};

class RpcServerProxy : Proxy<RpcServer> {
 public:
  explicit RpcServerProxy(RpcServer *rpc_server);

  static HRESULT CalculateMtStat(DWORD pid, size_t *size);
  static boolean GetMtStat(size_t offset, DWORD size, MtStat stat[]);
  static HRESULT GetMtName(uintptr_t addr, LPBSTR name);

  template <typename... Args>
  static void LogError(PCWSTR format, Args &&... args) {
    auto lock = Mutex.lock_exclusive();
    if (Instance) Instance->LogError(format, std::forward<Args>(args)...);
  }
};