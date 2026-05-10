#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/message_writer.hpp"
#include "arv/voxel/block.hpp"

#include <string>
#include <vector>

namespace arv::protocol {

inline constexpr f32 DefaultClientMovementSendRateHz = 20.0F;

enum class PlayerMoveFlags : u32 {
  None = 0U,
  OnGround = 1U << 0U,
  Jumping = 1U << 1U,
  Sneaking = 1U << 2U,
  Sprinting = 1U << 3U,
  Swimming = 1U << 4U,
  Flying = 1U << 5U,
};

[[nodiscard]] constexpr PlayerMoveFlags operator|(PlayerMoveFlags lhs, PlayerMoveFlags rhs) noexcept {
  return static_cast<PlayerMoveFlags>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
}

constexpr PlayerMoveFlags& operator|=(PlayerMoveFlags& lhs, PlayerMoveFlags rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

[[nodiscard]] constexpr bool has_move_flag(PlayerMoveFlags flags, PlayerMoveFlags flag) noexcept {
  return (static_cast<u32>(flags) & static_cast<u32>(flag)) != 0U;
}

enum class PlayerActionFlags : u16 {
  None = 0U,
  PrimaryPressed = 1U << 0U,
  PrimaryHeld = 1U << 1U,
  SecondaryPressed = 1U << 2U,
  SecondaryHeld = 1U << 3U,
  ToggleFlyPressed = 1U << 4U,
};

[[nodiscard]] constexpr PlayerActionFlags operator|(PlayerActionFlags lhs, PlayerActionFlags rhs) noexcept {
  return static_cast<PlayerActionFlags>(static_cast<u16>(lhs) | static_cast<u16>(rhs));
}

constexpr PlayerActionFlags& operator|=(PlayerActionFlags& lhs, PlayerActionFlags rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

[[nodiscard]] constexpr bool has_action_flag(PlayerActionFlags flags, PlayerActionFlags flag) noexcept {
  return (static_cast<u16>(flags) & static_cast<u16>(flag)) != 0U;
}

struct ServerboundPlayerMove {
  u32 sequence{};
  u64 client_tick{};
  Vec3f position{};
  Vec3f velocity{};
  f32 yaw_radians{};
  f32 pitch_radians{};
  PlayerMoveFlags flags{PlayerMoveFlags::None};
  PlayerActionFlags action_flags{PlayerActionFlags::None};
  u8 selected_hotbar_slot{};
};

struct BlockBreakProgress {
  bool active{};
  BlockPos block{};
  voxel::BlockVariantID variant{};
  f32 progress{};
  u8 stage{};

  constexpr bool operator==(const BlockBreakProgress&) const = default;
};

enum class PlayerMoveCorrectionReason : u8 {
  InvalidNumber,
  StalePacket,
  TooFast,
  TooFar,
  Collision,
  UnloadedChunk,
  OutOfWorld,
  PermissionDenied,
  ServerTeleport,
  Unknown,
};

struct ClientboundPlayerMoveCorrection {
  u32 rejected_sequence{};
  Vec3f position{};
  Vec3f velocity{};
  f32 yaw_radians{};
  f32 pitch_radians{};
  PlayerMoveFlags flags{PlayerMoveFlags::None};
  PlayerMoveCorrectionReason reason{PlayerMoveCorrectionReason::Unknown};
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

struct ClientboundPlayerBlockBreakProgress {
  u64 server_tick{};
  PlayerID player_id{};
  BlockBreakProgress block_break{};
};

std::vector<u8> serialize_serverbound_player_move(const ServerboundPlayerMove& move);
Expected<ServerboundPlayerMove, ProtocolError> deserialize_serverbound_player_move(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_player_move_correction(const ClientboundPlayerMoveCorrection& correction);
Expected<ClientboundPlayerMoveCorrection, ProtocolError> deserialize_clientbound_player_move_correction(
    std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_player_velocity(const ClientboundPlayerVelocity& velocity);
Expected<ClientboundPlayerVelocity, ProtocolError> deserialize_clientbound_player_velocity(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_set_movement_hz(const ClientboundSetMovementHz& settings);
Expected<ClientboundSetMovementHz, ProtocolError> deserialize_clientbound_set_movement_hz(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_player_block_break_progress(
    const ClientboundPlayerBlockBreakProgress& progress);
Expected<ClientboundPlayerBlockBreakProgress, ProtocolError>
deserialize_clientbound_player_block_break_progress(std::span<const u8> bytes);

} // namespace arv::protocol
