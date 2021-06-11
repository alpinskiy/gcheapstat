#pragma once
#include <codecvt>
#include <locale>

class LogLine final {
 public:
  LogLine(LogLine&& other) noexcept : visible_{other.visible_} {
    if (visible_) {
      other.visible_ = false;
    }
  }

  ~LogLine() {
    if (visible_) {
      std::cerr << std::endl;
    }
  }

  LogLine(const LogLine& other) = delete;
  LogLine& operator=(const LogLine&) = delete;
  LogLine& operator=(LogLine&&) = delete;

  template <typename T>
  LogLine& operator<<(T&& t) {
    if (visible_) {
      std::cerr << std::forward<T>(t);
    }
    return *this;
  }

 private:
  explicit LogLine(bool visible) : visible_{visible} {}

  bool visible_;
  friend struct Log;
};

struct Log final {
  static int Level;
  static int ErrorCount;

  static LogLine Error() {
    ++ErrorCount;
    return LogLine{true};
  }

  static LogLine Debug() { return LogLine{1 <= Level}; }
};

inline LogLine Error() { return Log::Error(); }
inline LogLine Debug() { return Log::Debug(); }

inline std::ostream& operator<<(std::ostream& out, const std::wstring& str) {
  out << std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>{}.to_bytes(str);
  return out;
}
