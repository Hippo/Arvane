#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"
#include "arv/player/permissions.hpp"
#include "arv/protocol/message_writer.hpp"
#include "arv/protocol/player_movement.hpp"
#include "arv/voxel/block.hpp"

#include <string>
#include <vector>

namespace arv::protocol {

struct BlockBreakProgress {
  bool active{};
  BlockPos block{};
  voxel::BlockVariantID variant{};
  f32 progress{};
  u8 stage{};

  constexpr bool operator==(const BlockBreakProgress&) const = default;
};

struct ClientboundPlayerVelocity {
  u64 server_tick{};
  PlayerID player_id{};
  Vec3f velocity{};
  u32 knockback_control_suppression_ticks{};
};

struct ClientboundSetMovementHz {
  f32 movement_hz{DefaultClientMovementSendRateHz};
};

struct ClientboundPlayerPermissions {
  std::string game_mode_id{};
  player::ResolvedCorePermissions core{};
};

struct ClientboundPlayerMovementTuning {
  f32 fly_speed{};
  f32 fly_vertical_speed{};
};

struct ClientboundPlayerBlockBreakProgress {
  u64 server_tick{};
  PlayerID player_id{};
  BlockBreakProgress block_break{};
};

std::vector<u8> serialize_clientbound_player_velocity(const ClientboundPlayerVelocity& velocity);
Expected<ClientboundPlayerVelocity, ProtocolError> deserialize_clientbound_player_velocity(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_set_movement_hz(const ClientboundSetMovementHz& settings);
Expected<ClientboundSetMovementHz, ProtocolError> deserialize_clientbound_set_movement_hz(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_player_permissions(const ClientboundPlayerPermissions& permissions);
Expected<ClientboundPlayerPermissions, ProtocolError> deserialize_clientbound_player_permissions(
    std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_player_movement_tuning(
    const ClientboundPlayerMovementTuning& tuning);
Expected<ClientboundPlayerMovementTuning, ProtocolError>
deserialize_clientbound_player_movement_tuning(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_player_block_break_progress(
    const ClientboundPlayerBlockBreakProgress& progress);
Expected<ClientboundPlayerBlockBreakProgress, ProtocolError>
deserialize_clientbound_player_block_break_progress(std::span<const u8> bytes);

} // namespace arv::protocol
