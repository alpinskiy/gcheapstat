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
    if (!wcscmp(argv[i], L"-pipe")) {
      if (!val) return -1;  // pipe name is missing
      pipename = val;
    } else if (!wcscmp(argv[i], L"-pid") || !wcscmp(argv[i], L"-p")) {
      if (!val || swscanf_s(val, L"%u", &pid) != 1)
        return -1;  // pid is missing or invalid
    } else if (!wcscmp(argv[i], L"-limit") || !wcscmp(argv[i], L"-l")) {
      if (!val || swscanf_s(val, L"%zu", &limit) != 1)
        return -1;  // limit is missing or invalid
    } else if (!wcscmp(argv[i], L"-sort") || !wcscmp(argv[i], L"-s")) {
      if (!val) continue;  // default sorting options apply
      if (val[0] == '-') {
        order = Order::Desc;
        ++val;
        if (*val == 0) continue;  // reverse sort order only
      }
      if (auto res = wcschr(val, ':')) {
        auto next = res + 1;
        if (*next && swscanf_s(next, L"%u", &orderby_gen) != 1)
          return -1;  // invalid generation number to sort on
        *res = 0;
      }
      if (!wcscmp(val, L"size") || !wcscmp(val, L"s"))
        orderby = OrderBy::TotalSize;
      else if (!wcscmp(val, L"count") || !wcscmp(val, L"c"))
        orderby = OrderBy::Count;
      else
        return -1;  // invalid field name to sort on
    } else if (!wcscmp(argv[i], L"-gen") || !wcscmp(argv[i], L"-g")) {
      if (swscanf_s(val, L"%u", &gen) != 1)
        return -1;  // invalid generation number to display
    } else if (!wcscmp(argv[i], L"-help") || !wcscmp(argv[i], L"-h"))
      help = true;
    else if (!wcscmp(argv[i], L"-verbose") || !wcscmp(argv[i], L"-V"))
      verbose = true;
    else if (!wcscmp(argv[i], L"-runas") || !wcscmp(argv[i], L"-as")) {
      if (!val) return -1;  // account name is missing
      if (!wcscmp(val, L"localsystem"))
        runaslocalsystem = true;
      else
        return -1;  // unrecognized account name
    } else if (!wcscmp(argv[i], L"-version") || !wcscmp(argv[i], L"-v")) {
      version = true;
    } else
      return -1;  // invalid option
  }
  return count;
}

void PrintVersion() { printf("gcheapstat version " VERSION "\n"); }

void PrintUsage(FILE* file) {
  // clang-format off
  fprintf(file, ".NET GC heap statistics generator.\n\n");
  fprintf(file, "gcheapstat [-version] [-help] [-verbose] [-sort:(size|count)[:<gen>]]\n");
  fprintf(file, "           [-limit:<count>] [-gen:<gen>] [-runas:localsystem] -pid:<pid>\n");
  // clang-format on
}