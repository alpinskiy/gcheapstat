#pragma once

struct ProcessInfo final {
  HRESULT Initialize(HANDLE hprocess);

  wchar_t clrname[MAX_PATH];
  wchar_t clrpath[MAX_PATH];
  uintptr_t clrbase;
  bool managed;
  bool coreclr;
};