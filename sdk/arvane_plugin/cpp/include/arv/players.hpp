#pragma once

#include "arv/api.h"
#include "arv/result.hpp"

#include <optional>

namespace arv {

struct PlayerSelection {
  bool has_world{};
  ArvWorldHandle world{};
  std::optional<ArvBlockPos> a{};
  std::optional<ArvBlockPos> b{};
};

class Players {
public:
  explicit Players(const ArvAPI* api) noexcept;

  [[nodiscard]] ValueResult<PlayerSelection> current_selection(ArvPlayerID player) const;

private:
  const ArvAPI* api_{};
};

} // namespace arv
