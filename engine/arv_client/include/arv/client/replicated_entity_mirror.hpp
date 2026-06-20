#pragma once

#include "arv/core/time.hpp"
#include "arv/protocol/entity_updates.hpp"

#include <optional>
#include <string>
#include <vector>

namespace arv::client {

struct ReplicatedPassengerMirror {
  EntityNetID passenger_id{};
  std::string seat_id{};
  bool controls_vehicle{};
};

struct ReplicatedEntityMirror {
  EntityNetID entity_id{};
  EntityTypeID entity_type{};
  std::string entity_type_name{};
  WorldID world_id{};
  Vec3f previous_position{};
  Vec3f position{};
  Vec3f velocity{};
  Vec3f render_position{};
  Vec3f interpolation_start_position{};
  Quatf previous_rotation{};
  Quatf rotation{};
  Quatf render_rotation{};
  Quatf interpolation_start_rotation{};
  u64 last_server_tick{};
  u64 last_transform_change_server_tick{};
  Seconds interpolation_elapsed_seconds{};
  Seconds interpolation_duration_seconds{};
  std::optional<PlayerID> owning_player{};
  std::vector<protocol::EntityAnimationPlayback> animations{};
  Vec3f head_rotation_degrees{};
  bool has_health{};
  f32 health_current{};
  f32 health_max{};
  bool dead{};
  bool invulnerable{};
  std::optional<EntityNetID> parent_rideable{};
  std::string parent_seat_id{};
  bool controls_parent_vehicle{};
  std::vector<ReplicatedPassengerMirror> passengers{};
};

} // namespace arv::client
