#include "arv/server/player_movement_validation.hpp"

#include <algorithm>
#include <cmath>

namespace arv::server {
namespace {

[[nodiscard]] bool finite_packet(const protocol::ServerboundPlayerMove& packet) noexcept {
  return finite(packet.position) && finite(packet.velocity) &&
         std::isfinite(packet.yaw_radians) && std::isfinite(packet.pitch_radians);
}

[[nodiscard]] bool outside_world_bounds(
    Vec3f position,
    const MovementValidationContext& context) noexcept {
  return std::abs(position.x) > context.max_abs_coordinate ||
         std::abs(position.z) > context.max_abs_coordinate ||
         position.y < context.min_world_y ||
         position.y > context.max_world_y;
}

[[nodiscard]] double validation_elapsed_seconds(
    const AcceptedPlayerMovementState& previous,
    const MovementValidationContext& context,
    const io::MovementValidationConfig& config) noexcept {
  const double min_elapsed = 1.0 / static_cast<double>(std::max(config.send_rate_hz, 1.0F));
  if (!previous.has_accepted_move) {
    return min_elapsed;
  }
  const double raw_elapsed = context.receive_time_seconds - previous.last_receive_time_seconds;
  return std::clamp(
      raw_elapsed,
      min_elapsed,
      static_cast<double>(std::max(config.max_validation_window_seconds, 0.001F)));
}

[[nodiscard]] f32 selected_horizontal_speed(
    const protocol::ServerboundPlayerMove& packet,
    const MovementValidationContext& context,
    const io::MovementValidationConfig& config) noexcept {
  if (context.void_space_movement) {
    return config.max_fly_speed_blocks_per_second;
  }
  if (context.can_fly &&
      protocol::has_move_flag(packet.flags, protocol::PlayerMoveFlags::Flying)) {
    return config.max_fly_speed_blocks_per_second;
  }
  if (protocol::has_move_flag(packet.flags, protocol::PlayerMoveFlags::Swimming)) {
    return config.max_swim_speed_blocks_per_second;
  }
  if (!protocol::has_move_flag(packet.flags, protocol::PlayerMoveFlags::OnGround)) {
    return config.max_air_speed_blocks_per_second;
  }
  return config.max_ground_speed_blocks_per_second;
}

[[nodiscard]] AcceptedPlayerMovementState accepted_state_from_packet(
    const protocol::ServerboundPlayerMove& packet,
    const AcceptedPlayerMovementState& previous,
    const MovementValidationContext& context,
    double elapsed) noexcept {
  Vec3f derived_velocity = packet.velocity;
  if (previous.has_accepted_move && elapsed > 0.0) {
    derived_velocity = (packet.position - previous.position) *
                       (1.0F / static_cast<f32>(elapsed));
  }
  return {
      packet.position,
      derived_velocity,
      packet.yaw_radians,
      packet.pitch_radians,
      packet.flags,
      packet.sequence,
      context.server_tick,
      context.receive_time_seconds,
      true};
}

[[nodiscard]] MovementValidationResult reject(
    const AcceptedPlayerMovementState& previous,
    protocol::PlayerMoveCorrectionReason reason) noexcept {
  return {false, true, reason, previous};
}

} // namespace

bool movement_sequence_is_newer(u32 sequence, u32 previous) noexcept {
  return sequence != previous &&
         static_cast<i32>(sequence - previous) > 0;
}

MovementValidationResult validate_serverbound_player_move(
    const protocol::ServerboundPlayerMove& packet,
    const AcceptedPlayerMovementState& previous,
    const MovementValidationContext& context,
    const io::MovementValidationConfig& config) noexcept {
  if (!config.enabled) {
    const double elapsed = validation_elapsed_seconds(previous, context, config);
    return {
        true,
        false,
        protocol::PlayerMoveCorrectionReason::Unknown,
        accepted_state_from_packet(packet, previous, context, elapsed)};
  }

  if (!finite_packet(packet)) {
    return reject(previous, protocol::PlayerMoveCorrectionReason::InvalidNumber);
  }
  if (outside_world_bounds(packet.position, context)) {
    return reject(previous, protocol::PlayerMoveCorrectionReason::OutOfWorld);
  }
  if (previous.has_accepted_move &&
      !movement_sequence_is_newer(packet.sequence, previous.last_sequence)) {
    return reject(previous, protocol::PlayerMoveCorrectionReason::StalePacket);
  }
  if (protocol::has_move_flag(packet.flags, protocol::PlayerMoveFlags::Flying) &&
      !context.can_fly &&
      !context.void_space_movement) {
    return reject(previous, protocol::PlayerMoveCorrectionReason::PermissionDenied);
  }
  const bool near_previous_position =
      previous.has_accepted_move &&
      distance(packet.position, previous.position) <= config.base_position_epsilon_blocks;
  if (config.reject_unloaded_chunk_movement && !context.void_space_movement &&
      !context.target_chunk_loaded &&
      !near_previous_position) {
    return reject(previous, protocol::PlayerMoveCorrectionReason::UnloadedChunk);
  }
  if (config.solid_collision_check && context.has_collision && context.final_aabb_overlaps_solid) {
    return reject(previous, protocol::PlayerMoveCorrectionReason::Collision);
  }

  const double elapsed = validation_elapsed_seconds(previous, context, config);
  if (previous.has_accepted_move) {
    const f32 horizontal = arv::horizontal_distance(packet.position, previous.position);
    const f32 vertical = std::abs(packet.position.y - previous.position.y);
    const f32 allowed_horizontal =
        selected_horizontal_speed(packet, context, config) * static_cast<f32>(elapsed) +
        config.base_position_epsilon_blocks + config.lag_grace_blocks;
    const f32 vertical_speed_limit =
        context.void_space_movement
            ? std::max(
                  config.max_vertical_speed_blocks_per_second,
                  config.max_fly_speed_blocks_per_second)
            : config.max_vertical_speed_blocks_per_second;
    const f32 allowed_vertical =
        vertical_speed_limit * static_cast<f32>(elapsed) +
        config.base_position_epsilon_blocks + config.lag_grace_blocks;
    f32 server_horizontal_allowance = 0.0F;
    f32 server_vertical_allowance = 0.0F;
    if (context.knockback_control_suppression_ticks > 0U) {
      server_horizontal_allowance =
          horizontal_length(context.server_velocity) * static_cast<f32>(elapsed);
      server_vertical_allowance =
          std::abs(context.server_velocity.y) * static_cast<f32>(elapsed);
    }
    if (horizontal > allowed_horizontal + server_horizontal_allowance ||
        vertical > allowed_vertical + server_vertical_allowance) {
      return reject(previous, protocol::PlayerMoveCorrectionReason::TooFast);
    }
    if (distance(packet.position, previous.position) >
        config.max_single_packet_distance_blocks) {
      return reject(previous, protocol::PlayerMoveCorrectionReason::TooFar);
    }
  }

  return {
      true,
      false,
      protocol::PlayerMoveCorrectionReason::Unknown,
      accepted_state_from_packet(packet, previous, context, elapsed)};
}

} // namespace arv::server
