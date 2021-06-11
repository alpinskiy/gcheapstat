#pragma once
#include <dacprivate.h>

struct IDac {
  virtual ~IDac() = default;
  virtual IXCLRDataTarget3* GetXCLRDataTarget3() = 0;
  virtual ISOSDacInterface* GetSOSDacInterface() = 0;
};

class TypeNameProvider final {
 public:
  explicit TypeNameProvider(IDac* dac)
      : sos_{dac->GetSOSDacInterface()}, buffer_(512) {}

  TypeNameProvider(const TypeNameProvider&) = delete;
  TypeNameProvider(TypeNameProvider&&) = delete;
  TypeNameProvider& operator=(const TypeNameProvider&) = delete;
  TypeNameProvider& operator=(TypeNameProvider&&) = delete;

  std::string operator()(CLRDATA_ADDRESS address) {
    for (auto i = 0;; ++i) {
      uint32_t needed = 0;
      auto hr = sos_->GetMethodTableName(address, buffer_.size(), &buffer_[0],
                                         &needed);
      if (FAILED(hr)) {
        std::ostringstream name{};
        name << " <error getting class name, code " << hr;
        return name.str();
      }
      if (needed <= buffer_.size() || 0 < i) {
        return std::wstring_convert<std::codecvt_utf8_utf16<WCHAR>, WCHAR>{}
            .to_bytes(&buffer_[0]);
      }
      buffer_.resize(needed);
    }
  }

 private:
  ISOSDacInterface* sos_;
  std::vector<WCHAR> buffer_;
};

std::unique_ptr<IDac> CreateDac(int pid);
