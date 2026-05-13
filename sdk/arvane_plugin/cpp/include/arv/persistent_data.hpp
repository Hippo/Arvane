#pragma once

#include "arv/api.h"
#include "arv/result.hpp"

namespace arv {

class PersistentData {
public:
  explicit constexpr PersistentData(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] Result unsupported() const {
    if (api_ == nullptr || api_->persistent_data == nullptr ||
        api_->persistent_data->unsupported == nullptr) {
      return Result::unsupported("persistent data API is unavailable");
    }
    return Result{api_->persistent_data->unsupported(api_->persistent_data_ctx)};
  }

private:
  const ArvAPI* api_{};
};

} // namespace arv
