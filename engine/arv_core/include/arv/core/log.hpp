#pragma once

#include <spdlog/fmt/chrono.h>
#include <spdlog/fmt/compile.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/fmt/ranges.h>
#include <spdlog/fmt/std.h>

#include <string>
#include <string_view>
#include <utility>

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

void set_log_level(LogLevel level);

[[nodiscard]] bool should_log(LogLevel level) noexcept;

void log(LogLevel level, std::string_view subsystem, std::string_view message);

template<typename... Args>
void log(
    LogLevel level,
    std::string_view subsystem,
    fmt::format_string<Args...> format,
    Args&&... args) {
  if (!should_log(level)) {
    return;
  }
  log(level, subsystem, fmt::format(format, std::forward<Args>(args)...));
}

template<typename... Args>
void log_runtime(
    LogLevel level,
    std::string_view subsystem,
    std::string_view format,
    Args&&... args) {
  if (!should_log(level)) {
    return;
  }
  log(
      level,
      subsystem,
      fmt::vformat(
          fmt::string_view(format.data(), format.size()),
          fmt::make_format_args(args...)));
}

void trace(std::string_view subsystem, std::string_view message);

template<typename... Args>
void trace(std::string_view subsystem, fmt::format_string<Args...> format, Args&&... args) {
  log(LogLevel::Trace, subsystem, format, std::forward<Args>(args)...);
}

template<typename... Args>
void trace_runtime(std::string_view subsystem, std::string_view format, Args&&... args) {
  log_runtime(LogLevel::Trace, subsystem, format, std::forward<Args>(args)...);
}

void debug(std::string_view subsystem, std::string_view message);

template<typename... Args>
void debug(std::string_view subsystem, fmt::format_string<Args...> format, Args&&... args) {
  log(LogLevel::Debug, subsystem, format, std::forward<Args>(args)...);
}

template<typename... Args>
void debug_runtime(std::string_view subsystem, std::string_view format, Args&&... args) {
  log_runtime(LogLevel::Debug, subsystem, format, std::forward<Args>(args)...);
}

void info(std::string_view subsystem, std::string_view message);

template<typename... Args>
void info(std::string_view subsystem, fmt::format_string<Args...> format, Args&&... args) {
  log(LogLevel::Info, subsystem, format, std::forward<Args>(args)...);
}

template<typename... Args>
void info_runtime(std::string_view subsystem, std::string_view format, Args&&... args) {
  log_runtime(LogLevel::Info, subsystem, format, std::forward<Args>(args)...);
}

void warn(std::string_view subsystem, std::string_view message);

template<typename... Args>
void warn(std::string_view subsystem, fmt::format_string<Args...> format, Args&&... args) {
  log(LogLevel::Warning, subsystem, format, std::forward<Args>(args)...);
}

template<typename... Args>
void warn_runtime(std::string_view subsystem, std::string_view format, Args&&... args) {
  log_runtime(LogLevel::Warning, subsystem, format, std::forward<Args>(args)...);
}

void error(std::string_view subsystem, std::string_view message);

template<typename... Args>
void error(std::string_view subsystem, fmt::format_string<Args...> format, Args&&... args) {
  log(LogLevel::Error, subsystem, format, std::forward<Args>(args)...);
}

template<typename... Args>
void error_runtime(std::string_view subsystem, std::string_view format, Args&&... args) {
  log_runtime(LogLevel::Error, subsystem, format, std::forward<Args>(args)...);
}

} // namespace arv::core
