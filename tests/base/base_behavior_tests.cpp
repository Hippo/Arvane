#include "arv/base/base_ids.hpp"
#include "arv/base/base_module.hpp"
#include "arv/player/item.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/entity_goal_registry.hpp"

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

TEST(BaseBehaviorTests, RegistersBaseEntityGoalIds) {
  arv::world::EntityGoalRegistry entity_goals;

  arv::base::register_base_entity_goals(entity_goals);

  EXPECT_TRUE(entity_goals.contains(arv::base::ids::IdleGoalID));
  EXPECT_TRUE(entity_goals.contains(arv::base::ids::WanderGoalID));
  EXPECT_TRUE(entity_goals.contains(arv::base::ids::AvoidFluidGoalID));
  EXPECT_TRUE(entity_goals.contains(arv::base::ids::SwimInFluidGoalID));
  EXPECT_TRUE(entity_goals.contains(arv::base::ids::NearestTargetGoalID));
  EXPECT_TRUE(entity_goals.contains(arv::base::ids::MoveToTargetGoalID));
  EXPECT_TRUE(entity_goals.contains(arv::base::ids::LookAtTargetGoalID));
  EXPECT_TRUE(entity_goals.contains(arv::base::ids::MeleeAttackGoalID));
  EXPECT_TRUE(entity_goals.contains(arv::base::ids::FleeFromTargetGoalID));
  EXPECT_TRUE(entity_goals.contains(arv::base::ids::ReturnHomeGoalID));
}
