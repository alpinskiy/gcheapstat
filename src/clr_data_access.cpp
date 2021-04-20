#include "clr_data_access.h"

#include "clr_data_target_impl.h"

HMODULE LoadDataAccessModule(ProcessInfo* procinfo) {
  if (!procinfo->managed || !procinfo->clrpath[0]) return nullptr;
  HRESULT hr;
  wchar_t path[MAX_PATH];
  auto fail =
      FAILED(hr = StringCchCopyW(path, ARRAYSIZE(path), procinfo->clrpath)) ||
      FAILED(hr = PathCchRemoveFileSpec(path, ARRAYSIZE(path))) ||
      FAILED(hr = PathCchCombine(
                 path, MAX_PATH, path,
                 procinfo->coreclr ? L"mscordaccore.dll" : L"mscordacwks.dll"));
  return fail ? nullptr : LoadLibraryW(path);
}

HRESULT CreateXCLRDataProcess(HANDLE process, ProcessInfo* procinfo,
                              HMODULE dac,
                              IXCLRDataProcess** xclr_data_process) {
  auto factory_proc = reinterpret_cast<PFN_CLRDataCreateInstance>(
      GetProcAddress(dac, "CLRDataCreateInstance"));
  if (!factory_proc) return E_NOINTERFACE;
  wil::com_ptr<ICLRDataTarget> clr_data_target;
  CreateClrDataTarget(process, procinfo, &clr_data_target);
  return factory_proc(__uuidof(IXCLRDataProcess), clr_data_target.get(),
                      reinterpret_cast<PVOID*>(xclr_data_process));
}