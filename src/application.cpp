#include "application.h"

#include "clr_data_access.h"
#include "mtstat_calculator.h"
#include "rpc_helpers.h"
#include "runas_localsystem.h"

DWORD RpcStubExchangePid(handle_t handle, DWORD pid) {
  return ApplicationProxy::ExchangePid(pid);
}

void RpcStubLogError(handle_t handle, BSTR message) { LogError(message); }

HRESULT Application::Run(Options &options) {
  // Bootstrap
  StdErrorOutput cerr;
  auto logger = RegisterLoggerOutput(&cerr);
  ApplicationProxy proxy{this};
  ConsoleCancellationHandler cancellation;
  // Calculate
  std::vector<MtStat> items;
  auto hr = CalculateMtStat(options.pid, items);
  if (FAILED(hr)) return hr;
  if (IsCancelled()) return S_FALSE;
  // Sort
  Sort(items.begin(), items.end(), options);
  // Print
  auto first = items.begin();
  auto last = first;
  std::advance(last, (std::min)(items.size(), options.limit));
  PrintWinDbgFormat(first, last, GetStatPtr(options.gen));
  return S_OK;
}

void Application::PrintWinDbgFormat(mtstat_iterator first, mtstat_iterator last,
                                    Stat MtStat::*ptr) {
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
    auto hr = GetMtName(it->addr, ARRAYSIZE(buffer), buffer, &needed);
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

HRESULT Application::CalculateMtStat(DWORD pid, std::vector<MtStat> &mtstat) {
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

HRESULT Application::GetMtName(uintptr_t addr, uint32_t size, PWSTR name,
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

HRESULT Application::ServerCalculateMtStat(DWORD pid,
                                           std::vector<MtStat> &mtstat) {
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

HRESULT Application::RunServerAsLocalSystem() {
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

DWORD Application::ExchangePid(DWORD pid) {
  ServerPid.store(pid);
  return GetCurrentProcessId();
}

void Application::Cancel() {
  if (RpcInitialized) TryExceptRpc(RpcProxyCancel, server_binding_.get());
}

ApplicationProxy::ApplicationProxy(Application *application)
    : Proxy{application} {}

DWORD ApplicationProxy::ExchangePid(DWORD pid) {
  auto lock = Mutex.lock_exclusive();
  return Instance ? Instance->ExchangePid(pid) : -1;
}

void ApplicationProxy::Cancel() {
  auto lock = Mutex.lock_exclusive();
  if (Instance) Instance->Cancel();
}

void StdErrorOutput::Print(PCWSTR str) { fwprintf(stderr, str); }

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