#include "format.h"
#include "statistics.h"

int Log::Level;
int Log::ErrorCount;

int main(int argc, char* argv[]) {
  Options options{};
  if (options.ParseCommandLine(argc, argv)) {
    if (options.help) {
      if (2 < argc) {
        Error() << "/help option should not be used with others";
      } else {
        PrintHelp(argv[0]);
        return 0;
      }
    }
    if (options.version) {
      if (2 < argc) {
        Error() << "/version option should not be used with others";
      } else {
        PrintVersion();
        return 0;
      }
    }
  }

  if (!options.pid) {
    Error() << "/pid option is not provided";
  }

  if (Log::ErrorCount != 0) {
    std::cerr << "See `" << GetProgramName(argv[0]) << " /help`";
    return Log::ErrorCount;
  }

  try {
    HeapStatistics statistics;
    if (HeapStatisticsGenerator::Run(options, statistics) &&
        (Log::ErrorCount == 0 || !options.strict)) {
      std::cout << Format{statistics, options};
    }
  } catch (std::exception& exception) {
    Error() << exception.what();
    return -1;
  }

  return Log::ErrorCount;
}
