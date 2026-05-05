#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/math.hpp"
#include "arv/server/session_registry.hpp"
#include "arv/world/world_runtime.hpp"

#include <optional>
#include <string_view>

namespace arv::server {

struct OnlinePlayerCommandTarget {
  SessionID session_id{};
  PlayerID player_id{};
  WorldID world_id{};
  std::string_view display_name{};
  Vec3f position{};
};

struct ServerCommandServices {
  SessionRegistry* sessions{};
  world::WorldRuntime* world{};

  [[nodiscard]] std::optional<OnlinePlayerCommandTarget> online_player_by_id(
      PlayerID player_id) const;
  [[nodiscard]] std::optional<OnlinePlayerCommandTarget> online_player_by_name(
      std::string_view display_name) const;
  [[nodiscard]] bool teleport_player(PlayerID player_id, Vec3f position) const;
};

} // namespace arv::server
