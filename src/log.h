#pragma once
#include "rpc_server.h"
#include "singleton_scope.h"

enum class LogMode { None, Console, Pipe };

class Log {
 public:
  static LogMode Mode;
  static int Level;

  template <class... Args>
  static void Write(int level, PCWSTR format, Args&&... args) {
    if (Level < level) return;
    switch (Mode) {
      case LogMode::Console:
        wprintf(format, std::forward<Args>(args)...);
        break;
      case LogMode::Pipe:
        SingletonScope<RpcServer>::Invoke(&RpcServer::LogError<Args...>, format,
                                          std::forward<Args>(args)...);
        break;
      default:
        _ASSERT(false);
    }
  }
};

template <class... Args>
void LogError(PCWSTR format, Args&&... args) {
  Log::Write(0, format, std::forward<Args>(args)...);
}

template <class... Args>
void LogDebug(PCWSTR format, Args&&... args) {
  Log::Write(1, format, std::forward<Args>(args)...);
}