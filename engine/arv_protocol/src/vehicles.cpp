#include "arv/protocol/vehicles.hpp"
#include "arv/protocol/message_primitives.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace arv::protocol {
namespace {

[[nodiscard]] std::string clamp_seat_id(std::string_view seat_id) {
  if (seat_id.size() <= MaxVehicleSeatIDBytes) {
    return std::string{seat_id};
  }
  return std::string{seat_id.substr(0, MaxVehicleSeatIDBytes)};
}

void write_seat_id(MessageWriter& writer, std::string_view seat_id) {
  writer.write_string(clamp_seat_id(seat_id));
}

[[nodiscard]] Expected<std::string, ProtocolError> read_seat_id(
    MessageReader& reader,
    const char* context) {
  auto seat_id = reader.read_string();
  if (!seat_id) {
    return Expected<std::string, ProtocolError>::failure(
        ProtocolError{std::string{"failed to decode "} + context});
  }
  if (seat_id.value().size() > MaxVehicleSeatIDBytes) {
    return Expected<std::string, ProtocolError>::failure(
        ProtocolError{std::string{"vehicle seat id too long in "} + context});
  }
  return Expected<std::string, ProtocolError>::success(std::move(seat_id.value()));
}

[[nodiscard]] Expected<void, ProtocolError> finish(
    MessageReader& reader,
    const char* context) {
  return require_no_trailing_bytes(reader, context);
}

} // namespace

std::vector<u8> serialize_serverbound_vehicle_move(const ServerboundVehicleMove& move) {
  MessageWriter writer;
  writer.write<u32>(move.vehicle_id.value);
  writer.write<u32>(move.sequence);
  writer.write<u64>(move.client_tick);
  write_f32(writer, move.dt_seconds);
  write_vec3(writer, move.position);
  write_vec3(writer, move.velocity);
  write_quat(writer, move.rotation);
  write_vec3(writer, move.angular_velocity);
  write_f32(writer, move.throttle);
  write_f32(writer, move.steer);
  writer.write<VehicleMoveFlags>(move.flags);
  return std::move(writer).take_bytes();
}

Expected<ServerboundVehicleMove, ProtocolError> deserialize_serverbound_vehicle_move(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto vehicle_id = reader.read<u32>();
  auto sequence = reader.read<u32>();
  auto client_tick = reader.read<u64>();
  auto dt = read_f32(reader);
  auto position = read_vec3(reader);
  auto velocity = read_vec3(reader);
  auto rotation = read_quat(reader);
  auto angular_velocity = read_vec3(reader);
  auto throttle = read_f32(reader);
  auto steer = read_f32(reader);
  auto flags = reader.read<VehicleMoveFlags>();
  if (!vehicle_id || !sequence || !client_tick || !dt || !position ||
      !velocity || !rotation || !angular_velocity || !throttle || !steer ||
      !flags) {
    return Expected<ServerboundVehicleMove, ProtocolError>::failure(
        ProtocolError{"failed to decode vehicle move"});
  }
  auto trailing = finish(reader, "vehicle message");
  if (!trailing) {
    return Expected<ServerboundVehicleMove, ProtocolError>::failure(trailing.error());
  }
  return Expected<ServerboundVehicleMove, ProtocolError>::success(
      {EntityNetID{vehicle_id.value()},
       sequence.value(),
       client_tick.value(),
       dt.value(),
       position.value(),
       velocity.value(),
       rotation.value(),
       angular_velocity.value(),
       throttle.value(),
       steer.value(),
       flags.value()});
}

std::vector<u8> serialize_clientbound_vehicle_state(const ClientboundVehicleState& state) {
  MessageWriter writer;
  writer.write<u32>(state.world_id.value);
  writer.write<u32>(state.vehicle_id.value);
  writer.write<u64>(state.server_tick);
  writer.write<u32>(state.accepted_sequence);
  write_vec3(writer, state.position);
  write_vec3(writer, state.velocity);
  write_quat(writer, state.rotation);
  write_vec3(writer, state.angular_velocity);
  writer.write<VehicleMoveFlags>(state.flags);
  return std::move(writer).take_bytes();
}

