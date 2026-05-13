#pragma once

#include "arvane/plugin/api.h"

#include <string_view>

namespace arv::plugin {

[[nodiscard]] constexpr ArvStringView view(std::string_view text) noexcept {
  return {text.data(), text.size()};
}

class Logger {
public:
  explicit constexpr Logger(const ArvPluginAPI* api) noexcept : api_(api) {
  }

  void trace(std::string_view message) const {
    if (api_ != nullptr && api_->logger != nullptr && api_->logger->trace != nullptr) {
      api_->logger->trace(api_->logger_ctx, view(message));
    }
  }

  void debug(std::string_view message) const {
    if (api_ != nullptr && api_->logger != nullptr && api_->logger->debug != nullptr) {
      api_->logger->debug(api_->logger_ctx, view(message));
    }
  }

  void info(std::string_view message) const {
    if (api_ != nullptr && api_->logger != nullptr && api_->logger->info != nullptr) {
      api_->logger->info(api_->logger_ctx, view(message));
    }
  }

  void warn(std::string_view message) const {
    if (api_ != nullptr && api_->logger != nullptr && api_->logger->warn != nullptr) {
      api_->logger->warn(api_->logger_ctx, view(message));
    }
  }

  void error(std::string_view message) const {
    if (api_ != nullptr && api_->logger != nullptr && api_->logger->error != nullptr) {
      api_->logger->error(api_->logger_ctx, view(message));
    }
  }

private:
  const ArvPluginAPI* api_{};
};

} // namespace arv::plugin
