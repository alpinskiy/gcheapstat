#pragma once
#include "options.h"
#include "process_context.h"
#include "rpc_h.h"

class AppCore {
 public:
  HRESULT CalculateMtStat(DWORD pid, std::vector<MtStat> &mtstat);
  HRESULT GetMtName(uintptr_t addr, uint32_t size, PWSTR name,
                    uint32_t *needed);

 private:
  // Effectively runs RpcServer::CalculateMtStat under LocalSystem account
  HRESULT ServerCalculateMtStat(DWORD pid, std::vector<MtStat> &mtstat);
  HRESULT RunServerAsLocalSystem();

  enum class ContextKind { None, Local, Remote };
  ContextKind context_kind_{ContextKind::None};
  ProcessContext process_context_;
  wil::unique_rpc_binding server_binding_;
  static std::atomic<DWORD> ServerPid;
  friend DWORD RpcStubExchangePid(handle_t handle, DWORD pid);
};

HRESULT Run(Options &options);