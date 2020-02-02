#pragma once

class ConsoleCtrlHandler final {
 public:
  explicit ConsoleCtrlHandler() {
    SetConsoleCtrlHandler(ConsoleCtrlHandler::Invoke, TRUE);
  }
  ~ConsoleCtrlHandler() {
    SetConsoleCtrlHandler(ConsoleCtrlHandler::Invoke, FALSE);
    if (IsCancelled()) printf("Operation cancelled by user\n");
  }

 private:
  static BOOL WINAPI Invoke(DWORD code);
};