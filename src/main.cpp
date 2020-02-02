#include "application.h"
#include "options.h"
#include "rpc_server.h"

class CancellationHandler {
 public:
  explicit inline CancellationHandler() {
    SetConsoleCtrlHandler(CancellationHandler::Invoke, TRUE);
  }
  inline ~CancellationHandler() {
    SetConsoleCtrlHandler(CancellationHandler::Invoke, FALSE);
    if (IsCancelled()) printf("Operation cancelled by user\n");
  }

 private:
  static BOOL WINAPI Invoke(DWORD code) {
    switch (code) {
      case CTRL_C_EVENT:
      case CTRL_BREAK_EVENT:
      case CTRL_CLOSE_EVENT:
        Cancel();
        ApplicationProxy::Cancel();
        return TRUE;
      default:
        return FALSE;
    }
  }
};

struct Output : IOutput {
  void Print(PCWSTR str) override { fwprintf(stderr, str); }
};

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
  Output output;
  auto logger = RegisterLoggerOutput(&output);
  CancellationHandler cancellation_handler;
  auto hr = Application{}.Run(options);
  if (FAILED(hr)) {
    LogError(hr);
    return 1;
  }
  return 0;
}

PVOID __RPC_API MIDL_user_allocate(size_t size) { return malloc(size); }
void __RPC_API MIDL_user_free(PVOID p) { free(p); }