#include "dac.h"

#include <Psapi.h>
#include <memoryapi.h>
#include <strsafe.h>

#include <cinttypes>

class Dac final : public IDac, IXCLRDataTarget3 {
 public:
  bool Initialize(int pid);
  ~Dac() = default;

 private:
  // IDac
  IXCLRDataTarget3* GetXCLRDataTarget3() override;
  ISOSDacInterface* GetSOSDacInterface() override;
  // clang-format off
  // IUnknown
  STDMETHOD(QueryInterface)(REFIID riid, void** ppvObject) override;
  STDMETHOD_(ULONG, AddRef)() override;
  STDMETHOD_(ULONG, Release)() override;
  // ICLRDataTarget
  STDMETHOD(GetMachineType)(ULONG32* machineType) override;
  STDMETHOD(GetPointerSize)(ULONG32* pointerSize) override;
  STDMETHOD(GetImageBase)(PCWSTR imagePath, CLRDATA_ADDRESS* baseAddress) override;
  STDMETHOD(ReadVirtual)(CLRDATA_ADDRESS address, BYTE* buffer, ULONG32 bytesRequested, ULONG32* bytesRead) override;
  STDMETHOD(WriteVirtual)(CLRDATA_ADDRESS address, BYTE* buffer, ULONG32 bytesRequested, ULONG32* bytesWritten) override;
  STDMETHOD(GetTLSValue)(ULONG32 threadID, ULONG32 index, CLRDATA_ADDRESS* value) override;
  STDMETHOD(SetTLSValue)(ULONG32 threadID, ULONG32 index, CLRDATA_ADDRESS value) override;
  STDMETHOD(GetCurrentThreadID)(ULONG32* threadID) override;
  STDMETHOD(GetThreadContext)(ULONG32 threadID, ULONG32 contextFlags, ULONG32 contextSize, BYTE* context) override;
  STDMETHOD(SetThreadContext)(ULONG32 threadID, ULONG32 contextSize, BYTE* context) override;
  STDMETHOD(Request)(ULONG32 reqCode, ULONG32 inBufferSize, BYTE* inBuffer, ULONG32 outBufferSize, BYTE* outBuffer) override;
  // ICLRDataTarget2
  STDMETHOD(AllocVirtual)(CLRDATA_ADDRESS addr, ULONG32 size, ULONG32 typeFlags, ULONG32 protectFlags, CLRDATA_ADDRESS* virt) override;
  STDMETHOD(FreeVirtual)(CLRDATA_ADDRESS addr, ULONG32 size, ULONG32 typeFlags) override;
  // IXCLRDataTarget3
  STDMETHOD(GetMetaData)(PCWSTR imagePath, ULONG32 imageTimestamp, ULONG32 imageSize, GUID* mvid, ULONG32 mdRva, ULONG32 flags, ULONG32 bufferSize, BYTE* buffer, ULONG32* dataSize) override;
  // clang-format on

  int refcount_{1};
  CLRDATA_ADDRESS pagesize_{};
  wil::unique_handle process_;
  bool coreclr_{false};
  wchar_t clrname_[MAX_PATH]{};
  wchar_t clrpath_[MAX_PATH]{};
  CLRDATA_ADDRESS clrbase_{};
  std::wstring dacpath_;
  wil::unique_hmodule dac_;
  wil::com_ptr<ISOSDacInterface> sos_;
};

namespace {

std::string ToString(HRESULT value) {
  char* buffer = nullptr;
  auto size = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, value, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
      reinterpret_cast<char*>(&buffer), 0, nullptr);
  if (size == 0) {
    return {};
  }
  std::unique_ptr<char, decltype(&LocalFree)> scope{buffer, LocalFree};
  return {buffer, size};
}

}  // namespace