Expected<ClientboundVehicleState, ProtocolError> deserialize_clientbound_vehicle_state(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto vehicle_id = reader.read<u32>();
  auto server_tick = reader.read<u64>();
  auto accepted_sequence = reader.read<u32>();
  auto position = read_vec3(reader);
  auto velocity = read_vec3(reader);
  auto rotation = read_quat(reader);
  auto angular_velocity = read_vec3(reader);
  auto flags = reader.read<VehicleMoveFlags>();
  if (!world_id || !vehicle_id || !server_tick || !accepted_sequence ||
      !position || !velocity || !rotation || !angular_velocity || !flags) {
    return Expected<ClientboundVehicleState, ProtocolError>::failure(
        ProtocolError{"failed to decode vehicle state"});
  }
  auto trailing = finish(reader, "vehicle message");
  if (!trailing) {
    return Expected<ClientboundVehicleState, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundVehicleState, ProtocolError>::success(
      {WorldID{world_id.value()},
       EntityNetID{vehicle_id.value()},
       server_tick.value(),
       accepted_sequence.value(),
       position.value(),
       velocity.value(),
       rotation.value(),
       angular_velocity.value(),
       flags.value()});
}

std::vector<u8> serialize_clientbound_vehicle_correction(
    const ClientboundVehicleCorrection& correction) {
  MessageWriter writer;
  writer.write<u32>(correction.vehicle_id.value);
  writer.write<u32>(correction.rejected_sequence);
  write_vec3(writer, correction.position);
  write_vec3(writer, correction.velocity);
  write_quat(writer, correction.rotation);
  write_vec3(writer, correction.angular_velocity);
  writer.write<VehicleCorrectionReason>(correction.reason);
  return std::move(writer).take_bytes();
}

Expected<ClientboundVehicleCorrection, ProtocolError> deserialize_clientbound_vehicle_correction(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto vehicle_id = reader.read<u32>();
  auto rejected_sequence = reader.read<u32>();
  auto position = read_vec3(reader);
  auto velocity = read_vec3(reader);
  auto rotation = read_quat(reader);
  auto angular_velocity = read_vec3(reader);
  auto reason = reader.read<VehicleCorrectionReason>();
  if (!vehicle_id || !rejected_sequence || !position || !velocity ||
      !rotation || !angular_velocity || !reason) {
    return Expected<ClientboundVehicleCorrection, ProtocolError>::failure(
        ProtocolError{"failed to decode vehicle correction"});
  }
  auto trailing = finish(reader, "vehicle message");
  if (!trailing) {
    return Expected<ClientboundVehicleCorrection, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundVehicleCorrection, ProtocolError>::success(
      {EntityNetID{vehicle_id.value()},
       rejected_sequence.value(),
       position.value(),
       velocity.value(),
       rotation.value(),
       angular_velocity.value(),
       reason.value()});
}

std::vector<u8> serialize_serverbound_ride_entity_request(
    const ServerboundRideEntityRequest& request) {
  MessageWriter writer;
  writer.write<u32>(request.rideable_id.value);
  write_seat_id(writer, request.seat_id);
  return std::move(writer).take_bytes();
}

Expected<ServerboundRideEntityRequest, ProtocolError> deserialize_serverbound_ride_entity_request(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto rideable_id = reader.read<u32>();
  auto seat_id = read_seat_id(reader, "ride request");
  if (!rideable_id || !seat_id) {
    return Expected<ServerboundRideEntityRequest, ProtocolError>::failure(
        ProtocolError{"failed to decode ride request"});
  }
  auto trailing = finish(reader, "vehicle message");
  if (!trailing) {
    return Expected<ServerboundRideEntityRequest, ProtocolError>::failure(trailing.error());
  }
  return Expected<ServerboundRideEntityRequest, ProtocolError>::success(
      {EntityNetID{rideable_id.value()}, std::move(seat_id.value())});
}

std::vector<u8> serialize_serverbound_dismount_request(
    const ServerboundDismountRequest& request) {
  MessageWriter writer;
  writer.write<u32>(request.rideable_id.value);
  return std::move(writer).take_bytes();
}

Expected<ServerboundDismountRequest, ProtocolError> deserialize_serverbound_dismount_request(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto rideable_id = reader.read<u32>();
  if (!rideable_id) {
    return Expected<ServerboundDismountRequest, ProtocolError>::failure(
        ProtocolError{"failed to decode dismount request"});
  }
  auto trailing = finish(reader, "vehicle message");
  if (!trailing) {
    return Expected<ServerboundDismountRequest, ProtocolError>::failure(trailing.error());
  }
  return Expected<ServerboundDismountRequest, ProtocolError>::success(
      {EntityNetID{rideable_id.value()}});
}

std::vector<u8> serialize_serverbound_vehicle_action(
    const ServerboundVehicleAction& action) {
  MessageWriter writer;
  writer.write<u32>(action.vehicle_id.value);
  writer.write<VehicleAction>(action.action);
  writer.write_bool(action.pressed);
  return std::move(writer).take_bytes();
}

Expected<ServerboundVehicleAction, ProtocolError> deserialize_serverbound_vehicle_action(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto vehicle_id = reader.read<u32>();
  auto action = reader.read<VehicleAction>();
  auto pressed = reader.read_bool();
  if (!vehicle_id || !action || !pressed) {
    return Expected<ServerboundVehicleAction, ProtocolError>::failure(
        ProtocolError{"failed to decode vehicle action"});
  }
  auto trailing = finish(reader, "vehicle message");
  if (!trailing) {
    return Expected<ServerboundVehicleAction, ProtocolError>::failure(trailing.error());
  }
  return Expected<ServerboundVehicleAction, ProtocolError>::success(
      {EntityNetID{vehicle_id.value()}, action.value(), pressed.value()});
}

std::vector<u8> serialize_clientbound_ride_started(const ClientboundRideStarted& event) {
  MessageWriter writer;
  writer.write<u32>(event.passenger_id.value);
  writer.write<u32>(event.rideable_id.value);
  write_seat_id(writer, event.seat_id);
  writer.write_bool(event.controls_vehicle);
  return std::move(writer).take_bytes();
}

Expected<ClientboundRideStarted, ProtocolError> deserialize_clientbound_ride_started(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto passenger_id = reader.read<u32>();
  auto rideable_id = reader.read<u32>();
  auto seat_id = read_seat_id(reader, "ride started");
  auto controls = reader.read_bool();
  if (!passenger_id || !rideable_id || !seat_id || !controls) {
    return Expected<ClientboundRideStarted, ProtocolError>::failure(
        ProtocolError{"failed to decode ride started"});
  }
  auto trailing = finish(reader, "vehicle message");
  if (!trailing) {
    return Expected<ClientboundRideStarted, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundRideStarted, ProtocolError>::success(
      {EntityNetID{passenger_id.value()},
       EntityNetID{rideable_id.value()},
       std::move(seat_id.value()),
       controls.value()});
}

std::vector<u8> serialize_clientbound_ride_ended(const ClientboundRideEnded& event) {
  MessageWriter writer;
  writer.write<u32>(event.passenger_id.value);
  writer.write<u32>(event.rideable_id.value);
  write_vec3(writer, event.position);
  return std::move(writer).take_bytes();
}

Expected<ClientboundRideEnded, ProtocolError> deserialize_clientbound_ride_ended(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto passenger_id = reader.read<u32>();
  auto rideable_id = reader.read<u32>();
  auto position = read_vec3(reader);
  if (!passenger_id || !rideable_id || !position) {
    return Expected<ClientboundRideEnded, ProtocolError>::failure(
        ProtocolError{"failed to decode ride ended"});
  }
  auto trailing = finish(reader, "vehicle message");
  if (!trailing) {
    return Expected<ClientboundRideEnded, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundRideEnded, ProtocolError>::success(
      {EntityNetID{passenger_id.value()},
       EntityNetID{rideable_id.value()},
       position.value()});
}

std::vector<u8> serialize_clientbound_entity_passengers_set(
    const ClientboundEntityPassengersSet& event) {
  MessageWriter writer;
  writer.write<u32>(event.rideable_id.value);
  const u32 count = static_cast<u32>(
      std::min<usize>(event.passengers.size(), MaxVehiclePassengersPerMessage));
  writer.write<u32>(count);
  for (u32 index = 0; index < count; ++index) {
    const VehiclePassengerEntry& passenger = event.passengers[index];
    writer.write<u32>(passenger.passenger_id.value);
    write_seat_id(writer, passenger.seat_id);
    writer.write_bool(passenger.controls_vehicle);
  }
  return std::move(writer).take_bytes();
}

Expected<ClientboundEntityPassengersSet, ProtocolError>
deserialize_clientbound_entity_passengers_set(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto rideable_id = reader.read<u32>();
  auto count = reader.read<u32>();
  if (!rideable_id || !count) {
    return Expected<ClientboundEntityPassengersSet, ProtocolError>::failure(
        ProtocolError{"failed to decode passenger set header"});
  }
  if (count.value() > MaxVehiclePassengersPerMessage) {
    return Expected<ClientboundEntityPassengersSet, ProtocolError>::failure(
        ProtocolError{"too many passengers in passenger set"});
  }

  ClientboundEntityPassengersSet event;
  event.rideable_id = EntityNetID{rideable_id.value()};
  event.passengers.reserve(count.value());
  for (u32 index = 0; index < count.value(); ++index) {
    auto passenger_id = reader.read<u32>();
    auto seat_id = read_seat_id(reader, "passenger set");
    auto controls = reader.read_bool();
    if (!passenger_id || !seat_id || !controls) {
      return Expected<ClientboundEntityPassengersSet, ProtocolError>::failure(
          ProtocolError{"failed to decode passenger set entry"});
    }
    event.passengers.push_back(
        {EntityNetID{passenger_id.value()},
         std::move(seat_id.value()),
         controls.value()});
  }
  auto trailing = finish(reader, "vehicle message");
  if (!trailing) {
    return Expected<ClientboundEntityPassengersSet, ProtocolError>::failure(
        trailing.error());
  }
  return Expected<ClientboundEntityPassengersSet, ProtocolError>::success(std::move(event));
}

} // namespace arv::protocol
