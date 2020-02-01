#include "process_context.h"

#include "clr_data_access.h"
#include "process_info.h"

HRESULT ProcessContext::Initialize(DWORD pid) {
  DWORD desired_access = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ;
  process_handle.reset(OpenProcess(desired_access, FALSE, pid));
  if (!process_handle) return HRESULT_FROM_WIN32(GetLastError());
  ProcessInfo info;
  auto hr = info.Initialize(process_handle.get());
  if (FAILED(hr)) return hr;
  if (!info.managed) return E_NOINTERFACE;
  dac_module_handle.reset(LoadDataAccessModule(&info));
  if (!dac_module_handle) return HRESULT_FROM_WIN32(GetLastError());
  wil::com_ptr<IXCLRDataProcess> xclr_data_process;
  hr = CreateXCLRDataProcess(process_handle.get(), &info,
                             dac_module_handle.get(), &xclr_data_process);
  if (FAILED(hr)) return hr;
  return xclr_data_process.try_query_to(&sos_dac_interface) ? S_OK
                                                            : E_NOINTERFACE;
}

HRESULT ProcessContext::GetMtName(uintptr_t addr, uint32_t size, PWSTR name,
                                  uint32_t *needed) {
  return sos_dac_interface
             ? sos_dac_interface->GetMethodTableName(addr, size, name, needed)
             : E_NOINTERFACE;
}