#pragma once
#include "process_context.h"
#include "rpc_h.h"

class RpcServer {
 public:
  HRESULT Run(PWSTR application_pipename);

 private:
  HRESULT CalculateMtStat(DWORD pid, size_t *size);
  boolean GetMtStat(size_t offset, DWORD size, MtStat stat[]);
  HRESULT GetMtName(uintptr_t addr, LPBSTR name);

  ProcessContext process_context_;
  std::vector<MtStat> mtstat_;
  wchar_t buffer_[1024];
  static RpcServer *Instance;
  friend HRESULT RpcStubCalculateMtStat(handle_t handle, DWORD pid,
                                        PSIZE_T size);
  friend boolean RpcStubGetMtStat(handle_t handle, SIZE_T offset, UINT size,
                                  MtStat mtstat[]);
  friend HRESULT RpcStubGetMtName(handle_t handle, UINT_PTR addr, LPBSTR name);
};