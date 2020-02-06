#include "application.h"

#include "clr_data_access.h"
#include "mtstat_calculator.h"
#include "rpc_helpers.h"
#include "runas_localsystem.h"
#include "singleton_scope.h"

HRESULT RpcStubExchangePid(handle_t handle, PDWORD pid) {
  return SingletonScope<Application>::Invoke(&Application::ExchangePid, pid);
}

void RpcStubLogError(handle_t handle, BSTR message) { wprintf(message); }

Application::Application()
    : context_kind_{ContextKind::None},
      server_pid_{0},
      server_binding_initialized_{false} {}

HRESULT Application::Run(Options &options) {
  SingletonScope<Application> singleton_scope{this};
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
  PrintWinDbgFormat(first, last, GetStatPtr(options.gen), this);
  return S_OK;
}

HRESULT Application::CalculateMtStat(DWORD pid, std::vector<MtStat> &mtstat) {
  HRESULT hr;
  ProcessContext process_context;
  hr = process_context.Initialize(pid);
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
    hr = TryExceptRpc(&RpcProxyGetMtStat, server_binding_.get(), offset, size32,
                      &mtstat[0] + offset);
    if (FAILED(hr)) return hr;
  }
  return S_OK;
}

HRESULT Application::RunServerAsLocalSystem() {
  if (server_pid_) return S_FALSE;
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
  fail =
      FAILED(hr = StringCchPrintfW(cmdline, ARRAYSIZE(cmdline),
                                   L"\"%s\" --pipe=%s", filepath, pipename)) ||
      FAILED(hr = RunAsLocalSystem(cmdline));
  if (fail) return hr;
  // Wait for the spawned process connect
  DWORD server_pid;
  for (; !(server_pid = server_pid_.load()); Sleep(1))
    if (IsCancelled()) return S_FALSE;
  if (server_pid == -1) return E_INVALIDARG;
  // Connect back
  hr = StringCchPrintfW(pipename, ARRAYSIZE(pipename), kPipeNameFormat,
                        server_pid);
  if (SUCCEEDED(hr)) {
    hr = RpcInitializeClient(pipename, &server_binding_);
    server_binding_initialized_ = !!server_binding_;
  }
  return hr;
}

HRESULT Application::ExchangePid(PDWORD pid) {
  // RunServerAsLocalSystem is blocked until non zero server_pid_ value received
  // Defend against error here by replacing 0 with -1 (both are invalid PIDs)
  auto server_pid = *pid;
  if (!server_pid) server_pid = -1;
  server_pid_.store(server_pid);
  *pid = GetCurrentProcessId();
  return S_OK;
}

void Application::Cancel() {
  if (server_binding_initialized_)
    TryExceptRpc(RpcProxyCancel, server_binding_.get());
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