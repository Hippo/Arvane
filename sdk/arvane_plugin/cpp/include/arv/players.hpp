#pragma once

#include "arv/api.h"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <optional>
#include <string>
#include <vector>

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
  [[nodiscard]] std::vector<ArvPlayerID> online() const;
  [[nodiscard]] std::string profile_id(ArvPlayerID player) const;
  [[nodiscard]] std::string display_name(ArvPlayerID player) const;
  [[nodiscard]] std::optional<ArvPlayerID> find_by_profile_id(
      std::string_view profile_id) const;

private:
  const ArvAPI* api_{};
};

} // namespace arv
