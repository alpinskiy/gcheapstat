#pragma once
#include "process_info.h"
#include "xclrdata.h"

class ClrDataTargetImpl final : public IXCLRDataTarget3 {
 public:
  ClrDataTargetImpl(HANDLE process, ProcessInfo *procinfo);

 private:
  // clang-format off
  // IUnknown
  STDMETHOD(QueryInterface)(REFIID riid, void **ppvObject);
  STDMETHOD_(ULONG,AddRef)();
  STDMETHOD_(ULONG,Release)();
  // ICLRDataTarget
  STDMETHOD(GetMachineType)(ULONG32 *machineType);
  STDMETHOD(GetPointerSize)(ULONG32 *pointerSize);
  STDMETHOD(GetImageBase)(PCWSTR imagePath, CLRDATA_ADDRESS *baseAddress);
  STDMETHOD(ReadVirtual)(CLRDATA_ADDRESS address, BYTE *buffer, ULONG32 bytesRequested, ULONG32 *bytesRead);
  STDMETHOD(WriteVirtual)(CLRDATA_ADDRESS address, BYTE *buffer, ULONG32 bytesRequested, ULONG32 *bytesWritten);
  STDMETHOD(GetTLSValue)(ULONG32 threadID, ULONG32 index, CLRDATA_ADDRESS *value);
  STDMETHOD(SetTLSValue)(ULONG32 threadID, ULONG32 index, CLRDATA_ADDRESS value);
  STDMETHOD(GetCurrentThreadID)(ULONG32 *threadID);
  STDMETHOD(GetThreadContext)(ULONG32 threadID, ULONG32 contextFlags, ULONG32 contextSize, BYTE *context);
  STDMETHOD(SetThreadContext)(ULONG32 threadID, ULONG32 contextSize, BYTE *context);
  STDMETHOD(Request)(ULONG32 reqCode, ULONG32 inBufferSize, BYTE *inBuffer, ULONG32 outBufferSize, BYTE *outBuffer);
  // ICLRDataTarget2
  STDMETHOD(AllocVirtual)(CLRDATA_ADDRESS addr, ULONG32 size, ULONG32 typeFlags, ULONG32 protectFlags, CLRDATA_ADDRESS* virt);
  STDMETHOD(FreeVirtual)(CLRDATA_ADDRESS addr, ULONG32 size, ULONG32 typeFlags);
  // IXCLRDataTarget3
  STDMETHOD(GetMetaData)(PCWSTR imagePath, ULONG32 imageTimestamp, ULONG32 imageSize, GUID* mvid, ULONG32 mdRva, ULONG32 flags, ULONG32 bufferSize, BYTE* buffer, ULONG32* dataSize);
  // clang-format on

  ULONG ref_count_;
  HANDLE process_;
  ProcessInfo *procinfo_;
  CLRDATA_ADDRESS pagesize_;
};

void CreateClrDataTarget(HANDLE process, ProcessInfo *procinfo,
                         ICLRDataTarget **clr_data_target);
