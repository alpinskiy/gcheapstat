#pragma once
#include "mtstat_calculator.h"
#include "options.h"
#include "rpc_h.h"

struct ProcessContext {
  HRESULT Initialize(DWORD pid);
  HRESULT GetMtName(uintptr_t addr, uint32_t size, PWSTR name,
                    uint32_t *needed);

  wil::unique_handle process_handle;
  wil::unique_hmodule dac_module_handle;
  wil::com_ptr<ISOSDacInterface> sos_dac_interface;
};

class AppCore {
 public:
  HRESULT CalculateMtStat(DWORD pid, std::vector<MtStat> &mtstat);
  HRESULT GetMtName(uintptr_t addr, uint32_t size, PWSTR name,
                    uint32_t *needed);

 private:
  // Effectively runs Server::CalculateMtStat under LocalSystem account
  HRESULT ServerCalculateMtStat(DWORD pid, std::vector<MtStat> &mtstat);
  HRESULT RunServerAsLocalSystem();

  enum class ContextKind { None, Local, Remote };
  ContextKind context_kind_{ContextKind::None};
  ProcessContext process_context_;
  wil::unique_rpc_binding server_binding_;
  static std::atomic<DWORD> ServerPid;
  friend DWORD RpcStubExchangePid(handle_t handle, DWORD pid);
};

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