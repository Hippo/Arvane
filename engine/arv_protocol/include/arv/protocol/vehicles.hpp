#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/message_writer.hpp"

#include <span>
#include <string>
#include <vector>

namespace arv::protocol {

inline constexpr u32 MaxVehiclePassengersPerMessage = 16U;
inline constexpr u32 MaxVehicleSeatIDBytes = 64U;

enum class VehicleMoveFlags : u16 {
  None = 0U,
  Jump = 1U << 0U,
  Brake = 1U << 1U,
  Sprint = 1U << 2U,
  OnGround = 1U << 3U,
  InWater = 1U << 4U,
};

[[nodiscard]] constexpr VehicleMoveFlags operator|(
    VehicleMoveFlags lhs,
    VehicleMoveFlags rhs) noexcept {
  return static_cast<VehicleMoveFlags>(
      static_cast<u16>(lhs) | static_cast<u16>(rhs));
}

constexpr VehicleMoveFlags& operator|=(
    VehicleMoveFlags& lhs,
    VehicleMoveFlags rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

[[nodiscard]] constexpr bool has_vehicle_move_flag(
    VehicleMoveFlags flags,
    VehicleMoveFlags flag) noexcept {
  return (static_cast<u16>(flags) & static_cast<u16>(flag)) != 0U;
}

enum class VehicleCorrectionReason : u8 {
  InvalidNumber,
  StalePacket,
  NotMounted,
  NotController,
  TooFast,
  TooMuchAcceleration,
  TooMuchAngularVelocity,
  TooMuchVerticalVelocity,
  Collision,
  MissingGroundSupport,
  MissingWaterSupport,
  Unknown,
};

enum class VehicleAction : u8 {
  None,
  Jump,
  Brake,
  Primary,
  Secondary,
};

struct ServerboundVehicleMove {
  EntityNetID vehicle_id{};
  u32 sequence{};
  u64 client_tick{};
  Seconds dt_seconds{};
  Vec3f position{};
  Vec3f velocity{};
  Quatf rotation{};
  Vec3f angular_velocity{};
  f32 throttle{};
  f32 steer{};
  VehicleMoveFlags flags{VehicleMoveFlags::None};
};

struct ClientboundVehicleState {
  WorldID world_id{};
  EntityNetID vehicle_id{};
  u64 server_tick{};
  u32 accepted_sequence{};
  Vec3f position{};
  Vec3f velocity{};
  Quatf rotation{};
  Vec3f angular_velocity{};
  VehicleMoveFlags flags{VehicleMoveFlags::None};
};

struct ClientboundVehicleCorrection {
  EntityNetID vehicle_id{};
  u32 rejected_sequence{};
  Vec3f position{};
  Vec3f velocity{};
  Quatf rotation{};
  Vec3f angular_velocity{};
  VehicleCorrectionReason reason{VehicleCorrectionReason::Unknown};
};

struct ServerboundRideEntityRequest {
  EntityNetID rideable_id{};
  std::string seat_id{};
};

struct ServerboundDismountRequest {
  EntityNetID rideable_id{};
};

struct ServerboundVehicleAction {
  EntityNetID vehicle_id{};
  VehicleAction action{VehicleAction::None};
  bool pressed{};
};

struct ClientboundRideStarted {
  EntityNetID passenger_id{};
  EntityNetID rideable_id{};
  std::string seat_id{};
  bool controls_vehicle{};
};

struct ClientboundRideEnded {
  EntityNetID passenger_id{};
  EntityNetID rideable_id{};
  Vec3f position{};
};

struct VehiclePassengerEntry {
  EntityNetID passenger_id{};
  std::string seat_id{};
  bool controls_vehicle{};
};

struct ClientboundEntityPassengersSet {
  EntityNetID rideable_id{};
  std::vector<VehiclePassengerEntry> passengers{};
};

std::vector<u8> serialize_serverbound_vehicle_move(const ServerboundVehicleMove& move);
Expected<ServerboundVehicleMove, ProtocolError> deserialize_serverbound_vehicle_move(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_vehicle_state(const ClientboundVehicleState& state);
Expected<ClientboundVehicleState, ProtocolError> deserialize_clientbound_vehicle_state(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_vehicle_correction(const ClientboundVehicleCorrection& correction);
Expected<ClientboundVehicleCorrection, ProtocolError> deserialize_clientbound_vehicle_correction(
    std::span<const u8> bytes);

std::vector<u8> serialize_serverbound_ride_entity_request(const ServerboundRideEntityRequest& request);
Expected<ServerboundRideEntityRequest, ProtocolError> deserialize_serverbound_ride_entity_request(
    std::span<const u8> bytes);

std::vector<u8> serialize_serverbound_dismount_request(const ServerboundDismountRequest& request);
Expected<ServerboundDismountRequest, ProtocolError> deserialize_serverbound_dismount_request(
    std::span<const u8> bytes);

std::vector<u8> serialize_serverbound_vehicle_action(const ServerboundVehicleAction& action);
Expected<ServerboundVehicleAction, ProtocolError> deserialize_serverbound_vehicle_action(
    std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_ride_started(const ClientboundRideStarted& event);
Expected<ClientboundRideStarted, ProtocolError> deserialize_clientbound_ride_started(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_ride_ended(const ClientboundRideEnded& event);
Expected<ClientboundRideEnded, ProtocolError> deserialize_clientbound_ride_ended(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_entity_passengers_set(const ClientboundEntityPassengersSet& event);
Expected<ClientboundEntityPassengersSet, ProtocolError> deserialize_clientbound_entity_passengers_set(
    std::span<const u8> bytes);

} // namespace arv::protocol