bool Dac::Initialize(int pid) {
  // Query memory page size
  SYSTEM_INFO system_info{};
  GetSystemInfo(&system_info);
  pagesize_ = system_info.dwPageSize ? system_info.dwPageSize : 0x1000;
  if (pagesize_ == 0) {
    Error() << "Memory page size can not be zero!";
    return false;
  }
  if ((pagesize_ & (pagesize_ - 1)) != 0) {
    Error() << "Memory page size must be a power of two!";
    return false;
  }
  // Open process
  DWORD desired_access = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ;
  process_.reset(OpenProcess(desired_access, FALSE, pid));
  // Find CLR
  auto modules = std::vector<HMODULE>(512);
  auto bytes_needed = static_cast<DWORD>(0);
  auto size_in_bytes = static_cast<DWORD>(modules.size() * sizeof(HMODULE));
  for (;;) {
    auto ok = EnumProcessModulesEx(process_.get(), &modules[0], size_in_bytes,
                                   &bytes_needed, LIST_MODULES_ALL);
    if (!ok) {
      auto lasterror = GetLastError();
      Error() << "Could not get module list";
      Error() << ToString(HRESULT_FROM_WIN32(lasterror));
      return false;
    }
    if (bytes_needed <= size_in_bytes) {
      // Success
      break;
    }
    // Next iteration
    modules.resize(bytes_needed / sizeof(HMODULE));
    size_in_bytes = bytes_needed;
  }
  auto i = 0u;
  auto size = bytes_needed / sizeof(HMODULE);
  for (; i < size; ++i) {
    bytes_needed = GetModuleBaseNameW(process_.get(), modules[i], clrname_,
                                      ARRAYSIZE(clrname_));
    if (bytes_needed) {
      if (!wcsncmp(clrname_, L"clr.dll", bytes_needed)) {
        break;
      }
      if (!wcsncmp(clrname_, L"coreclr.dll", bytes_needed)) {
        coreclr_ = true;
        break;
      }
    }
  }
  if (i == size) {
    Error() << "CLR module not found";
    return false;
  }
  clrbase_ = reinterpret_cast<CLRDATA_ADDRESS>(modules[i]);
  auto ok = GetModuleFileNameExW(process_.get(), modules[i], clrpath_,
                                 ARRAYSIZE(clrpath_));
  if (!ok) {
    auto lasterror = GetLastError();
    Error() << "Error resolving CLR module path";
    Error() << ToString(HRESULT_FROM_WIN32(lasterror));
    return false;
  }
  // Load DAC
  auto pos = wcsrchr(clrpath_, '\\');
  if (pos == nullptr) {
    Error() << "Bad CLR module path";
    return false;
  }
  dacpath_.assign(clrpath_, pos);
  dacpath_.append(coreclr_ ? L"\\mscordaccore.dll" : L"\\mscordacwks.dll");
  dac_.reset(LoadLibraryW(dacpath_.c_str()));
  if (!dac_) {
    auto lasterror = GetLastError();
    Error() << "Could not load " << dacpath_;
    Error() << ToString(HRESULT_FROM_WIN32(lasterror));
    return false;
  }
  // Query ISOSDacInterface
  auto pfn = GetProcAddress(dac_.get(), "CLRDataCreateInstance");
  if (pfn == nullptr) {
    Error() << "CLRDataCreateInstance not found";
    return false;
  }
  wil::com_ptr<IXCLRDataProcess> xclrdataprocess;
  auto hr = reinterpret_cast<PFN_CLRDataCreateInstance>(pfn)(
      __uuidof(IXCLRDataProcess), this,
      reinterpret_cast<void**>(&xclrdataprocess));
  if (FAILED(hr)) {
    Error() << "Could not create IXCLRDataProcess instance";
    Error() << ToString(hr);
    return false;
  }
  hr = xclrdataprocess.try_query_to(&sos_);
  if (FAILED(hr)) {
    Error() << "Could not create ISOSDacInterface instance";
    Error() << ToString(hr);
    return false;
  }

  return true;
}

IXCLRDataTarget3* Dac::GetXCLRDataTarget3() { return this; }
ISOSDacInterface* Dac::GetSOSDacInterface() { return sos_.get(); }

