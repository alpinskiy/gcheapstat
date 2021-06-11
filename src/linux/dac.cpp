#include "dac.h"

#include <fcntl.h>
#include <unistd.h>

#include <cinttypes>
#include <fstream>
#include <locale>
#include <sstream>

class Dac final : public IDac, IXCLRDataTarget3 {
 public:
  bool Initialize(int pid);
  ~Dac() override;

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

  static void Release2(IUnknown* ptr) { ptr->Release(); }

  int refcount_{1};
  int fdmem_{};
  std::u16string clrname_{u"libcoreclr.so"};
  std::string clrpath_;
  CLRDATA_ADDRESS clrbase_{};
  std::unique_ptr<void, decltype(&dlclose)> dac_{nullptr, dlclose};
  std::unique_ptr<ISOSDacInterface, decltype(&Release2)> sos_{nullptr,
                                                              Release2};
};

bool Dac::Initialize(int pid) {
  // Open process memory file for reading
  auto stream = std::ostringstream{};
  stream << "/proc/" << pid << "/mem";
  auto mem_path = stream.str();
  fdmem_ = open(mem_path.c_str(), O_RDONLY);
  if (fdmem_ < 0) {
    Error() << "Could not open " << mem_path;
    return false;
  }
  // Find CLR
  std::ostringstream{}.swap(stream);
  stream << "/proc/" << pid << "/maps";
  auto maps_path = stream.str();
  std::ifstream maps{maps_path};
  if (!maps.is_open()) {
    Error() << "Could not open " << maps_path;
    return false;
  }
  CLRDATA_ADDRESS addr{};
  auto path = std::vector<char>();
  auto clrfound = false;
  auto clrlenth = strlen("libcoreclr.so");
  for (std::string line; std::getline(maps, line);) {
    if (path.size() < line.size()) {
      path.resize(line.size());
    }
    auto n =
        sscanf(line.c_str(), "%tx-%*tx %*s %*s %*s %*s %s", &addr, &path[0]);
    if (n == 2) {
      auto pathlen = strlen(&path[0]);
      if (clrlenth <= pathlen &&
          strcmp(&path[pathlen - clrlenth], "libcoreclr.so") == 0) {
        clrfound = true;
        clrbase_ = addr;
        clrpath_.assign(&path[0], pathlen);
        break;
      }
    }
  }
  if (!clrfound) {
    Error() << "CLR module not found";
    return false;
  }
  // Load DAC
  std::string dacpath{clrpath_.substr(0, clrpath_.find_last_of('/')) +
                      "/libmscordaccore.so"};
  dac_.reset(dlopen(dacpath.c_str(), RTLD_NOW));
  if (!dac_) {
    Error() << "Error loading " << dacpath;
    Error() << dlerror();
    return false;
  }
  // Initialize PAL
  auto pfn = dlsym(dac_.get(), "DAC_PAL_InitializeDLL");
  if (!pfn) {
    Error() << "DAC_PAL_InitializeDLL not found";
    return false;
  }
  auto res = reinterpret_cast<int(PALAPI*)()>(pfn)();
  if (res != 0) {
    Error() << "Error initializing DAC_PAL";
    return false;
  }
  // Query ISOSDacInterface
  pfn = dlsym(dac_.get(), "CLRDataCreateInstance");
  if (pfn == nullptr) {
    Error() << "CLRDataCreateInstance not found";
    return false;
  }
  IXCLRDataProcess* xclrdataprocess{};
  auto hr = reinterpret_cast<PFN_CLRDataCreateInstance>(pfn)(
      __uuidof(IXCLRDataProcess), this,
      reinterpret_cast<void**>(&xclrdataprocess));
  if (FAILED(hr)) {
    Error() << "Could not create IXCLRDataProcess instance";
    return false;
  }
  std::unique_ptr<IXCLRDataProcess, decltype(&Release2)> xclrdataprocess_scope{
      xclrdataprocess, Release2};
  ISOSDacInterface* sos{};
  hr = xclrdataprocess->QueryInterface(__uuidof(ISOSDacInterface),
                                       reinterpret_cast<void**>(&sos));
  if (FAILED(hr)) {
    Error() << "Could not create ISOSDacInterface instance";
    return false;
  }
  sos_.reset(sos);
  return true;
}

Dac::~Dac() {
  if (fdmem_ != 0) {
    close(fdmem_);
  }
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
  if (clrname_.compare(imagePath) != 0) {
    return E_FAIL;
  }

  *baseAddress = clrbase_;
  return S_OK;
}

HRESULT Dac::ReadVirtual(CLRDATA_ADDRESS address, BYTE* buffer,
                         ULONG32 bytesRequested, ULONG32* bytesRead) {
  auto res = pread(fdmem_, buffer, bytesRequested, address);
  if (res < 0) {
    Error() << "Error " << errno << " reading process memory at 0x" << std::hex
            << address << " (requested " << bytesRequested << ")";
    return E_FAIL;
  }

  *bytesRead = static_cast<ULONG32>(res);
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
