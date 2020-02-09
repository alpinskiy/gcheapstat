#pragma once
#include "rpc_server.h"
#include "singleton_scope.h"

enum class LogMode { None, Console, Pipe };

class Log {
 public:
  static LogMode Mode;
  static bool Verbose;

  template <class... Args>
  static void Write(bool verbose, PCWSTR format, Args&&... args) {
    if (Verbose < verbose) return;
    switch (Mode) {
      case LogMode::Console:
        fwprintf(stderr, format, std::forward<Args>(args)...);
        break;
      case LogMode::Pipe:
        SingletonScope<RpcServer>::Invoke(&RpcServer::LogWrite<Args...>, format,
                                          std::forward<Args>(args)...);
        break;
      default:
        _ASSERT(false);
    }
  }
};

template <class... Args>
void LogError(PCWSTR format, Args&&... args) {
  Log::Write(false, format, std::forward<Args>(args)...);
}

template <class... Args>
void LogDebug(PCWSTR format, Args&&... args) {
  Log::Write(true, format, std::forward<Args>(args)...);
}