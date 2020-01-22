#include "application.h"
#include "logger.h"
#include "options.h"

Stat MtStat::*GetStatPtr(int gen) {
  _ASSERT(-1 <= gen && gen <= 3);
  switch (gen) {
    case 0:
      return &MtStat::gen0;
    case 1:
      return &MtStat::gen1;
      break;
    case 2:
      return &MtStat::gen2;
      break;
    case 3:
      return &MtStat::gen3;
      break;
    default:
      return &MtStat::stat;
  }
}

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

template <typename T>
void PrintWinDbgFormat(T first, T last, Stat MtStat::*ptr, AppCore &app) {
#ifdef _WIN64
  constexpr auto kHeader =
      "              MT    Count    TotalSize Class Name\n";
  constexpr auto kRowFormat = L"%016" PRIx64 "%9" PRIu64 "%13" PRIu64 " ";
#else
  constexpr auto kHeader = "      MT    Count    TotalSize Class Name\n";
  constexpr auto kRowFormat = L"%08" PRIx32 "%9" PRIu32 "%13" PRIu32 " ";
#endif
  printf(kHeader);
  wchar_t buffer[1024];
  size_t total_count = 0;
  size_t total_size = 0;
  for (auto it = first; it != last; ++it) {
    if (IsCancelled()) return;
    auto count = (*it.*ptr).count;
    auto size = (*it.*ptr).size_total;
    if (!count && !size) continue;
    wprintf(kRowFormat, it->addr, count, size);
    uint32_t needed;
    auto hr = app.GetMtName(it->addr, ARRAYSIZE(buffer), buffer, &needed);
    if (SUCCEEDED(hr))
      wprintf(L"%s\n", buffer);
    else
      wprintf(L"<error getting class name, code 0x%08lx>\n", hr);
    total_count += count;
    total_size += size;
  }
  printf("Total %" PRIuPTR " objects\n", total_count);
  printf("Total size %" PRIuPTR " bytes\n", total_size);
}

int main() {
  Options options{};
  if (!options.ParseCommandLine(GetCommandLineW())) {
    PrintUsage(stderr);
    return 1;
  }
  if (options.pipename) {
    auto hr = RpcServer{}.Run(options.pipename);
    return FAILED(hr) ? 1 : 0;
  }
  if (!options.pid) {
    if (options.help) {
      PrintUsage(stdout);
      return 0;
    } else {
      PrintUsage(stderr);
      return 1;
    }
  }
  // Bootstrap
  struct ConsoleErr : IOutput {
    void Print(PCWSTR str) override { fwprintf(stderr, str); }
  } cerr;
  auto cerr_registration = RegisterLoggerOutput(&cerr);
  struct ConsoleCtrlHandler {
    ConsoleCtrlHandler() {
      SetConsoleCtrlHandler(ConsoleCtrlHandler::Invoke, TRUE);
    }
    ~ConsoleCtrlHandler() {
      if (IsCancelled()) printf("Operation cancelled by user\n");
    }
    static BOOL WINAPI Invoke(DWORD code) {
      switch (code) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
          Cancel();
          return TRUE;
        default:
          return FALSE;
      }
    }
  } console_ctrl_handler;
  // Calculate
  AppCore app;
  std::vector<MtStat> items;
  auto hr = app.CalculateMtStat(options.pid, items);
  if (FAILED(hr)) {
    LogError(hr);
    return 1;
  }
  if (IsCancelled()) return 0;
  // Sort
  Sort(items.begin(), items.end(), options);
  // Print
  auto first = items.begin();
  auto last = first;
  std::advance(last, (std::min)(items.size(), options.limit));
  PrintWinDbgFormat(first, last, GetStatPtr(options.gen), app);
  return 0;
}