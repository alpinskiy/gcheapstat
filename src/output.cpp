#include "output.h"

void Print(IOutput* output, PCWSTR str) {
  if (output != nullptr) output->Print(str);
}

void Printf(IOutput* output, PCWSTR format, ...) {
  thread_local wchar_t Buffer[4 * 1024];
  va_list args;
  va_start(args, format);
  auto len = vswprintf_s(Buffer, ARRAYSIZE(Buffer), format, args);
  va_end(args);
  if (0 < len) {
    Print(output, Buffer);
  }
}
