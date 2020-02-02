#include "application.h"
#include "options.h"
#include "rpc_server.h"

int main() {
  Options options{};
  if (!options.ParseCommandLine(GetCommandLineW())) {
    PrintUsage(stderr);
    return 1;
  }
  if (options.pipename) {
    auto hr = RpcServer{}.Run(options.pipename);
    return FAILED(hr) ? 1 : 0;
  }
  if (!options.pid) {
    if (options.help) {
      PrintUsage(stdout);
      return 0;
    } else {
      PrintUsage(stderr);
      return 1;
    }
  }
  auto hr = Application{}.Run(options);
  if (FAILED(hr)) {
    LogError(hr);
    return 1;
  }
  return 0;
}

PVOID __RPC_API MIDL_user_allocate(size_t size) { return malloc(size); }
void __RPC_API MIDL_user_free(PVOID p) { free(p); }