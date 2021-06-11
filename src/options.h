#pragma once
#include <limits>

enum class Order { Asc, Desc };
enum class OrderBy { TotalSize, Count };

struct Options final {
  int pid{0};
  Order order{Order::Asc};
  OrderBy orderby{OrderBy::TotalSize};
  int orderby_gen{DAC_NUMBERGENERATIONS};
  std::size_t limit{(std::numeric_limits<std::size_t>::max)()};
  int gen{-1};
  bool help{false};
  bool verbose{false};
  bool version{false};
  bool strict{false};
  bool json;
  int json_indent{-1};

  bool ParseCommandLine(int argc, char* argv[]);
};

void PrintHelp(char* argv0);
void PrintVersion();
std::string GetProgramName(char* argv0);
