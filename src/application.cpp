#include "application.h"

#include "clr_data_access.h"
#include "common.h"
#include "mtstat_calculator.h"
#include "rpc_helpers.h"
#include "runas_localsystem.h"

std::atomic<DWORD> AppCore::ServerPid;
std::atomic_bool AppCore::RpcInitialized;

DWORD RpcStubExchangePid(handle_t handle, DWORD pid) {
  AppCore::ServerPid.store(pid);
  return GetCurrentProcessId();
}

void RpcStubLogError(handle_t handle, BSTR message) { LogError(message); }

HRESULT AppCore::CalculateMtStat(DWORD pid, std::vector<MtStat> &mtstat) {
  ProcessContext process_context;
  auto hr = process_context.Initialize(pid);
  if (SUCCEEDED(hr)) {
    hr = ::CalculateMtStat(process_context.process_handle.get(),
                           process_context.sos_dac_interface.get(), mtstat);
    if (SUCCEEDED(hr)) {
      std::swap(process_context, process_context_);
      context_kind_ = ContextKind::Local;
      return hr;
    }
  }
  if (hr != E_ACCESSDENIED) return hr;
  hr = ServerCalculateMtStat(pid, mtstat);
  if (SUCCEEDED(hr)) context_kind_ = ContextKind::Remote;
  return hr;
}

HRESULT AppCore::GetMtName(uintptr_t addr, uint32_t size, PWSTR name,
                           uint32_t *needed) {
  switch (context_kind_) {
    case ContextKind::Local:
      return process_context_.GetMtName(addr, size, name, needed);
    case ContextKind::Remote: {
      _bstr_t bstr;
      auto hr = TryExceptRpc(&RpcProxyGetMtName, server_binding_.get(), addr,
                             bstr.GetAddress());
      if (SUCCEEDED(hr)) {
        hr = StringCchCopyW(name, size, bstr.GetBSTR());
        if (hr == STRSAFE_E_INSUFFICIENT_BUFFER) hr = S_FALSE;
        if (needed) *needed = bstr.length() + 1;
      }
      return hr;
    }
    default:
      return E_FAIL;
  }
}

void AppCore::Cancel() {
  if (RpcInitialized) TryExceptRpc(RpcProxyCancel, server_binding_.get());
}

HRESULT AppCore::ServerCalculateMtStat(DWORD pid, std::vector<MtStat> &mtstat) {
  auto hr = RunServerAsLocalSystem();
  if (FAILED(hr)) return hr;
  SIZE_T size = 0;
  hr =
      TryExceptRpc(&RpcProxyCalculateMtStat, server_binding_.get(), pid, &size);
  if (FAILED(hr)) return hr;
  mtstat.resize(size);
  UINT size32;
  SIZE_T size32max = (std::numeric_limits<DWORD>::max)();
  for (SIZE_T offset = 0; offset < size; offset += size32) {
    size32 = static_cast<UINT>((std::min)(size - offset, size32max));
    boolean ok = FALSE;
    hr = TryExceptRpc(ok, &RpcProxyGetMtStat, server_binding_.get(), offset,
                      size32, &mtstat[0] + offset);
    if (!ok) return FAILED(hr) ? hr : E_UNEXPECTED;
  }
  return S_OK;
}

HRESULT AppCore::RunServerAsLocalSystem() {
  if (ServerPid) return S_FALSE;
  // Run RPC server
  HRESULT hr;
  wchar_t pipename[MAX_PATH];
  auto fail =
      FAILED(hr = StringCchPrintfW(pipename, ARRAYSIZE(pipename),
                                   kPipeNameFormat, GetCurrentProcessId())) ||
      FAILED(hr = RpcInitializeServer(pipename, RpcStubClient_v0_0_s_ifspec));
  if (fail) return hr;
  // Spawn a new process running under the LocalSystem account
  wchar_t filepath[MAX_PATH];
  auto length = GetModuleFileNameW(nullptr, filepath, ARRAYSIZE(filepath) - 1);
  auto error = GetLastError();
  if (!length || error == ERROR_INSUFFICIENT_BUFFER) {
    hr = HRESULT_FROM_WIN32(error);
    return FAILED(hr) ? hr : E_FAIL;
  }
  filepath[length] = 0;
  wchar_t cmdline[MAX_PATH];
  fail = FAILED(hr = StringCchPrintfW(cmdline, ARRAYSIZE(cmdline),
                                      L"\"%s\" --pipe \"%s\"", filepath,
                                      pipename)) ||
         FAILED(hr = RunAsLocalSystem(cmdline));
  if (fail) return hr;
  // Wait for the spawned process connect
  DWORD server_pid;
  for (; !(server_pid = ServerPid.load()); Sleep(1))
    if (IsCancelled()) return S_FALSE;
  // Connect back
  hr = StringCchPrintfW(pipename, ARRAYSIZE(pipename), kPipeNameFormat,
                        server_pid);
  if (SUCCEEDED(hr)) {
    hr = RpcInitializeClient(pipename, &server_binding_);
    RpcInitialized = !!server_binding_;
  }
  return hr;
}

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

class ConsoleCancellationHandler {
  // Must outlive the application
 public:
  explicit ConsoleCancellationHandler(AppCore &application) {
    {
      auto lock = Mutex.lock_exclusive();
      _ASSERT(!Application);
      Application = &application;
    }
    SetConsoleCtrlHandler(ConsoleCancellationHandler::Invoke, TRUE);
  }
  ~ConsoleCancellationHandler() {
    {
      auto lock = Mutex.lock_exclusive();
      Application = nullptr;
    }
    SetConsoleCtrlHandler(ConsoleCancellationHandler::Invoke, FALSE);
    if (IsCancelled()) printf("Operation cancelled by user\n");
  }

 private:
  static BOOL WINAPI Invoke(DWORD code) {
    switch (code) {
      case CTRL_C_EVENT:
      case CTRL_BREAK_EVENT:
      case CTRL_CLOSE_EVENT:
        Cancel();
        CancelApplication();
        return TRUE;
      default:
        return FALSE;
    }
  }
  static void CancelApplication() {
    auto lock = Mutex.lock_exclusive();
    if (Application) Application->Cancel();
  }
  static wil::srwlock Mutex;
  static AppCore *Application;
};

wil::srwlock ConsoleCancellationHandler::Mutex;
AppCore *ConsoleCancellationHandler::Application;

HRESULT Run(Options &options) {
  // Bootstrap
  struct Output : IOutput {
    void Print(PCWSTR str) override { fwprintf(stderr, str); }
  } output;
  auto logger = RegisterLoggerOutput(&output);
  AppCore application;
  ConsoleCancellationHandler cancellation_handler{application};
  // Calculate
  std::vector<MtStat> items;
  auto hr = application.CalculateMtStat(options.pid, items);
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
  PrintWinDbgFormat(first, last, GetStatPtr(options.gen), application);
  return S_OK;
}
