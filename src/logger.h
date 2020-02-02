#pragma once
#include "output.h"

class Logger {
  void RegisterOutput(IOutput* output);
  void UnregisterOutput(IOutput* output);
  void Print(PCWSTR str);

  template <class... Args>
  void Printf(PCWSTR format, Args&&... args) {
    auto hr = StringCchPrintfW(Buffer, ARRAYSIZE(Buffer), format,
                               std::forward<Args>(args)...);
    _ASSERT(SUCCEEDED(hr));
    if (SUCCEEDED(hr)) Print(Buffer);
  }

  static thread_local WCHAR Buffer[1024];
  std::vector<IOutput*> outputs_;
  wil::srwlock mutex_;
  friend class LoggerRegistration;
  template <class... Args>
  friend void LogError(PCWSTR format, Args&&... args);
};

class LoggerRegistration {
 public:
  LoggerRegistration();
  ~LoggerRegistration();
  LoggerRegistration(const LoggerRegistration&) = delete;
  LoggerRegistration(LoggerRegistration&& other);
  LoggerRegistration& operator=(const LoggerRegistration&) = delete;
  LoggerRegistration& operator=(LoggerRegistration&& other);

  void Terminate();

 private:
  LoggerRegistration(IOutput* output);
  friend LoggerRegistration RegisterLoggerOutput(IOutput* output);
  IOutput* output_;
};

extern Logger TheLogger;
LoggerRegistration RegisterLoggerOutput(IOutput* output);

template <class... Args>
inline void LogError(PCWSTR format, Args&&... args) {
  TheLogger.Printf(format, std::forward<Args>(args)...);
}