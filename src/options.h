#pragma once

enum class Order { Asc, Desc };
enum class OrderBy { TotalSize, Count };

struct Options {
  DWORD pid{0};
  PWSTR pipename;
  Order order{Order::Asc};
  OrderBy orderby{OrderBy::TotalSize};
  size_t limit{(std::numeric_limits<size_t>::max)()};
  bool help{false};

  bool ParseCommandLine(PCWSTR cmdline);
};

void PrintUsage(FILE* file);