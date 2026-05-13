#pragma once

#include "arv/api.h"
#include "arv/result.hpp"

namespace arv {

class Network {
public:
  explicit constexpr Network(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] Result unsupported() const {
    if (api_ == nullptr || api_->network == nullptr ||
        api_->network->unsupported == nullptr) {
      return Result::unsupported("network API is unavailable");
    }
    return Result{api_->network->unsupported(api_->network_ctx)};
  }

private:
  const ArvAPI* api_{};
};

} // namespace arv
