#pragma once

#include "arv/api.h"
#include "arv/types.hpp"

#include <cstdint>
#include <string_view>

namespace arv {

class Metrics {
public:
  explicit Metrics(const ArvAPI* api) noexcept;

  void counter_add(std::string_view metric_id, std::int64_t amount) const;

private:
  const ArvAPI* api_{};
};

} // namespace arv
