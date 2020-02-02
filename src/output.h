#pragma once

struct IOutput {
  // No virtual destructor intentionally
  virtual void Print(PCWSTR str) = 0;
};

void Print(IOutput* output, PCWSTR str);
void Printf(IOutput* output, PCWSTR format, ...);