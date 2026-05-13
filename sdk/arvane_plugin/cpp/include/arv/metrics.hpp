#pragma once

#include "arv/api.h"
#include "arv/types.hpp"

#include <cstdint>
#include <string_view>

namespace arv {

class Metrics {
public:
  explicit constexpr Metrics(const ArvAPI* api) noexcept : api_(api) {
  }

  void counter_add(std::string_view metric_id, std::int64_t amount) const {
    if (api_ != nullptr && api_->metrics != nullptr &&
        api_->metrics->counter_add != nullptr) {
      api_->metrics->counter_add(api_->metrics_ctx, view(metric_id), amount);
    }
  }

private:
  const ArvAPI* api_{};
};

} // namespace arv
