#pragma once
#include "rpc_server.h"

extern bool RpcServerMode;

template <class... Args>
void LogError(PCWSTR format, Args&&... args) {
  if (!RpcServerMode)
    wprintf(format, std::forward<Args>(args)...);
  else
    RpcServerProxy::LogError(format, std::forward<Args>(args)...);
}