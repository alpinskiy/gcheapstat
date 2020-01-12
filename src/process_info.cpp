#include "process_info.h"

#include <psapi.h>

HRESULT ProcessInfo::Initialize(HANDLE hprocess) {
  DWORD n = 0;
  HMODULE modules[1024];
  if (!EnumProcessModulesEx(hprocess, modules, sizeof(modules), &n,
                            LIST_MODULES_ALL)) {
    auto error = GetLastError();
    return HRESULT_FROM_WIN32(error);
  }
  auto i = 0u;
  auto size = n / sizeof(HMODULE);
  managed = false;
  coreclr = false;
  for (; i < size; ++i) {
    n = GetModuleBaseNameW(hprocess, modules[i], clrname, ARRAYSIZE(clrname));
    if (n) {
      if (!wcsncmp(clrname, L"clr.dll", n)) {
        managed = true;
        break;
      }
      if (!wcsncmp(clrname, L"coreclr.dll", n)) {
        managed = true;
        coreclr = true;
        break;
      }
    }
  }
  if (i == size) return E_NOINTERFACE;  // CLR module not found
  clrbase = reinterpret_cast<uintptr_t>(modules[i]);
  if (GetModuleFileNameExW(hprocess, modules[i], clrpath, ARRAYSIZE(clrpath)))
    return S_OK;
  auto error = GetLastError();
  return HRESULT_FROM_WIN32(error);
}
