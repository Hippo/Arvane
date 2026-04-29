#include "arv/core/log.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>

namespace {

[[nodiscard]] spdlog::level::level_enum to_spdlog_level(arv::core::LogLevel level) noexcept {
  switch (level) {
    case arv::core::LogLevel::Trace:
      return spdlog::level::trace;
    case arv::core::LogLevel::Debug:
      return spdlog::level::debug;
    case arv::core::LogLevel::Info:
      return spdlog::level::info;
    case arv::core::LogLevel::Warning:
      return spdlog::level::warn;
    case arv::core::LogLevel::Error:
      return spdlog::level::err;
  }
  return spdlog::level::info;
}

[[nodiscard]] std::shared_ptr<spdlog::logger> make_global_logger() {
  if (auto existing = spdlog::get("arvane")) {
    return existing;
  }

  auto logger = spdlog::stdout_color_mt("arvane");
  logger->set_pattern("[%^%l%$] %v");
  logger->set_level(spdlog::level::trace);
  logger->flush_on(spdlog::level::warn);
  return logger;
}

[[nodiscard]] spdlog::logger& global_logger() {
  static const std::shared_ptr<spdlog::logger> logger = make_global_logger();
  return *logger;
}

} // namespace

namespace arv::core {

void set_log_level(LogLevel level) {
  global_logger().set_level(to_spdlog_level(level));
}

void log(LogLevel level, std::string_view subsystem, std::string_view message) {
  global_logger().log(to_spdlog_level(level), "[{}] {}", subsystem, message);
}

void trace(std::string_view subsystem, std::string_view message) {
  log(LogLevel::Trace, subsystem, message);
}

void debug(std::string_view subsystem, std::string_view message) {
  log(LogLevel::Debug, subsystem, message);
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

} // namespace arv::core
