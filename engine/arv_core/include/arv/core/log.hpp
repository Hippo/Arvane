#pragma once

#include <iostream>
#include <mutex>
#include <string_view>

namespace arv::core {

enum class LogLevel {
  Trace,
  Debug,
  Info,
  Warning,
  Error,
};

[[nodiscard]] constexpr std::string_view to_string(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::Trace:
      return "trace";
    case LogLevel::Debug:
      return "debug";
    case LogLevel::Info:
      return "info";
    case LogLevel::Warning:
      return "warning";
    case LogLevel::Error:
      return "error";
  }
  return "unknown";
}

class Logger {
public:
  explicit Logger(std::ostream& stream = std::clog);

  void log(LogLevel level, std::string_view subsystem, std::string_view message);

  void info(std::string_view subsystem, std::string_view message);

  void warn(std::string_view subsystem, std::string_view message);

  void error(std::string_view subsystem, std::string_view message);

private:
  std::ostream* stream_{};
  std::mutex mutex_;
};

} // namespace arv::core