HRESULT Dac::QueryInterface(REFIID riid, void** ppvObject) {
  if (IsEqualIID(riid, __uuidof(ICLRDataTarget)) ||
      IsEqualIID(riid, __uuidof(ICLRDataTarget2)) ||
      IsEqualIID(riid, __uuidof(IXCLRDataTarget3))) {
    *ppvObject = this;
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

ULONG Dac::AddRef() { return ++refcount_; }
ULONG Dac::Release() { return --refcount_; }

HRESULT Dac::GetMachineType(ULONG32* machineType) {
#ifdef _WIN64
  *machineType = IMAGE_FILE_MACHINE_AMD64;
#else
  *machineType = IMAGE_FILE_MACHINE_I386;
#endif
  return S_OK;
}

HRESULT Dac::GetPointerSize(ULONG32* pointerSize) {
  *pointerSize = sizeof(PVOID);
  return S_OK;
}

HRESULT Dac::GetImageBase(PCWSTR imagePath, CLRDATA_ADDRESS* baseAddress) {
  if (wcscmp(imagePath, clrname_)) {
    return E_FAIL;
  }

  *baseAddress = clrbase_;
  return S_OK;
}

HRESULT Dac::ReadVirtual(CLRDATA_ADDRESS address, BYTE* buffer,
                         ULONG32 bytesRequested, ULONG32* bytesRead) {
  SIZE_T read = 0;
  if (!ReadProcessMemory(process_.get(), (PVOID)(ULONG_PTR)address, buffer,
                         bytesRequested, &read)) {
    auto hr = HRESULT_FROM_WIN32(GetLastError());
    auto pagefirst = address & ~(pagesize_ - 1);
    auto pagelast = (address + bytesRequested) & ~(pagesize_ - 1);
    Error() << "Error reading process memory at " << address << " (requested "
            << bytesRequested << ", read " << read << " bytes)"
            << (pagefirst != pagelast ? ", cross-page read" : "");
    return hr;
  }

  *bytesRead = static_cast<ULONG32>(read);
  return S_OK;
}

HRESULT Dac::WriteVirtual(CLRDATA_ADDRESS address, BYTE* buffer,
                          ULONG32 bytesRequested, ULONG32* bytesWritten) {
  Debug() << "Not implemented ICLRDataTarget::WriteVirtual";
  return E_NOTIMPL;
}

HRESULT Dac::GetTLSValue(ULONG32 threadID, ULONG32 index,
                         CLRDATA_ADDRESS* value) {
  Debug() << "Not implemented ICLRDataTarget::GetTLSValue";
  return E_NOTIMPL;
}

HRESULT Dac::SetTLSValue(ULONG32 threadID, ULONG32 index,
                         CLRDATA_ADDRESS value) {
  Debug() << "Not implemented ICLRDataTarget::SetTLSValue";
  return E_NOTIMPL;
}

HRESULT Dac::GetCurrentThreadID(ULONG32* threadID) {
  Debug() << "Not implemented ICLRDataTarget::GetCurrentThreadID";
  return E_NOTIMPL;
}

HRESULT Dac::GetThreadContext(ULONG32 threadID, ULONG32 contextFlags,
                              ULONG32 contextSize, BYTE* context) {
  Debug() << "Not implemented ICLRDataTarget::GetThreadContext";
  return E_NOTIMPL;
}

HRESULT Dac::SetThreadContext(ULONG32 threadID, ULONG32 contextSize,
                              BYTE* context) {
  Debug() << "Not implemented ICLRDataTarget::SetThreadContext";
  return E_NOTIMPL;
}

HRESULT Dac::Request(ULONG32 reqCode, ULONG32 inBufferSize, BYTE* inBuffer,
                     ULONG32 outBufferSize, BYTE* outBuffer) {
  Debug() << "Not implemented ICLRDataTarget::Request";
  return E_NOTIMPL;
}

HRESULT Dac::AllocVirtual(CLRDATA_ADDRESS addr, ULONG32 size, ULONG32 typeFlags,
                          ULONG32 protectFlags, CLRDATA_ADDRESS* virt) {
  Debug() << "Not implemented ICLRDataTarget2::AllocVirtual";
  return E_NOTIMPL;
}

HRESULT Dac::FreeVirtual(CLRDATA_ADDRESS addr, ULONG32 size,
                         ULONG32 typeFlags) {
  Debug() << "Not implemented ICLRDataTarget2::FreeVirtual";
  return E_NOTIMPL;
}

HRESULT Dac::GetMetaData(PCWSTR imagePath, ULONG32 imageTimestamp,
                         ULONG32 imageSize, GUID* mvid, ULONG32 mdRva,
                         ULONG32 flags, ULONG32 bufferSize, BYTE* buffer,
                         ULONG32* dataSize) {
  Debug() << "Not implemented IXCLRDataTarget3::GetMetaData";
  return E_NOTIMPL;
}

std::unique_ptr<IDac> CreateDac(int pid) {
  auto dac = std::make_unique<Dac>();
  if (!dac->Initialize(pid)) {
    return nullptr;
  }
  return dac;
}
