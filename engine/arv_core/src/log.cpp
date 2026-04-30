#include "arv/core/log.hpp"

#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <memory>

namespace {

constexpr arv::core::LogLevel MinCompiledLogLevel{
#ifdef NDEBUG
    arv::core::LogLevel::Info
#else
    arv::core::LogLevel::Trace
#endif
};

std::atomic<arv::core::LogLevel> g_log_level{
#ifdef NDEBUG
    arv::core::LogLevel::Info
#else
    arv::core::LogLevel::Trace
#endif
};

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

  constexpr std::size_t QueueSize = 8192;
  constexpr std::size_t WorkerThreads = 1;
  spdlog::init_thread_pool(QueueSize, WorkerThreads);
  auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_st>();
  auto logger = std::make_shared<spdlog::async_logger>(
      "arvane",
      std::move(sink),
      spdlog::thread_pool(),
      spdlog::async_overflow_policy::overrun_oldest);
  spdlog::register_logger(logger);
  logger->set_pattern("[%^%l%$] %v");
  logger->set_level(to_spdlog_level(g_log_level.load(std::memory_order_relaxed)));
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
  g_log_level.store(level, std::memory_order_relaxed);
  global_logger().set_level(to_spdlog_level(level));
}

bool should_log(LogLevel level) noexcept {
  return static_cast<int>(level) >= static_cast<int>(MinCompiledLogLevel) &&
         static_cast<int>(level) >=
             static_cast<int>(g_log_level.load(std::memory_order_relaxed));
}

void log(LogLevel level, std::string_view subsystem, std::string_view message) {
  if (!should_log(level)) {
    return;
  }
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
