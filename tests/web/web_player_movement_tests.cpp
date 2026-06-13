#include "arv/client/chunk_collision_query.hpp"
#include "arv/client/local_player_controller.hpp"
#include "arv/physics/collision_resolution.hpp"
#include "arv/voxel/storage.hpp"

#include <gtest/gtest.h>

#include <span>
#include <utility>
#include <vector>

namespace {

arv::voxel::BlockRegistry make_web_collision_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:stone"));

  arv::voxel::BlockDefinition short_grass;
  short_grass.name = "arvane:short_grass";
  short_grass.render_category = arv::voxel::RenderCategory::Transparent;
  short_grass.properties.replaceable = true;
  short_grass.properties.full_cube = false;
  definitions.push_back(std::move(short_grass));

  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(registry_result) << registry_result.error().message;
  if (!registry_result) {
    return {};
  }
  return std::move(registry_result.value());
}

arv::protocol::ClientboundChunkSectionData make_section(
    arv::ChunkSectionCoord coord = {{0, 0, 0}}) {
  arv::protocol::ClientboundChunkSectionData section;
  section.world_id = arv::WorldID{1};
  section.coord = coord;
  section.chunk_content_version = 1;
  section.blocks.fill(arv::voxel::AirBlockVariantID);
  section.fluids.fill(arv::voxel::empty_fluid_cell());
  return section;
}

void set_block(
    arv::protocol::ClientboundChunkSectionData& section,
    int x,
    int y,
    int z,
    arv::voxel::BlockVariantID block) {
  section.blocks[arv::voxel::local_index(arv::SectionLocalPos{{x, y, z}})] = block;
}

arv::client::LocalPlayerController grounded_controller(arv::Vec3f position) {
  arv::client::LocalPlayerController controller;
  controller.camera().set_rotation(0.0F, 0.0F);
  controller.enter_world(
      arv::WorldID{1},
      arv::PlayerID{2},
      arv::EntityNetID{3},
      position,
      0,
      arv::DefaultBaseTps);

  arv::protocol::ClientboundPlayerMoveCorrection grounded;
  grounded.position = position;
  grounded.flags = arv::protocol::PlayerMoveFlags::OnGround;
  controller.apply_move_correction(grounded);
  return controller;
}

struct ControllerStep {
  arv::player::PlayerCommand command{};
  arv::protocol::ServerboundPlayerMove move{};
};

ControllerStep run_shared_controller_step(
    arv::client::LocalPlayerController& controller,
    arv::client::InputFrame input,
    std::span<const arv::protocol::ClientboundChunkSectionData> sections,
    const arv::voxel::BlockRegistry& block_registry,
    arv::Seconds tick_seconds,
    arv::TickIndex tick = 1) {
  controller.submit_input(input);
  arv::client::ChunkSectionCollisionQuery query(sections, block_registry);
  ControllerStep step;
  step.command = controller.consume_command(tick);
  controller.update_local_movement(step.command, &query, tick_seconds);
  step.move = controller.build_movement_packet(
      0,
      tick,
      arv::client::action_flags_from_player_command(step.command));
  return step;
}

} // namespace

TEST(WebPlayerMovementTests, ChunkCollisionQueryReadsStreamedSections) {
  const arv::voxel::BlockRegistry registry = make_web_collision_registry();
  const auto stone_variant = registry.find_variant("arvane:stone");
  ASSERT_TRUE(stone_variant.has_value());

  auto section = make_section({{1, -1, 0}});
  set_block(section, 2, 15, 3, stone_variant.value());
  section.has_fluids = true;
  section.fluids[arv::voxel::local_index(arv::SectionLocalPos{{2, 15, 3}})] =
      arv::voxel::FluidCell{
          arv::voxel::DefaultWaterFluidTypeID,
          8,
          arv::voxel::FluidFlags::Source};

  const arv::client::ChunkSectionCollisionQuery query(
      std::span<const arv::protocol::ClientboundChunkSectionData>{&section, 1},
      registry);
  const arv::BlockPos world_block{{18, -1, 3}};

  EXPECT_TRUE(query.is_block_loaded(world_block));
  EXPECT_EQ(query.block_variant(world_block), stone_variant.value());
  EXPECT_NE(query.block_body(world_block), nullptr);
  EXPECT_EQ(
      query.fluid_cell(world_block),
      (arv::voxel::FluidCell{
          arv::voxel::DefaultWaterFluidTypeID,
          8,
          arv::voxel::FluidFlags::Source}));
  EXPECT_FALSE(query.is_block_loaded(arv::BlockPos{{0, 0, 0}}));
  EXPECT_EQ(
      query.block_variant(arv::BlockPos{{0, 0, 0}}),
      arv::voxel::AirBlockVariantID);
  EXPECT_TRUE(query.missing_blocks_are_solid());
}

TEST(WebPlayerMovementTests, ChunkCollisionQueryUsesRegistryBlockBodies) {
  const arv::voxel::BlockRegistry registry = make_web_collision_registry();
  const auto stone_variant = registry.find_variant("arvane:stone");
  const auto short_grass_variant = registry.find_variant("arvane:short_grass");
  ASSERT_TRUE(stone_variant.has_value());
  ASSERT_TRUE(short_grass_variant.has_value());

  auto section = make_section();
  set_block(section, 1, 1, 1, short_grass_variant.value());
  set_block(section, 3, 1, 1, stone_variant.value());

  const arv::client::ChunkSectionCollisionQuery query(
      std::span<const arv::protocol::ClientboundChunkSectionData>{&section, 1},
      registry);

  const arv::voxel::BlockBody* grass_body = query.block_body(arv::BlockPos{{1, 1, 1}});
  ASSERT_NE(grass_body, nullptr);
  EXPECT_TRUE(grass_body->boxes.empty());
  EXPECT_FALSE(arv::physics::overlaps_solid_blocks(
      query,
      arv::Aabb3f{{1.1F, 1.1F, 1.1F}, {1.9F, 1.9F, 1.9F}},
      arv::physics::CollisionEpsilon));

  const arv::voxel::BlockBody* stone_body = query.block_body(arv::BlockPos{{3, 1, 1}});
  ASSERT_NE(stone_body, nullptr);
  ASSERT_EQ(stone_body->boxes.size(), 1U);
  EXPECT_TRUE(arv::physics::overlaps_solid_blocks(
      query,
      arv::Aabb3f{{3.1F, 1.1F, 1.1F}, {3.9F, 1.9F, 1.9F}},
      arv::physics::CollisionEpsilon));
}

