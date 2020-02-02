#pragma once
#include "common.h"
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

class RpcServerProxy : Proxy<RpcServer> {
 public:
  explicit RpcServerProxy(RpcServer *rpc_server);

  static HRESULT CalculateMtStat(DWORD pid, size_t *size);
  static boolean GetMtStat(size_t offset, DWORD size, MtStat stat[]);
  static HRESULT GetMtName(uintptr_t addr, LPBSTR name);
};

class RpcOutput : public IOutput {
 public:
  explicit RpcOutput(RPC_BINDING_HANDLE handle) : handle_{handle} {}
  void Print(PCWSTR str) override;

 private:
  RPC_BINDING_HANDLE handle_;
};