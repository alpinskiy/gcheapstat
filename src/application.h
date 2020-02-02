#pragma once
#include "common.h"
#include "options.h"
#include "process_context.h"
#include "rpc_h.h"

class Application {
 public:
  HRESULT Run(Options &options);

 private:
  using mtstat_iterator = std::vector<MtStat>::iterator;
  void PrintWinDbgFormat(mtstat_iterator first, mtstat_iterator last,
                         Stat MtStat::*ptr);
  HRESULT CalculateMtStat(DWORD pid, std::vector<MtStat> &mtstat);
  HRESULT GetMtName(uintptr_t addr, uint32_t size, PWSTR name,
                    uint32_t *needed);
  // Effectively runs RpcServer::CalculateMtStat under LocalSystem account
  HRESULT ServerCalculateMtStat(DWORD pid, std::vector<MtStat> &mtstat);
  HRESULT RunServerAsLocalSystem();
  DWORD ExchangePid(DWORD pid);
  void Cancel();

  enum class ContextKind { None, Local, Remote };
  ContextKind context_kind_{ContextKind::None};
  ProcessContext process_context_;
  wil::unique_rpc_binding server_binding_;
  std::atomic<DWORD> ServerPid;
  std::atomic_bool RpcInitialized;
  friend class ApplicationProxy;
};

class ApplicationProxy : Proxy<Application> {
 public:
  explicit ApplicationProxy(Application *application);

  static DWORD ExchangePid(DWORD pid);
  static void Cancel();
};

Stat MtStat::*GetStatPtr(int gen);

template <class T, template <class> class C>
struct MtStatComparer {
  explicit MtStatComparer(OrderBy orderby, int gen)
      : ptr{GetStatPtr(gen)},
        ptr2{orderby == OrderBy::Count ? &Stat::count : &Stat::size_total} {
    _ASSERT(orderby == OrderBy::Count || orderby == OrderBy::TotalSize);
  }
  bool operator()(MtStat &a, MtStat &b) {
    return cmp(a.*ptr.*ptr2, b.*ptr.*ptr2);
  }
  Stat MtStat::*ptr;
  T Stat::*ptr2;
  C<T> cmp;
};

template <typename T>
void Sort(T first, T last, Options &opt) {
  _ASSERT(opt.order == Order::Asc || opt.order == Order::Desc);
  if (opt.order == Order::Asc)
    std::sort(first, last,
              MtStatComparer<SIZE_T, std::less>{opt.orderby, opt.orderby_gen});
  else
    std::sort(
        first, last,
        MtStatComparer<SIZE_T, std::greater>{opt.orderby, opt.orderby_gen});
}