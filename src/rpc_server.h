#pragma once
#include "process_context.h"
#include "rpc_h.h"

class RpcServer {
 public:
  HRESULT Run(PWSTR pipename);

 private:
  HRESULT CalculateMtStat(DWORD pid, size_t *size);
  boolean GetMtStat(size_t offset, DWORD size, MtStat stat[]);
  HRESULT GetMtName(uintptr_t addr, LPBSTR name);

  ProcessContext process_context_;
  std::vector<MtStat> mtstat_;
  wchar_t buffer_[1024];
  friend class RpcServerProxy;
};

class RpcServerProxy {
 public:
  explicit RpcServerProxy(RpcServer *rpc_server);
  ~RpcServerProxy();
  RpcServerProxy(const RpcServerProxy &) = delete;
  RpcServerProxy(RpcServerProxy &&) = delete;
  RpcServerProxy &operator=(const RpcServerProxy &) = delete;
  RpcServerProxy &operator=(RpcServerProxy &&) = delete;

  static HRESULT CalculateMtStat(DWORD pid, size_t *size);
  static boolean GetMtStat(size_t offset, DWORD size, MtStat stat[]);
  static HRESULT GetMtName(uintptr_t addr, LPBSTR name);

 private:
  static wil::srwlock Mutex;
  static RpcServer *Instance;
};