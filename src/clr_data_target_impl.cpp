#include "clr_data_target_impl.h"

ClrDataTargetImpl::ClrDataTargetImpl(HANDLE process, ProcessInfo *procinfo)
    : ref_count_{0}, process_{process}, procinfo_{procinfo} {
  SYSTEM_INFO system_info{};
  GetSystemInfo(&system_info);
  pagesize_ = system_info.dwPageSize ? system_info.dwPageSize : 0x1000;
  _ASSERT_EXPR(pagesize_, L"Memory page size can not be zero!");
  _ASSERT_EXPR((pagesize_ & (pagesize_ - 1)) == 0,
               L"Memory page size must be a power of two!");
}

HRESULT ClrDataTargetImpl::QueryInterface(REFIID riid, void **ppvObject) {
  if (IsEqualIID(riid, __uuidof(ICLRDataTarget)) ||
      IsEqualIID(riid, __uuidof(ICLRDataTarget2)) ||
      IsEqualIID(riid, __uuidof(IXCLRDataTarget3))) {
    *ppvObject = this;
    AddRef();
    return S_OK;
  }
  _ASSERT(IsEqualIID(riid, __uuidof(ICLRMetadataLocator)) ||
          IsEqualIID(riid, __uuidof(ICLRDataTarget3)));
  return E_NOINTERFACE;
}

ULONG ClrDataTargetImpl::AddRef() { return ++ref_count_; }

ULONG ClrDataTargetImpl::Release() {
  if (--ref_count_) return ref_count_;
  delete this;
  return 0;
}

HRESULT ClrDataTargetImpl::GetMachineType(ULONG32 *machineType) {
#ifdef _WIN64
  *machineType = IMAGE_FILE_MACHINE_AMD64;
#else
  *machineType = IMAGE_FILE_MACHINE_I386;
#endif
  return S_OK;
}

HRESULT ClrDataTargetImpl::GetPointerSize(ULONG32 *pointerSize) {
  *pointerSize = sizeof(PVOID);
  return S_OK;
}

HRESULT ClrDataTargetImpl::GetImageBase(PCWSTR imagePath,
                                        CLRDATA_ADDRESS *baseAddress) {
  if (wcscmp(imagePath, procinfo_->clrname)) {
    _ASSERT_EXPR(FALSE, L"Not implemented ICLRDataTarget::GetImageBase.");
    return E_FAIL;
  }
  *baseAddress = procinfo_->clrbase;
  return S_OK;
}

HRESULT ClrDataTargetImpl::ReadVirtual(CLRDATA_ADDRESS address, BYTE *buffer,
                                       ULONG32 bytesRequested,
                                       ULONG32 *bytesRead) {
  SIZE_T read = 0;
  if (!ReadProcessMemory(process_, (PVOID)(ULONG_PTR)address, buffer,
                         bytesRequested, &read)) {
    auto hr = HRESULT_FROM_WIN32(GetLastError());
    auto pagefirst = address & ~(pagesize_ - 1);
    auto pagelast = (address + bytesRequested) & ~(pagesize_ - 1);
    LogError(L"Error reading process memory at 0x%" PRIX64
             " (requested %" PRIu32 ", read %zu bytes)%s\n",
             address, bytesRequested, read,
             pagefirst != pagelast ? L", cross-page read" : L"");
    return hr;
  }
  *bytesRead = static_cast<ULONG32>(read);
  return S_OK;
}

HRESULT ClrDataTargetImpl::WriteVirtual(CLRDATA_ADDRESS address, BYTE *buffer,
                                        ULONG32 bytesRequested,
                                        ULONG32 *bytesWritten) {
  _ASSERT_EXPR(FALSE, L"Not implemented ICLRDataTarget::WriteVirtual.");
  return E_NOTIMPL;
}

HRESULT ClrDataTargetImpl::GetTLSValue(ULONG32 threadID, ULONG32 index,
                                       CLRDATA_ADDRESS *value) {
  _ASSERT_EXPR(FALSE, L"Not implemented ICLRDataTarget::GetTLSValue.");
  return E_NOTIMPL;
}

HRESULT ClrDataTargetImpl::SetTLSValue(ULONG32 threadID, ULONG32 index,
                                       CLRDATA_ADDRESS value) {
  _ASSERT_EXPR(FALSE, L"Not implemented ICLRDataTarget::SetTLSValue.");
  return E_NOTIMPL;
}

HRESULT ClrDataTargetImpl::GetCurrentThreadID(ULONG32 *threadID) {
  _ASSERT_EXPR(FALSE, L"Not implemented ICLRDataTarget::GetCurrentThreadID.");
  return E_NOTIMPL;
}

HRESULT ClrDataTargetImpl::GetThreadContext(ULONG32 threadID,
                                            ULONG32 contextFlags,
                                            ULONG32 contextSize,
                                            BYTE *context) {
  _ASSERT_EXPR(FALSE, L"Not implemented ICLRDataTarget::GetThreadContext.");
  return E_NOTIMPL;
}

HRESULT ClrDataTargetImpl::SetThreadContext(ULONG32 threadID,
                                            ULONG32 contextSize,
                                            BYTE *context) {
  _ASSERT_EXPR(FALSE, L"Not implemented ICLRDataTarget::SetThreadContext.");
  return E_NOTIMPL;
}

HRESULT ClrDataTargetImpl::Request(ULONG32 reqCode, ULONG32 inBufferSize,
                                   BYTE *inBuffer, ULONG32 outBufferSize,
                                   BYTE *outBuffer) {
  _ASSERT_EXPR(FALSE, L"Not implemented ICLRDataTarget::Request.");
  return E_NOTIMPL;
}

HRESULT ClrDataTargetImpl::AllocVirtual(CLRDATA_ADDRESS addr, ULONG32 size,
                                        ULONG32 typeFlags, ULONG32 protectFlags,
                                        CLRDATA_ADDRESS *virt) {
  _ASSERT_EXPR(FALSE, L"Not implemented ICLRDataTarget2::AllocVirtual.");
  return E_NOTIMPL;
}

HRESULT ClrDataTargetImpl::FreeVirtual(CLRDATA_ADDRESS addr, ULONG32 size,
                                       ULONG32 typeFlags) {
  _ASSERT_EXPR(FALSE, L"Not implemented ICLRDataTarget2::FreeVirtual.");
  return E_NOTIMPL;
}

HRESULT ClrDataTargetImpl::GetMetaData(PCWSTR imagePath, ULONG32 imageTimestamp,
                                       ULONG32 imageSize, GUID *mvid,
                                       ULONG32 mdRva, ULONG32 flags,
                                       ULONG32 bufferSize, BYTE *buffer,
                                       ULONG32 *dataSize) {
  _ASSERT_EXPR(FALSE, L"Not implemented IXCLRDataTarget3::GetMetaData.");
  return E_NOTIMPL;
}

void CreateClrDataTarget(HANDLE process, ProcessInfo *procinfo,
                         ICLRDataTarget **clr_data_target) {
  auto pimpl = std::make_unique<ClrDataTargetImpl>(process, procinfo);
  (*clr_data_target = pimpl.get())->AddRef();
  pimpl.release();
}
