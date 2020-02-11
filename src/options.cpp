#include "options.h"

#include "version.h"

int Options::ParseCommandLine(PCWSTR cmdline) {
  int argc = 0;
  auto argv = CommandLineToArgvW(cmdline, &argc);
  if (argv == nullptr) return -1;
  std::unique_ptr<PWSTR, decltype(&LocalFree)> argv_scope_guard{
      CommandLineToArgvW(cmdline, &argc), LocalFree};
  int count = 0;
  for (auto i = 1; i < argc; ++i, ++count) {
    PWSTR val = nullptr;
    if (auto res = wcschr(argv[i], ':')) {
      auto next = res + 1;
      if (*next) val = next;
      *res = 0;
    }
    if (!wcsicmp(argv[i], L"/pipe")) {
      if (!val)
        // pipe name is missing
        return -1;
      pipename = val;
    } else if (!wcsicmp(argv[i], L"/pid") || !wcsicmp(argv[i], L"/p")) {
      if (!val || swscanf_s(val, L"%u", &pid) != 1)
        // pid is invalid or missing
        return -1;
    } else if (!wcsicmp(argv[i], L"/limit") || !wcsicmp(argv[i], L"/l")) {
      if (!val || swscanf_s(val, L"%zu", &limit) != 1)
        // limit is missing or invalid
        return -1;
    } else if (!wcsicmp(argv[i], L"/sort") || !wcsicmp(argv[i], L"/s")) {
      if (!val)
        // default sorting options apply
        continue;
      if (val[0] == '-') {
        order = Order::Desc;
        ++val;
        if (*val == 0)
          // just invert sort order
          continue;
      }
      if (auto res = wcschr(val, ':')) {
        auto next = res + 1;
        if (*next && swscanf_s(next, L"%u", &orderby_gen) != 1)
          // invalid generation number to sort on
          return -1;
        *res = 0;
      }
      if (!wcsicmp(val, L"size") || !wcsicmp(val, L"s"))
        orderby = OrderBy::TotalSize;
      else if (!wcsicmp(val, L"count") || !wcsicmp(val, L"c"))
        orderby = OrderBy::Count;
      else
        // invalid field name to sort on
        return -1;
    } else if (!wcsicmp(argv[i], L"/gen") || !wcsicmp(argv[i], L"/g")) {
      if (swscanf_s(val, L"%u", &gen) != 1)
        // invalid generation number to display
        return -1;
    } else if (!wcsicmp(argv[i], L"/help") || !wcsicmp(argv[i], L"/h") ||
               !wcsicmp(argv[i], L"/?"))
      help = true;
    else if (!wcsicmp(argv[i], L"/verbose") || !wcsicmp(argv[i], L"/V"))
      verbose = true;
    else if (!wcsicmp(argv[i], L"/runas") || !wcsicmp(argv[i], L"/as")) {
      if (!val)
        // account name is missing
        return -1;
      if (!wcsicmp(val, L"localsystem"))
        runaslocalsystem = true;
      else
        // unrecognized account name
        return -1;
    } else if (!wcsicmp(argv[i], L"/version") || !wcscmp(argv[i], L"/v")) {
      version = true;
    } else
      // invalid option
      return -1;
  }
  return count;
}

void PrintVersion() { printf("gcheapstat version " VERSION "\n"); }

void PrintUsage(FILE* file) {
  // clang-format off
  fprintf(file, ".NET GC heap statistics generator.\n\n");
  fprintf(file, "GCHEAPSTAT [/VERSION] [/HELP] [/VERBOSE] [/SORT:(SIZE|COUNT)[:gen]]\n");
  fprintf(file, "           [/LIMIT:count] [/GEN:gen] [/RUNAS:LOCALSYSTEM] /PID:pid\n");
  // clang-format on
}