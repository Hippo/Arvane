#include "arv/base/base_module.hpp"

#include "arv/base/base_ids.hpp"

#include <array>

namespace arv::base {

std::span<const StarterInventoryItem> starter_inventory_items() noexcept {
  static constexpr std::array<StarterInventoryItem, 9> Items{{
      {ids::StoneBlockID, 64},
      {ids::DirtBlockID, 64},
      {ids::GrassBlockID, 64},
      {ids::GlassBlockID, 64},
      {ids::GlowstoneBlockID, 64},
      {ids::CoalItemID, 32},
      {ids::IronPickaxeItemID, 1},
      {ids::BucketItemID, 1},
      {ids::WaterBucketItemID, 1},
  }};
  return Items;
}

} // namespace arv::base
