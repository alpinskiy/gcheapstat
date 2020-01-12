#include "options.h"

bool Options::ParseCommandLine(PCWSTR cmdline) {
  int argc = 0;
  auto argv = CommandLineToArgvW(cmdline, &argc);
  if (argv == nullptr) return false;
  std::unique_ptr<PWSTR, decltype(&LocalFree)> argv_scope_guard{
      CommandLineToArgvW(cmdline, &argc), LocalFree};
  for (auto i = 1; i < argc; ++i) {
    auto val = static_cast<PWSTR>(nullptr);
    if (auto res = wcschr(argv[i], '=')) {
      auto next = res + 1;
      if (*next) val = next;
      *res = 0;
    }
    if (!wcscmp(argv[i], L"--pipe")) {
      if (!val) return false;  // pipe name is missing
      pipename = val;
    } else if (!wcscmp(argv[i], L"--pid")) {
      if (!val || swscanf_s(val, L"%u", &pid) != 1)
        return false;  // pid is missing or invalid
    } else if (!wcscmp(argv[i], L"--limit")) {
      if (!val || swscanf_s(val, L"%zu", &limit) != 1)
        return false;  // limit is missing or invalid
    } else if (!wcscmp(argv[i], L"--sort")) {
      if (!val) continue;  // default sorting options apply
      if (val[0] == '-') {
        order = Order::Desc;
        ++val;
        if (*val == 0) continue;  // reverse sort order only
      }
      if (!wcscmp(val, L"size")) {
        orderby = OrderBy::TotalSize;
      } else if (!wcscmp(val, L"count")) {
        orderby = OrderBy::Count;
      } else {
        return false;  // invalid field name to sort on
      }
    } else if (!wcscmp(argv[i], L"--help")) {
      help = true;
    } else {
      return false;  // invalid option
    }
  }
  return true;
}

void PrintUsage(FILE* file) {
  fprintf(file,
          "usage: gcheapstat --pid=<pid> [--sort=(size|count)] "
          "[--limit=<count>]\n");
}