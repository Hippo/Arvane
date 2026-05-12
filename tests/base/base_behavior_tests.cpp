#include "arv/base/base_ids.hpp"
#include "arv/base/base_module.hpp"
#include "arv/player/item.hpp"
#include "arv/voxel/block.hpp"

#include <gtest/gtest.h>

TEST(BaseBehaviorTests, RegistersBaseBlockAndItemBehaviorIds) {
  arv::voxel::BlockBehaviorRegistry block_behaviors;
  arv::player::ItemBehaviorRegistry item_behaviors;

  arv::base::register_base_block_behaviors(block_behaviors);
  arv::base::register_base_item_behaviors(item_behaviors);

  EXPECT_TRUE(block_behaviors.contains(arv::base::ids::FluidBlockBehaviorID));
  EXPECT_TRUE(item_behaviors.contains(arv::base::ids::BlockItemBehaviorID));
  EXPECT_TRUE(item_behaviors.contains(arv::base::ids::ToolItemBehaviorID));
  EXPECT_TRUE(item_behaviors.contains(arv::base::ids::EmptyFluidContainerItemBehaviorID));
  EXPECT_TRUE(item_behaviors.contains(arv::base::ids::FilledFluidContainerItemBehaviorID));
}
