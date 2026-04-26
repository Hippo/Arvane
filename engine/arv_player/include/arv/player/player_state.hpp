#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/player/inventory.hpp"
#include "arv/player/player_constants.hpp"

namespace arv::player {

enum class MovementMode : u8 {
  Walking,
  Flying,
};

struct PlayerCommand {
  TickIndex client_tick{};
  i16 move_x{};
  i16 move_z{};
  i16 look_delta_x{};
  i16 look_delta_y{};
  bool jump_pressed{};
  bool jump_held{};
  bool move_down_held{};
  bool sprint_held{};
  bool primary_pressed{};
  bool primary_held{};
  bool secondary_pressed{};
  bool secondary_held{};
  bool toggle_fly_pressed{};
  u8 selected_hotbar_slot{};
};

struct PlayerState {
  PlayerID player_id{};
  WorldID world_id{};
  Vec3f position{8.0F, 20.0F, -20.0F};
  Vec3f velocity{};
  f32 yaw_radians{};
  f32 pitch_radians{};
  bool grounded{};
  MovementMode movement_mode{MovementMode::Walking};
  Inventory inventory{DefaultPlayerInventorySlotCount};
  u8 selected_hotbar_slot{};
  TickIndex last_processed_input_tick{};
};

struct PlayerViewState {
  PlayerID player_id{};
  WorldID world_id{};
  Vec3f position{};
  Vec3f velocity{};
  f32 yaw_radians{};
  f32 pitch_radians{};
  bool grounded{};
  MovementMode movement_mode{MovementMode::Walking};
  u8 selected_hotbar_slot{};
};

[[nodiscard]] inline const std::optional<ItemStack>& held_item_slot(const PlayerState& player) {
  return player.inventory.slot(player.selected_hotbar_slot);
}

} // namespace arv::player
