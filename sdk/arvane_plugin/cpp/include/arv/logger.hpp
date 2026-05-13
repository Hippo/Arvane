#pragma once

#include "arv/api.h"
#include "arv/types.hpp"

#include <string_view>

namespace arv {

class Logger {
public:
  explicit constexpr Logger(const ArvAPI* api) noexcept : api_(api) {
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
  const ArvAPI* api_{};
};

} // namespace arv
