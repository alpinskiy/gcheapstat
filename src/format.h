#pragma once
#include <nlohmann/json.hpp>

#include "statistics.h"

class Format final {
 public:
  Format(const HeapStatistics& statistics, const Options& options)
      : statistics_{statistics}, options_{options} {}

 private:
  void Print(std::ostream& out) const {
    if (options_.json) {
      PrintJsonFormat(out);
    } else {
      PrintWinDbgFormat(out);
    }
  }

  void PrintWinDbgFormat(std::ostream& out) const {
    auto first = statistics_.details.cbegin();
    auto last = statistics_.details.cend();
#ifdef _WIN64
    out << "              MT    Count    TotalSize Class Name\n";
#else
    out << "      MT    Count    TotalSize Class Name\n";
#endif
    std::cout << std::right;
    for (auto it = first; it != last; ++it) {
      auto count = it->statistics.count[options_.gen];
      auto size = it->statistics.size_total[options_.gen];
      if (!count && !size) {
        continue;
      }
      out << std::hex << std::setfill('0');
#ifdef _WIN64
      out << std::setw(16);
#else
      out << std::setw(8);
#endif
      out << it->method_table_address;
      out << std::dec << std::setfill(' ');
      out << std::setw(9) << count;
      out << std::setw(13) << size;
      out << ' ' << it->name << std::endl;
    }
    out << "Total " << statistics_.count[DAC_NUMBERGENERATIONS] << " objects"
        << std::endl;
    out << "Total size " << statistics_.size_total[DAC_NUMBERGENERATIONS]
        << " bytes" << std::endl;
  }

  void PrintJsonFormat(std::ostream& out) const {
    nlohmann::json details{};
    for (auto& item : statistics_.details) {
      details.push_back({
          {"name", item.name},
          {"count", item.statistics.count},
          {"size_total", item.statistics.size_total},
      });
    }
    nlohmann::json j{{"count", statistics_.count},
                     {"size_total", statistics_.size_total},
                     {"details", details}};
    out << j.dump(options_.json_indent) << std::endl;
  }

  const HeapStatistics& statistics_;
  const Options& options_;
  friend std::ostream& operator<<(std::ostream& out, const Format& format);
};

inline std::ostream& operator<<(std::ostream& out, const Format& format) {
  format.Print(out);
  return out;
}
