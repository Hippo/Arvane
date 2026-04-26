#include "arv/core/log.hpp"

namespace arv::core {

Logger::Logger(std::ostream& stream) : stream_(&stream) {
}

void Logger::log(LogLevel level, std::string_view subsystem, std::string_view message) {
  std::lock_guard lock(mutex_);
  (*stream_) << '[' << to_string(level) << "] [" << subsystem << "] " << message << '\n';
}

void Logger::info(std::string_view subsystem, std::string_view message) {
  log(LogLevel::Info, subsystem, message);
}

void Logger::warn(std::string_view subsystem, std::string_view message) {
  log(LogLevel::Warning, subsystem, message);
}

void Logger::error(std::string_view subsystem, std::string_view message) {
  log(LogLevel::Error, subsystem, message);
}

} // namespace arv::core
