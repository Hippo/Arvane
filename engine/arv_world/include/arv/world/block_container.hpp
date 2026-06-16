#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/types.hpp"
#include "arv/player/inventory.hpp"

#include <string>
#include <vector>

namespace arv::world {

struct BlockContainerSlotRecord {
  u32 slot{};
  player::ItemStack stack{};
};

struct SavedBlockContainer {
  SectionLocalPos local{};
  std::string container_type{};
  u32 slot_count{};
  std::vector<BlockContainerSlotRecord> slots{};
};

struct BlockAttachedContainer {
  std::string container_type{};
  player::Inventory inventory{};
};

} // namespace arv::world
