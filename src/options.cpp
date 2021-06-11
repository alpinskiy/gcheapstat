#include "options.h"

#include <cstring>

#include "version.h"

#ifdef _MSC_VER
#include <string.h>
#define strcasecmp _stricmp
#define sscanf sscanf_s
#endif

namespace {

#ifdef _MSC_VER
inline bool DirectorySeparatorChar(char ch) { return ch == '/' || ch == '\\'; }
#else
inline bool DirectorySeparatorChar(char ch) { return ch == '/'; }
#endif

}  // namespace

bool Options::ParseCommandLine(int argc, char* argv[]) {
  // Parse command line
  auto i = 1;
  for (; i < argc; ++i) {
    char* val = nullptr;
    if (auto res = strchr(argv[i], ':')) {
      auto next = res + 1;
      if (*next) val = next;
      *res = 0;
    }
    if (!strcasecmp(argv[i], "/pid") || !strcasecmp(argv[i], "/p")) {
      if (!val || sscanf(val, "%d", &pid) != 1) {
        Error() << "Invalid or missing value for /pid option";
        break;
      }
    } else if (!strcasecmp(argv[i], "/limit") || !strcasecmp(argv[i], "/l")) {
      if (!val || sscanf(val, "%zu", &limit) != 1) {
        Error() << "Invalid or missing value for /limit option";
        break;
      }
    } else if (!strcasecmp(argv[i], "/sort") || !strcasecmp(argv[i], "/s")) {
      if (!val)
        // default sorting options apply
        continue;
      if (val[0] == '-' || val[0] == '+') {
        order = val[0] == '+' ? Order::Asc : Order::Desc;
        ++val;
        if (*val == 0)
          // just sort order
          continue;
      }
      if (auto res = strchr(val, ':')) {
        auto next = res + 1;
        if (*next && sscanf(next, "%d", &orderby_gen) != 1) {
          Error() << "Invalid generation number for /sort option";
          break;
        }
        *res = 0;
      }
      if (!strcasecmp(val, "size") || !strcasecmp(val, "s"))
        orderby = OrderBy::TotalSize;
      else if (!strcasecmp(val, "count") || !strcasecmp(val, "c"))
        orderby = OrderBy::Count;
      else {
        Error() << "Invalid column name for /sort option";
        break;
      }
    } else if (!strcasecmp(argv[i], "/statistics") ||
               !strcasecmp(argv[i], "/g")) {
      if (!val || sscanf(val, "%d", &gen) != 1) {
        Error() << "Invalid or missing value for /statistics option";
        break;
      }
    } else if (!strcasecmp(argv[i], "/help") || !strcasecmp(argv[i], "/h") ||
               !strcasecmp(argv[i], "/?"))
      help = true;
    else if (!strcasecmp(argv[i], "/verbose") || !strcmp(argv[i], "/V")) {
      if (!val || !strcasecmp(val, "yes") || !strcasecmp(val, "y"))
        verbose = true;
      else if (strcasecmp(val, "no") || !strcasecmp(val, "n")) {
        Error() << "Invalid value for /verbose option";
        break;
      }
    } else if (!strcasecmp(argv[i], "/json")) {
      json = true;
      if (val && sscanf(val, "%d", &json_indent) != 1) {
        Error() << "Invalid indentation value for /json option";
        break;
      }
    } else if (!strcasecmp(argv[i], "/strict")) {
      strict = true;
    } else if (!strcasecmp(argv[i], "/version") || !strcmp(argv[i], "/v")) {
      version = true;
    } else {
      Error() << "`" << argv[i] << "` is not an option";
      break;
    }
  }

  return i == argc;
}

void PrintHelp(char* argv0) {
  auto pname = GetProgramName(argv0);
  std::cout << DESCRIPTION "\n\n";
  std::cout << "Usage:\n";
  // clang-format off
  std::cout << pname << " [/version] [/help] [/verbose] [/sort:{+|-}{size|count}[:gen]]\n";
  for (auto _ : pname) std::cout << " ";
  std::cout << " [/limit:n] [/statistics:n] [/format:text|json] /pid:n\n\n";
  std::cout << "  help     Display usage information\n";
  std::cout << "  verbose  Display warnings. Only errors are displayed by default\n";
  std::cout << "  sort     Sort output by either total size or count, ascending '+' or\n";
  std::cout << "           descending '-'. You can also specify generation to sort on (refer to\n";
  std::cout << "           `statistics` option description)\n";
  std::cout << "  limit    Limit the number of rows to output\n";
  std::cout << "  statistics      Count only objects of the generation specified. Valid values are\n";
  std::cout << "           0 to 2 (first, second the third generations respectevely) and 3 for\n";
  std::cout << "           Large Object Heap. The same is for statistics parameter of `sort` option\n";
  std::cout << "  pid      Target process ID\n\n";
  std::cout << "Zero status code on success, non-zero otherwise\n";
  // clang-format on
}

void PrintVersion() { std::cout << PRODUCTNAME " " VERSION_STR "\n"; }

std::string GetProgramName(char* argv0) {
  for (auto p = argv0; *p != 0; ++p) {
    if (DirectorySeparatorChar(*p)) {
      argv0 = p + 1;
    }
  }
#ifdef _MSC_VER
  // Trim ".exe"
  auto len = strlen(argv0);
  if (4 < len && strcmp(argv0 + len - 4, ".exe") == 0) {
    len -= 4;
  }
  return {argv0, len};
#else
  return argv0;
#endif
}
