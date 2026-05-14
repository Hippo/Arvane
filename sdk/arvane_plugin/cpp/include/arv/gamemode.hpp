#pragma once

#include "arv/api.h"
#include "arv/permissions.hpp"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace arv {

using GameModeDefinition = ArvGameModeDefinition;
using GameModeInfo = ArvGameModeInfo;

class GameModes {
public:
  explicit GameModes(const ArvAPI* api) noexcept;

  [[nodiscard]] Result register_mode(const GameModeDefinition& definition) const;

  [[nodiscard]] bool exists(std::string_view mode_id) const;

  [[nodiscard]] ValueResult<GameModeInfo> info(std::string_view mode_id) const;

  [[nodiscard]] ValueResult<std::string_view> current(ArvPlayerID player) const;

  [[nodiscard]] Result set(
      ArvPlayerID player,
      std::string_view mode_id,
      std::string_view reason = {}) const;

  [[nodiscard]] ValueResult<ResolvedCorePermissions> base_core_permissions(
      std::string_view mode_id) const;

  [[nodiscard]] std::vector<GameModeInfo> list_modes() const;

private:
  const ArvAPI* api_{};
};

} // namespace arv