TEST(WebPlayerMovementTests, SharedControllerMouseLookDoesNotTranslateBodyPosition) {
  const arv::voxel::BlockRegistry registry = make_web_collision_registry();
  const auto stone_variant = registry.find_variant("arvane:stone");
  ASSERT_TRUE(stone_variant.has_value());

  auto section = make_section();
  set_block(section, 8, 0, 8, stone_variant.value());
  auto controller = grounded_controller({8.0F, 1.0F, 8.0F});

  const ControllerStep step = run_shared_controller_step(
      controller,
      arv::client::InputFrame{.mouse_delta_x = 100.0F, .mouse_delta_y = -20.0F},
      std::span<const arv::protocol::ClientboundChunkSectionData>{&section, 1},
      registry,
      0.05);

  EXPECT_EQ(step.move.position, (arv::Vec3f{8.0F, 1.0F, 8.0F}));
  EXPECT_FLOAT_EQ(step.move.yaw_radians, 0.25F);
  EXPECT_FLOAT_EQ(step.move.pitch_radians, 0.05F);
  EXPECT_EQ(controller.player_position(), (arv::Vec3f{8.0F, 1.0F, 8.0F}));
}

TEST(WebPlayerMovementTests, SharedControllerRenderLookDoesNotMoveCameraPosition) {
  auto controller = grounded_controller({8.0F, 1.0F, 8.0F});
  const arv::Vec3f camera_position = controller.camera().position();
  const arv::Vec3f body_position = controller.player_position();

  controller.submit_input(
      arv::client::InputFrame{.mouse_delta_x = 100.0F, .mouse_delta_y = -20.0F});

  EXPECT_EQ(controller.player_position(), body_position);
  EXPECT_EQ(controller.camera().position(), camera_position);
  EXPECT_FLOAT_EQ(controller.camera().yaw_radians(), 0.25F);
  EXPECT_FLOAT_EQ(controller.camera().pitch_radians(), 0.05F);
}

TEST(WebPlayerMovementTests, SharedControllerAirbornePlayerFallsWithSharedGravity) {
  const arv::voxel::BlockRegistry registry = make_web_collision_registry();
  const auto section = make_section();
  arv::client::LocalPlayerController controller;
  controller.camera().set_rotation(0.0F, 0.0F);
  controller.enter_world(
      arv::WorldID{1},
      arv::PlayerID{2},
      arv::EntityNetID{3},
      {8.0F, 5.0F, 8.0F},
      0,
      arv::DefaultBaseTps);

  const ControllerStep step = run_shared_controller_step(
      controller,
      {},
      std::span<const arv::protocol::ClientboundChunkSectionData>{&section, 1},
      registry,
      0.1);

  EXPECT_LT(step.move.velocity.y, 0.0F);
  EXPECT_LT(step.move.position.y, 5.0F);
  EXPECT_FALSE(arv::protocol::has_move_flag(
      step.move.flags,
      arv::protocol::PlayerMoveFlags::OnGround));
}

TEST(WebPlayerMovementTests, SharedControllerBrowserStrafeKeysUseDesktopDirection) {
  const arv::voxel::BlockRegistry registry = make_web_collision_registry();
  const auto stone_variant = registry.find_variant("arvane:stone");
  ASSERT_TRUE(stone_variant.has_value());

  auto section = make_section();
  set_block(section, 8, 0, 8, stone_variant.value());
  const std::span<const arv::protocol::ClientboundChunkSectionData> sections{&section, 1};

  auto right_controller = grounded_controller({8.0F, 1.0F, 8.0F});
  const ControllerStep right = run_shared_controller_step(
      right_controller,
      arv::client::InputFrame{.move_right = true},
      sections,
      registry,
      0.1);

  auto left_controller = grounded_controller({8.0F, 1.0F, 8.0F});
  const ControllerStep left = run_shared_controller_step(
      left_controller,
      arv::client::InputFrame{.move_left = true},
      sections,
      registry,
      0.1);

  EXPECT_GT(right.move.position.x, 8.0F);
  EXPECT_LT(left.move.position.x, 8.0F);
}

TEST(WebPlayerMovementTests, SharedControllerExplicitFlyToggleBuildsActionFlag) {
  const arv::voxel::BlockRegistry registry = make_web_collision_registry();
  auto controller = grounded_controller({8.0F, 1.0F, 8.0F});

  const ControllerStep step = run_shared_controller_step(
      controller,
      arv::client::InputFrame{.toggle_fly_pressed = true},
      {},
      registry,
      arv::tick_duration_seconds(arv::DefaultBaseTps));

  EXPECT_TRUE(step.command.toggle_fly_pressed);
  EXPECT_TRUE(arv::protocol::has_action_flag(
      step.move.action_flags,
      arv::protocol::PlayerActionFlags::ToggleFlyPressed));
}
