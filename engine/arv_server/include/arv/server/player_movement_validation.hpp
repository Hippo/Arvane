#pragma once

#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/io/config.hpp"
#include "arv/protocol/player_input.hpp"

namespace arv::server {

struct AcceptedPlayerMovementState {
  Vec3f position{};
  Vec3f velocity{};
  f32 yaw_radians{};
  f32 pitch_radians{};
  protocol::PlayerMoveFlags flags{protocol::PlayerMoveFlags::None};
  u32 last_sequence{};
  TickIndex last_server_tick{};
  double last_receive_time_seconds{};
  bool has_accepted_move{};
};

struct MovementValidationContext {
  double receive_time_seconds{};
  TickIndex server_tick{};
  bool target_chunk_loaded{true};
  bool final_aabb_overlaps_solid{};
  bool can_fly{};
  bool has_collision{true};
  bool void_space_movement{};
  Vec3f server_velocity{};
  u32 knockback_control_suppression_ticks{};
  f32 min_world_y{-4096.0F};
  f32 max_world_y{4096.0F};
  f32 max_abs_coordinate{30000000.0F};
};

struct MovementValidationResult {
  bool accepted{};
  bool should_send_correction{};
  protocol::PlayerMoveCorrectionReason reason{protocol::PlayerMoveCorrectionReason::Unknown};
  AcceptedPlayerMovementState accepted_state{};
};

[[nodiscard]] bool movement_sequence_is_newer(u32 sequence, u32 previous) noexcept;

[[nodiscard]] MovementValidationResult validate_serverbound_player_move(
    const protocol::ServerboundPlayerMove& packet,
    const AcceptedPlayerMovementState& previous,
    const MovementValidationContext& context,
    const io::MovementValidationConfig& config) noexcept;

} // namespace arv::server
