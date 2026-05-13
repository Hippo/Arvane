#pragma once

#include "arv/api.h"
#include "arv/result.hpp"

namespace arv {

class Permissions {
public:
  explicit constexpr Permissions(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] Result unsupported() const {
    if (api_ == nullptr || api_->permissions == nullptr ||
        api_->permissions->unsupported == nullptr) {
      return Result::unsupported("permissions API is unavailable");
    }
    return Result{api_->permissions->unsupported(api_->permissions_ctx)};
  }

private:
  const ArvAPI* api_{};
};

} // namespace arv
