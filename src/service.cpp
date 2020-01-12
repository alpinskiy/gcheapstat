#include <strsafe.h>
#include <windows.h>

#include "wil/resource.h"

void Run(DWORD argc, LPWSTR* argv) {
  if (argc <= 1 || !argv[1]) return;
  static WCHAR CommandLine[32768];
  auto dest = CommandLine;
  auto size = ARRAYSIZE(CommandLine);
  for (DWORD i = 1; i < argc; ++i) {
    if (1 < i) {
      auto hr = StringCchCatExW(dest, size, L" ", &dest, &size, 0);
      if (FAILED(hr)) return;
    }
    SIZE_T j = 0;
    auto src = argv[i];
    for (; src[j] && src[j] != ' '; ++j)
      ;
    HRESULT hr;
    if (src[j] == ' ')
      hr = StringCchPrintfExW(dest, size, &dest, &size, 0, L"\"%s\"", src);
    else
      hr = StringCchCatExW(dest, size, src, &dest, &size, 0);
    if (FAILED(hr)) return;
  }
  STARTUPINFOW startup_info{sizeof(STARTUPINFOW)};
  wil::unique_process_information process_information;
  CreateProcessW(nullptr, CommandLine, nullptr, nullptr, FALSE, 0, nullptr,
                 nullptr, &startup_info, &process_information);
}

void WINAPI CtrlHandler(DWORD ctrl) {}
void WINAPI ServiceMain(DWORD argc, LPWSTR* argv) {
  auto hstatus = RegisterServiceCtrlHandlerW(L"", CtrlHandler);
  Run(argc, argv);
  SERVICE_STATUS status{SERVICE_WIN32_OWN_PROCESS, SERVICE_STOPPED};
  SetServiceStatus(hstatus, &status);
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
                    _In_ LPWSTR lpCmdLine, _In_ int nShowCmd) {
  WCHAR dummy = 0;
  SERVICE_TABLE_ENTRYW dispatch_table[] = {{&dummy, ServiceMain},
                                           {nullptr, nullptr}};
  StartServiceCtrlDispatcherW(dispatch_table);
}