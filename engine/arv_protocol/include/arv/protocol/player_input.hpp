#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/coordinates.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"
#include "arv/player/player_state.hpp"
#include "arv/protocol/message_writer.hpp"
#include "arv/voxel/block.hpp"

#include <string>
#include <vector>

namespace arv::protocol {

enum class ClientInputButtons : u32 {
  None = 0U,
  JumpPressed = 1U << 0U,
  JumpHeld = 1U << 1U,
  SneakHeld = 1U << 2U,
  SprintHeld = 1U << 3U,
  PrimaryPressed = 1U << 4U,
  PrimaryHeld = 1U << 5U,
  SecondaryPressed = 1U << 6U,
  SecondaryHeld = 1U << 7U,
  ToggleFlyPressed = 1U << 8U,
};

[[nodiscard]] constexpr ClientInputButtons operator|(ClientInputButtons lhs, ClientInputButtons rhs) noexcept {
  return static_cast<ClientInputButtons>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
}

[[nodiscard]] constexpr bool has_button(ClientInputButtons buttons, ClientInputButtons flag) noexcept {
  return (static_cast<u32>(buttons) & static_cast<u32>(flag)) != 0U;
}

struct InputSample {
  ClientInputButtons buttons{ClientInputButtons::None};
  i16 move_x{};
  i16 move_z{};
  i16 look_delta_x{};
  i16 look_delta_y{};
  u8 selected_hotbar_slot{};
};

struct ClientInputBatch {
  u64 first_input_tick{};
  std::vector<InputSample> inputs{};
};

struct BlockBreakProgress {
  bool active{};
  BlockPos block{};
  voxel::BlockVariantID variant{};
  f32 progress{};
  u8 stage{};

  constexpr bool operator==(const BlockBreakProgress&) const = default;
};

struct PlayerCorrection {
  u64 server_tick{};
  PlayerID player_id{};
  WorldID world_id{};
  Vec3f position{};
  Vec3f velocity{};
  f32 yaw_radians{};
  f32 pitch_radians{};
  player::MovementMode movement_mode{player::MovementMode::Walking};
  std::string game_mode_id{std::string{player::SurvivalGameModeID}};
  player::ResolvedCorePermissions resolved_core_permissions{};
  bool grounded{};
  bool sneaking{};
  bool swimming{};
  f32 breath_seconds{player::DefaultBreathSeconds};
  u8 selected_hotbar_slot{};
  BlockBreakProgress block_break{};
};

std::vector<u8> serialize_client_input_batch(const ClientInputBatch& batch);
Expected<ClientInputBatch, ProtocolError> deserialize_client_input_batch(std::span<const u8> bytes);

std::vector<u8> serialize_player_correction(const PlayerCorrection& correction);
Expected<PlayerCorrection, ProtocolError> deserialize_player_correction(std::span<const u8> bytes);

[[nodiscard]] player::PlayerCommand input_sample_to_command(
    const InputSample& sample,
    TickIndex input_tick) noexcept;
[[nodiscard]] InputSample input_sample_from_command(const player::PlayerCommand& command) noexcept;

} // namespace arv::protocol
