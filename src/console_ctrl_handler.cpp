#include "console_ctrl_handler.h"

#include "application.h"

BOOL ConsoleCtrlHandler::Invoke(DWORD code) {
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