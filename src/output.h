#pragma once

struct IOutput {
  virtual void Print(PCWSTR str) = 0;
};

void Print(IOutput* output, PCWSTR str);
void Printf(IOutput* output, PCWSTR format, ...);