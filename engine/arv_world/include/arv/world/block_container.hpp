#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/player/inventory.hpp"

#include <string>
#include <optional>
#include <vector>

namespace arv::world {

struct BlockContainerSlotRecord {
  u32 slot{};
  player::ItemStack stack{};
};

struct FurnaceCycleState {
  player::ItemID input{};
  u16 input_count{};
  player::ItemID output{};
  u16 output_count{};
  Seconds duration_secs{};
  Seconds start_at{};
};

struct FurnaceState {
  std::optional<FurnaceCycleState> active_cycle{};
};

struct SavedBlockContainer {
  SectionLocalPos local{};
  std::string container_type{};
  u32 slot_count{};
  std::vector<BlockContainerSlotRecord> slots{};
  FurnaceState furnace{};
};

struct BlockAttachedContainer {
  std::string container_type{};
  player::Inventory inventory{};
  FurnaceState furnace{};
};

} // namespace arv::world
