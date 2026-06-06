#pragma once

#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/entity/components.hpp"
#include "arv/protocol/vehicles.hpp"

namespace arv::server {

struct AcceptedVehicleMovementState {
  Vec3f position{};
  Vec3f velocity{};
  Quatf rotation{};
  Vec3f angular_velocity{};
  protocol::VehicleMoveFlags flags{protocol::VehicleMoveFlags::None};
  u32 last_sequence{};
  TickIndex last_server_tick{};
  double last_receive_time_seconds{};
  bool has_accepted_move{};
};

struct VehicleMovementValidationContext {
  double receive_time_seconds{};
  TickIndex server_tick{};
  bool passenger_is_mounted{};
  bool passenger_controls_vehicle{};
  bool final_aabb_overlaps_solid{};
  bool proposed_sweep_hit_solid{};
  bool previous_velocity_sweep_hit_solid{};
  bool target_chunk_loaded{true};
  bool swept_path_chunks_loaded{true};
  bool has_collision_resolved_state{};
  AcceptedVehicleMovementState collision_resolved_state{};
  bool has_ground_support{true};
  u32 water_support_samples{};
  f32 water_occupancy{1.0F};
};

struct VehicleMovementValidationResult {
  bool accepted{};
  bool should_send_correction{};
  protocol::VehicleCorrectionReason reason{protocol::VehicleCorrectionReason::Unknown};
  AcceptedVehicleMovementState accepted_state{};
};

[[nodiscard]] bool vehicle_sequence_is_newer(u32 sequence, u32 previous) noexcept;

[[nodiscard]] VehicleMovementValidationResult validate_serverbound_vehicle_move(
    const protocol::ServerboundVehicleMove& packet,
    const AcceptedVehicleMovementState& previous,
    const VehicleMovementValidationContext& context,
    const entity::VehicleAuthorityDefinition& authority) noexcept;

} // namespace arv::server
