#pragma once
#include "process_info.h"
#include "xclrdata.h"  // IXCLRDataProcess

HMODULE LoadDataAccessModule(ProcessInfo* procinfo);
HRESULT CreateXCLRDataProcess(HANDLE process, ProcessInfo* procinfo,
                              HMODULE dac,
                              IXCLRDataProcess** xclr_data_process);
