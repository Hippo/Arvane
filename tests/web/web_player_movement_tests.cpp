#include "web_player_movement.hpp"

#include "arv/client/local_player_controller.hpp"
#include "arv/physics/collision_query.hpp"
#include "arv/voxel/storage.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <span>

namespace {

class SectionCollisionQuery final : public arv::physics::ICollisionQuery {
public:
  explicit SectionCollisionQuery(
      std::span<const arv::protocol::ClientboundChunkSectionData> sections) noexcept
      : sections_(sections) {
  }

  [[nodiscard]] bool is_block_loaded(arv::BlockPos pos) const noexcept override {
    return section_for(pos) != nullptr;
  }

  [[nodiscard]] arv::voxel::BlockVariantID block_variant(
      arv::BlockPos pos) const noexcept override {
    const auto* section = section_for(pos);
    if (section == nullptr) {
      return arv::voxel::AirBlockVariantID;
    }
    return section->blocks[arv::voxel::local_index(arv::block_to_local(pos))];
  }

  [[nodiscard]] const arv::voxel::BlockBody* block_body(
      arv::BlockPos pos) const noexcept override {
    return block_variant(pos) == arv::voxel::AirBlockVariantID ? nullptr : &full_cube_body();
  }

private:
  [[nodiscard]] const arv::protocol::ClientboundChunkSectionData* section_for(
      arv::BlockPos pos) const noexcept {
    const arv::ChunkSectionCoord coord = arv::block_to_chunk(pos);
    const auto found = std::find_if(
        sections_.begin(),
        sections_.end(),
        [&](const arv::protocol::ClientboundChunkSectionData& section) {
          return section.coord == coord;
        });
    return found == sections_.end() ? nullptr : &*found;
  }

  [[nodiscard]] static const arv::voxel::BlockBody& full_cube_body() noexcept {
    static const arv::voxel::BlockBody body{{
        arv::Aabb3f{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}}}};
    return body;
  }

  std::span<const arv::protocol::ClientboundChunkSectionData> sections_;
};

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
    arv::voxel::BlockVariantID block = arv::voxel::BlockVariantID{1}) {
  section.blocks[arv::voxel::local_index(arv::SectionLocalPos{{x, y, z}})] = block;
}

arv::web::WebPlayerMovementState supported_player_state() {
  arv::web::WebPlayerMovementState state;
  state.position = {8.0F, 1.0F, 8.0F};
  state.grounded = true;
  return state;
}

} // namespace

TEST(WebPlayerMovementTests, MouseLookDoesNotTranslateBodyPosition) {
  auto section = make_section();
  set_block(section, 8, 0, 8);
  const arv::web::WebPlayerMovementState state = supported_player_state();

  arv::web::WebPlayerMovementInput input;
  input.mouse_dx = 100.0F;
  input.mouse_dy = -20.0F;
  const arv::web::WebPlayerMovementStep step =
      arv::web::simulate_web_player_movement(
          state,
          input,
          std::span<const arv::protocol::ClientboundChunkSectionData>{&section, 1},
          0.05);

  EXPECT_EQ(step.state.position, state.position);
  EXPECT_FLOAT_EQ(step.state.yaw_radians, 0.25F);
  EXPECT_FLOAT_EQ(step.state.pitch_radians, 0.05F);
}

TEST(WebPlayerMovementTests, RenderLookCanBeAppliedWithoutPhysicsDisplacement) {
  arv::web::WebPlayerMovementState state = supported_player_state();

  arv::web::apply_web_player_look(state, 100.0F, -20.0F);

  EXPECT_EQ(state.position, (arv::Vec3f{8.0F, 1.0F, 8.0F}));
  EXPECT_FLOAT_EQ(state.yaw_radians, 0.25F);
  EXPECT_FLOAT_EQ(state.pitch_radians, 0.05F);
}

TEST(WebPlayerMovementTests, AirbornePlayerFallsWithSharedGravity) {
  const auto section = make_section();
  arv::web::WebPlayerMovementState state;
  state.position = {8.0F, 5.0F, 8.0F};

  const arv::web::WebPlayerMovementStep step =
      arv::web::simulate_web_player_movement(
          state,
          {},
          std::span<const arv::protocol::ClientboundChunkSectionData>{&section, 1},
          0.1);

  EXPECT_LT(step.state.velocity.y, 0.0F);
  EXPECT_LT(step.state.position.y, state.position.y);
  EXPECT_FALSE(arv::protocol::has_move_flag(
      step.flags,
      arv::protocol::PlayerMoveFlags::OnGround));
}

TEST(WebPlayerMovementTests, BrowserStrafeKeysUseDesktopStrafeDirection) {
  auto section = make_section();
  set_block(section, 8, 0, 8);
  const arv::web::WebPlayerMovementState state = supported_player_state();

  arv::web::WebPlayerMovementInput right;
  right.right = true;
  const arv::web::WebPlayerMovementStep right_step =
      arv::web::simulate_web_player_movement(
          state,
          right,
          std::span<const arv::protocol::ClientboundChunkSectionData>{&section, 1},
          0.1);

  arv::web::WebPlayerMovementInput left;
  left.left = true;
  const arv::web::WebPlayerMovementStep left_step =
      arv::web::simulate_web_player_movement(
          state,
          left,
          std::span<const arv::protocol::ClientboundChunkSectionData>{&section, 1},
          0.1);

  EXPECT_GT(right_step.state.position.x, state.position.x);
  EXPECT_LT(left_step.state.position.x, state.position.x);
}

