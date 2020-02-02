#pragma once
#include "rpc_server.h"
#include "singleton_scope.h"

enum class LoggerMode { None, Console, RpcServer };
extern LoggerMode Mode;

template <class... Args>
void LogError(PCWSTR format, Args&&... args) {
  switch (Mode) {
    case LoggerMode::Console:
      wprintf(format, std::forward<Args>(args)...);
      break;
    case LoggerMode::RpcServer:
      SingletonScope<RpcServer>::Invoke(&RpcServer::LogError<Args...>, format,
                                        std::forward<Args>(args)...);
      break;
    default:
      _ASSERT(false);
  }
}