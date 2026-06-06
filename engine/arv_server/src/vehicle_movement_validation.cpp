#include "arv/server/vehicle_movement_validation.hpp"

#include <algorithm>
#include <cmath>

namespace arv::server {
namespace {

inline constexpr f32 MinVehicleValidationDtSeconds = 1.0F / 60.0F;
inline constexpr f32 MaxVehicleValidationDtSeconds = 0.25F;
inline constexpr f32 VehiclePositionEpsilonBlocks = 0.15F;
inline constexpr f32 VehicleVelocityEpsilonBlocksPerSecond = 0.5F;
inline constexpr f32 VehicleAccelerationEpsilonBlocksPerSecond = 2.0F;
inline constexpr f32 VehicleAngularSpeedEpsilonRadiansPerSecond = 0.25F;
inline constexpr f32 VehicleImpactStopSpeedEpsilonBlocksPerSecond = 0.25F;

[[nodiscard]] bool finite_quat(Quatf value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] bool finite_packet(const protocol::ServerboundVehicleMove& packet) noexcept {
  return finite(packet.position) && finite(packet.velocity) &&
         finite_quat(packet.rotation) && finite(packet.angular_velocity) &&
         std::isfinite(packet.dt_seconds) &&
         std::isfinite(packet.throttle) &&
         std::isfinite(packet.steer);
}

[[nodiscard]] f32 quat_dot(Quatf lhs, Quatf rhs) noexcept {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
}

[[nodiscard]] f32 rotation_delta_radians(Quatf from, Quatf to) noexcept {
  const Quatf normalized_from = normalize_quat(from);
  const Quatf normalized_to = normalize_quat(to);
  const f32 dot_value =
      std::clamp(std::abs(quat_dot(normalized_from, normalized_to)), 0.0F, 1.0F);
  return 2.0F * std::acos(dot_value);
}

[[nodiscard]] f32 clamped_packet_dt(const protocol::ServerboundVehicleMove& packet) noexcept {
  return std::clamp(packet.dt_seconds, MinVehicleValidationDtSeconds, MaxVehicleValidationDtSeconds);
}

[[nodiscard]] f32 elapsed_seconds(
    const protocol::ServerboundVehicleMove& packet,
    const AcceptedVehicleMovementState& previous,
    const VehicleMovementValidationContext& context) noexcept {
  if (!previous.has_accepted_move) {
    return clamped_packet_dt(packet);
  }
  const double raw_elapsed = context.receive_time_seconds - previous.last_receive_time_seconds;
  if (std::isfinite(raw_elapsed) && raw_elapsed > 0.0) {
    return std::clamp(
        static_cast<f32>(raw_elapsed),
        MinVehicleValidationDtSeconds,
        MaxVehicleValidationDtSeconds);
  }
  return clamped_packet_dt(packet);
}

[[nodiscard]] AcceptedVehicleMovementState accepted_state_from_packet(
    const protocol::ServerboundVehicleMove& packet,
    const VehicleMovementValidationContext& context) noexcept {
  return {
      packet.position,
      packet.velocity,
      normalize_quat(packet.rotation),
      packet.angular_velocity,
      packet.flags,
      packet.sequence,
      context.server_tick,
      context.receive_time_seconds,
      true};
}

[[nodiscard]] VehicleMovementValidationResult reject(
    const AcceptedVehicleMovementState& previous,
    protocol::VehicleCorrectionReason reason) noexcept {
  return {false, true, reason, previous};
}

[[nodiscard]] AcceptedVehicleMovementState stopped_state(
    const AcceptedVehicleMovementState& previous) noexcept {
  AcceptedVehicleMovementState stopped = previous;
  stopped.velocity = {};
  stopped.angular_velocity = {};
  return stopped;
}

[[nodiscard]] VehicleMovementValidationResult reject_collision(
    const AcceptedVehicleMovementState& state) noexcept {
  return {
      false,
      true,
      protocol::VehicleCorrectionReason::Collision,
      state};
}

} // namespace

bool vehicle_sequence_is_newer(u32 sequence, u32 previous) noexcept {
  return sequence != previous &&
         static_cast<i32>(sequence - previous) > 0;
}

VehicleMovementValidationResult validate_serverbound_vehicle_move(
    const protocol::ServerboundVehicleMove& packet,
    const AcceptedVehicleMovementState& previous,
    const VehicleMovementValidationContext& context,
    const entity::VehicleAuthorityDefinition& authority) noexcept {
  if (!finite_packet(packet) || packet.dt_seconds <= 0.0F) {
    return reject(previous, protocol::VehicleCorrectionReason::InvalidNumber);
  }
  if (!context.passenger_is_mounted) {
    return reject(previous, protocol::VehicleCorrectionReason::NotMounted);
  }
  if (!context.passenger_controls_vehicle) {
    return reject(previous, protocol::VehicleCorrectionReason::NotController);
  }
  if (previous.has_accepted_move &&
      !vehicle_sequence_is_newer(packet.sequence, previous.last_sequence)) {
    return reject(previous, protocol::VehicleCorrectionReason::StalePacket);
  }
  if (context.final_aabb_overlaps_solid) {
    return reject_collision(
        context.has_collision_resolved_state
            ? context.collision_resolved_state
            : stopped_state(previous));
  }
  if (authority.requires_ground &&
      protocol::has_vehicle_move_flag(packet.flags, protocol::VehicleMoveFlags::OnGround) &&
      !context.has_ground_support) {
    return reject(previous, protocol::VehicleCorrectionReason::MissingGroundSupport);
  }
  if (authority.requires_water &&
      context.water_support_samples < authority.min_water_support_samples) {
    return reject(previous, protocol::VehicleCorrectionReason::MissingWaterSupport);
  }

  const f32 horizontal_speed = horizontal_length(packet.velocity);
  const f32 vertical_speed = std::abs(packet.velocity.y);
  if (horizontal_speed > authority.max_speed + VehicleVelocityEpsilonBlocksPerSecond) {
    return reject(previous, protocol::VehicleCorrectionReason::TooFast);
  }
  if (vertical_speed > authority.max_vertical_speed + VehicleVelocityEpsilonBlocksPerSecond) {
    return reject(previous, protocol::VehicleCorrectionReason::TooMuchVerticalVelocity);
  }
  if (length(packet.angular_velocity) >
      authority.max_angular_speed + VehicleAngularSpeedEpsilonRadiansPerSecond) {
    return reject(previous, protocol::VehicleCorrectionReason::TooMuchAngularVelocity);
  }

  if (previous.has_accepted_move) {
    const f32 elapsed = elapsed_seconds(packet, previous, context);
    const f32 displacement = distance(packet.position, previous.position);
    const f32 allowed_distance =
        authority.max_speed * elapsed + VehiclePositionEpsilonBlocks;
    if (displacement > allowed_distance) {
      return reject(previous, protocol::VehicleCorrectionReason::TooFast);
    }

    const f32 previous_speed = horizontal_length(previous.velocity);
    const f32 current_speed = horizontal_length(packet.velocity);
    const f32 speed_delta = current_speed - previous_speed;
    const bool plausible_collision_deceleration =
        context.previous_velocity_sweep_hit_solid &&
        previous_speed > current_speed &&
        displacement <= previous_speed * elapsed + VehiclePositionEpsilonBlocks;
    const bool impact_stop =
        current_speed <= VehicleImpactStopSpeedEpsilonBlocksPerSecond &&
        previous_speed > current_speed &&
        displacement <= previous_speed * elapsed + VehiclePositionEpsilonBlocks;
    if (speed_delta > 0.0F &&
        speed_delta / elapsed >
            authority.max_acceleration + VehicleAccelerationEpsilonBlocksPerSecond) {
      return reject(previous, protocol::VehicleCorrectionReason::TooMuchAcceleration);
    }
    if (!impact_stop && speed_delta < 0.0F &&
        !plausible_collision_deceleration &&
        (-speed_delta) / elapsed >
            authority.max_deceleration + VehicleAccelerationEpsilonBlocksPerSecond) {
      return reject(previous, protocol::VehicleCorrectionReason::TooMuchAcceleration);
    }

    const f32 rotation_speed =
        rotation_delta_radians(previous.rotation, packet.rotation) / elapsed;
    if (rotation_speed >
        authority.max_angular_speed + VehicleAngularSpeedEpsilonRadiansPerSecond) {
      return reject(previous, protocol::VehicleCorrectionReason::TooMuchAngularVelocity);
    }
  }

  return {
      true,
      false,
      protocol::VehicleCorrectionReason::Unknown,
      accepted_state_from_packet(packet, context)};
}

} // namespace arv::server