TEST(WebPlayerMovementTests, WebPredictionMatchesDesktopControllerForStrafeStep) {
  auto section = make_section();
  set_block(section, 8, 0, 8);
  const std::span<const arv::protocol::ClientboundChunkSectionData> sections{&section, 1};
  const arv::Seconds tick_seconds = arv::tick_duration_seconds(arv::DefaultBaseTps);
  const arv::web::WebPlayerMovementState state = supported_player_state();

  arv::web::WebPlayerMovementInput web_input;
  web_input.right = true;
  const arv::web::WebPlayerMovementStep web_step =
      arv::web::simulate_web_player_movement(state, web_input, sections, tick_seconds);

  arv::client::LocalPlayerController desktop;
  desktop.enter_world(
      arv::WorldID{1},
      arv::PlayerID{2},
      arv::EntityNetID{3},
      state.position,
      0,
      arv::DefaultBaseTps);
  arv::protocol::ClientboundPlayerMoveCorrection grounded;
  grounded.position = state.position;
  grounded.velocity = state.velocity;
  grounded.yaw_radians = state.yaw_radians;
  grounded.pitch_radians = state.pitch_radians;
  grounded.flags = arv::protocol::PlayerMoveFlags::OnGround;
  desktop.apply_move_correction(grounded);
  desktop.submit_input(arv::client::InputFrame{.move_right = true});
  const arv::player::PlayerCommand command = desktop.consume_command(1);
  SectionCollisionQuery query(sections);
  desktop.update_local_movement(command, &query, tick_seconds);
  const arv::protocol::ServerboundPlayerMove desktop_move =
      desktop.build_movement_packet(
          1,
          1,
          arv::protocol::PlayerActionFlags::None);

  EXPECT_FLOAT_EQ(web_step.state.position.x, desktop_move.position.x);
  EXPECT_FLOAT_EQ(web_step.state.position.y, desktop_move.position.y);
  EXPECT_FLOAT_EQ(web_step.state.position.z, desktop_move.position.z);
  EXPECT_FLOAT_EQ(web_step.state.velocity.x, desktop_move.velocity.x);
  EXPECT_FLOAT_EQ(web_step.state.velocity.y, desktop_move.velocity.y);
  EXPECT_FLOAT_EQ(web_step.state.velocity.z, desktop_move.velocity.z);
  EXPECT_EQ(web_step.flags, desktop_move.flags);
}

TEST(WebPlayerMovementTests, ServerVelocityAppliesOnlyToLocalPlayer) {
  arv::web::WebPlayerMovementState state = supported_player_state();
  state.grounded = true;

  arv::protocol::ClientboundPlayerVelocity remote_velocity;
  remote_velocity.player_id = arv::PlayerID{99};
  remote_velocity.velocity = {1.0F, 4.0F, 0.0F};
  arv::web::apply_web_player_velocity(state, remote_velocity, arv::PlayerID{2});
  EXPECT_EQ(state.velocity, arv::Vec3f{});
  EXPECT_TRUE(state.grounded);

  arv::protocol::ClientboundPlayerVelocity local_velocity = remote_velocity;
  local_velocity.player_id = arv::PlayerID{2};
  arv::web::apply_web_player_velocity(state, local_velocity, arv::PlayerID{2});
  EXPECT_EQ(state.velocity, (arv::Vec3f{1.0F, 4.0F, 0.0F}));
  EXPECT_FALSE(state.grounded);
}

TEST(WebPlayerMovementTests, ServerPermissionsUpdateMovementAuthority) {
  arv::web::WebPlayerMovementState state = supported_player_state();
  state.movement_mode = arv::player::MovementMode::Flying;
  arv::physics::PlayerMovementAuthority authority{true, false};

  arv::protocol::ClientboundPlayerPermissions permissions;
  permissions.game_mode_id = "survival";
  permissions.core = arv::player::survival_core_permissions();
  arv::web::apply_web_player_permissions(state, authority, permissions);

  EXPECT_FALSE(authority.can_fly);
  EXPECT_TRUE(authority.has_collision);
  EXPECT_EQ(state.movement_mode, arv::player::MovementMode::Walking);
}

TEST(WebPlayerMovementTests, ServerMovementTuningUpdatesFlySpeeds) {
  arv::player::PlayerTuning tuning = arv::player::default_player_tuning();

  arv::web::apply_web_player_movement_tuning(
      tuning,
      arv::protocol::ClientboundPlayerMovementTuning{-1.0F, 0.0F});
  EXPECT_FLOAT_EQ(tuning.fly_speed, arv::player::default_player_tuning().fly_speed);
  EXPECT_FLOAT_EQ(
      tuning.fly_vertical_speed,
      arv::player::default_player_tuning().fly_vertical_speed);

  arv::web::apply_web_player_movement_tuning(
      tuning,
      arv::protocol::ClientboundPlayerMovementTuning{9.5F, 6.25F});
  EXPECT_FLOAT_EQ(tuning.fly_speed, 9.5F);
  EXPECT_FLOAT_EQ(tuning.fly_vertical_speed, 6.25F);
}
