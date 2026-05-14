#include "arv/metrics.hpp"

namespace arv {

Metrics::Metrics(const ArvAPI* api) noexcept : api_(api) {
}

void Metrics::counter_add(std::string_view metric_id, std::int64_t amount) const {
  if (api_ != nullptr && api_->metrics != nullptr &&
      api_->metrics->counter_add != nullptr) {
    api_->metrics->counter_add(api_->metrics_ctx, view(metric_id), amount);
  }
}

} // namespace arv
