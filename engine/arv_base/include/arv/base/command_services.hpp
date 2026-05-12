#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace arv::base {

struct OnlinePlayerCommandTarget {
  SessionID session_id{};
  PlayerID player_id{};
  WorldID world_id{};
  std::string display_name{};
  Vec3f position{};
};

using OnlinePlayerByIDFn =
    FunctionRef<std::optional<OnlinePlayerCommandTarget>(PlayerID)>;
using OnlinePlayerByNameFn =
    FunctionRef<std::optional<OnlinePlayerCommandTarget>(std::string_view)>;
using TeleportPlayerFn = FunctionRef<bool(PlayerID, Vec3f)>;

struct ServerCommandServices {
  OnlinePlayerByIDFn online_player_by_id{};
  OnlinePlayerByNameFn online_player_by_name{};
  TeleportPlayerFn teleport_player{};
};

} // namespace arv::base
