#pragma once
#include "common.h"
#include "process_context.h"
#include "rpc_h.h"

class RpcServer;

class RpcServerProxy : Proxy<RpcServer> {
 public:
  explicit RpcServerProxy(RpcServer *rpc_server);

  static HRESULT CalculateMtStat(DWORD pid, size_t *size);
  static boolean GetMtStat(size_t offset, DWORD size, MtStat stat[]);
  static HRESULT GetMtName(uintptr_t addr, LPBSTR name);
};

class RpcServer {
 public:
  template <size_t N>
  RpcServer(wchar_t (&buffer)[N])
      : buffer_{buffer}, buffer_size_{N}, proxy_{this} {}
  HRESULT Run(PWSTR pipename);

 private:
  HRESULT CalculateMtStat(DWORD pid, size_t *size);
  boolean GetMtStat(size_t offset, DWORD size, MtStat stat[]);
  HRESULT GetMtName(uintptr_t addr, LPBSTR name);

  ProcessContext process_context_;
  std::vector<MtStat> mtstat_;
  wchar_t *buffer_;
  size_t buffer_size_;
  RpcServerProxy proxy_;
  friend class RpcServerProxy;
};

class RpcOutput : public IOutput {
 public:
  explicit RpcOutput(RPC_BINDING_HANDLE handle) : handle_{handle} {}
  void Print(PCWSTR str) override;

 private:
  RPC_BINDING_HANDLE handle_;
};