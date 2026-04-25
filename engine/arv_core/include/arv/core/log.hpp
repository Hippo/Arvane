#pragma once

#include <chrono>
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
  explicit Logger(std::ostream& stream = std::clog) : stream_(&stream) {}

  void log(LogLevel level, std::string_view subsystem, std::string_view message) {
    std::lock_guard lock(mutex_);
    (*stream_) << '[' << to_string(level) << "] [" << subsystem << "] " << message << '\n';
  }

  void info(std::string_view subsystem, std::string_view message) {
    log(LogLevel::Info, subsystem, message);
  }

  void warn(std::string_view subsystem, std::string_view message) {
    log(LogLevel::Warning, subsystem, message);
  }

  void error(std::string_view subsystem, std::string_view message) {
    log(LogLevel::Error, subsystem, message);
  }

private:
  std::ostream* stream_{};
  std::mutex mutex_;
};

} // namespace arv::core
