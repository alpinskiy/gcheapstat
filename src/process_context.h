#pragma once
#include <sospriv.h>

struct ProcessContext final {
  HRESULT Initialize(DWORD pid);
  HRESULT GetMtName(uintptr_t addr, uint32_t size, PWSTR name,
                    uint32_t *needed);

  wil::unique_handle process_handle;
  wil::unique_hmodule dac_module_handle;
  wil::com_ptr<ISOSDacInterface> sos_dac_interface;
};