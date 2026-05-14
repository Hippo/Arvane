#include "arv/logger.hpp"

namespace arv {

Logger::Logger(const ArvAPI* api) noexcept : api_(api) {
}

void Logger::trace(std::string_view message) const {
  if (api_ != nullptr && api_->logger != nullptr && api_->logger->trace != nullptr) {
    api_->logger->trace(api_->logger_ctx, view(message));
  }
}

void Logger::debug(std::string_view message) const {
  if (api_ != nullptr && api_->logger != nullptr && api_->logger->debug != nullptr) {
    api_->logger->debug(api_->logger_ctx, view(message));
  }
}

void Logger::info(std::string_view message) const {
  if (api_ != nullptr && api_->logger != nullptr && api_->logger->info != nullptr) {
    api_->logger->info(api_->logger_ctx, view(message));
  }
}

void Logger::warn(std::string_view message) const {
  if (api_ != nullptr && api_->logger != nullptr && api_->logger->warn != nullptr) {
    api_->logger->warn(api_->logger_ctx, view(message));
  }
}

void Logger::error(std::string_view message) const {
  if (api_ != nullptr && api_->logger != nullptr && api_->logger->error != nullptr) {
    api_->logger->error(api_->logger_ctx, view(message));
  }
}

} // namespace arv
