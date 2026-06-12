#include "web_player_movement.hpp"

#include "arv/voxel/storage.hpp"

#include <algorithm>
#include <cmath>

namespace arv::web {
namespace {

class WebChunkCollisionQuery final : public physics::ICollisionQuery {
public:
  explicit WebChunkCollisionQuery(
      std::span<const protocol::ClientboundChunkSectionData> chunk_sections) noexcept
      : chunk_sections_(chunk_sections) {
  }

  [[nodiscard]] bool is_block_loaded(BlockPos pos) const noexcept override {
    return section_for(pos) != nullptr;
  }

  [[nodiscard]] voxel::BlockVariantID block_variant(BlockPos pos) const noexcept override {
    const auto* section = section_for(pos);
    if (section == nullptr) {
      return voxel::AirBlockVariantID;
    }
    return section->blocks[voxel::local_index(block_to_local(pos))];
  }

  [[nodiscard]] voxel::FluidCell fluid_cell(BlockPos pos) const noexcept override {
    const auto* section = section_for(pos);
    if (section == nullptr || !section->has_fluids) {
      return voxel::empty_fluid_cell();
    }
    return section->fluids[voxel::local_index(block_to_local(pos))];
  }

  [[nodiscard]] const voxel::BlockBody* block_body(BlockPos pos) const noexcept override {
    return block_variant(pos) == voxel::AirBlockVariantID ? nullptr : &full_cube_body();
  }

  [[nodiscard]] bool missing_blocks_are_solid() const noexcept override {
    return true;
  }

private:
  [[nodiscard]] const protocol::ClientboundChunkSectionData* section_for(
      BlockPos pos) const noexcept {
    const ChunkSectionCoord coord = block_to_chunk(pos);
    const auto found = std::find_if(
        chunk_sections_.begin(),
        chunk_sections_.end(),
        [&](const protocol::ClientboundChunkSectionData& section) {
          return section.coord == coord;
        });
    return found == chunk_sections_.end() ? nullptr : &*found;
  }

  [[nodiscard]] static const voxel::BlockBody& full_cube_body() noexcept {
    static const voxel::BlockBody body{{
        Aabb3f{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}}}};
    return body;
  }

  std::span<const protocol::ClientboundChunkSectionData> chunk_sections_;
};

[[nodiscard]] i16 clamped_mouse_delta(f32 delta) noexcept {
  return static_cast<i16>(std::clamp(delta, -32768.0F, 32767.0F));
}

[[nodiscard]] player::PlayerCommand command_from_web_input(
    WebPlayerMovementInput input) noexcept {
  player::PlayerCommand command;
  command.move_x = static_cast<i16>((input.right ? 1 : 0) - (input.left ? 1 : 0));
  command.move_z = static_cast<i16>((input.forward ? 1 : 0) - (input.back ? 1 : 0));
  command.look_delta_x = clamped_mouse_delta(input.mouse_dx);
  command.look_delta_y = clamped_mouse_delta(input.mouse_dy);
  command.jump_pressed = input.jump;
  command.jump_held = input.jump;
  command.sneak_held = input.sneak;
  command.sprint_held = input.sprint;
  command.toggle_fly_pressed = input.toggle_fly_pressed;
  return command;
}

[[nodiscard]] WebPlayerMovementState state_from_physics(
    const physics::PlayerKinematicState& state) noexcept {
  return {
      state.position,
      state.velocity,
      state.yaw_radians,
      state.pitch_radians,
      state.grounded,
      state.sneaking,
      state.swimming,
      state.breath_seconds,
      state.movement_mode};
}

[[nodiscard]] physics::PlayerKinematicState state_to_physics(
    WebPlayerMovementState state) noexcept {
  return {
      state.position,
      state.velocity,
      state.yaw_radians,
      state.pitch_radians,
      state.grounded,
      state.sneaking,
      state.swimming,
      state.breath_seconds,
      state.movement_mode};
}

} // namespace

WebPlayerMovementStep simulate_web_player_movement(
    WebPlayerMovementState state,
    WebPlayerMovementInput input,
    std::span<const protocol::ClientboundChunkSectionData> chunk_sections,
    Seconds tick_seconds,
    const player::PlayerTuning& tuning,
    physics::PlayerMovementAuthority authority) {
  WebChunkCollisionQuery query(chunk_sections);
  const player::PlayerCommand command = command_from_web_input(input);
  const physics::PlayerMovementStep step = physics::simulate_player_movement_tick(
      &query,
      state_to_physics(state),
      command,
      tuning,
      authority,
      std::max<Seconds>(tick_seconds, 0.0));

  WebPlayerMovementStep result;
  result.state = state_from_physics(step.state);
  result.flags = web_player_move_flags(result.state, input);
  return result;
}

void apply_web_player_move_correction(
    WebPlayerMovementState& state,
    const protocol::ClientboundPlayerMoveCorrection& correction) noexcept {
  state.position = correction.position;
  state.velocity = correction.velocity;
  state.yaw_radians = correction.yaw_radians;
  state.pitch_radians = correction.pitch_radians;
  state.grounded =
      protocol::has_move_flag(correction.flags, protocol::PlayerMoveFlags::OnGround);
  state.sneaking =
      protocol::has_move_flag(correction.flags, protocol::PlayerMoveFlags::Sneaking);
  state.swimming =
      protocol::has_move_flag(correction.flags, protocol::PlayerMoveFlags::Swimming);
  state.movement_mode =
      protocol::has_move_flag(correction.flags, protocol::PlayerMoveFlags::Flying)
          ? player::MovementMode::Flying
          : player::MovementMode::Walking;
}

void apply_web_player_velocity(
    WebPlayerMovementState& state,
    const protocol::ClientboundPlayerVelocity& velocity,
    PlayerID local_player_id) noexcept {
  if (velocity.player_id != local_player_id) {
    return;
  }
  state.velocity = velocity.velocity;
  if (state.velocity.y > 0.0F) {
    state.grounded = false;
  }
}

void apply_web_player_permissions(
    WebPlayerMovementState& state,
    physics::PlayerMovementAuthority& authority,
    const protocol::ClientboundPlayerPermissions& permissions) noexcept {
  authority.can_fly = permissions.core.can_fly;
  authority.has_collision = permissions.core.has_collision;
  if (!authority.can_fly && state.movement_mode == player::MovementMode::Flying) {
    state.movement_mode = player::MovementMode::Walking;
  }
}

void apply_web_player_movement_tuning(
    player::PlayerTuning& tuning,
    const protocol::ClientboundPlayerMovementTuning& movement_tuning) noexcept {
  if (std::isfinite(movement_tuning.fly_speed) && movement_tuning.fly_speed > 0.0F) {
    tuning.fly_speed = movement_tuning.fly_speed;
  }
  if (std::isfinite(movement_tuning.fly_vertical_speed) &&
      movement_tuning.fly_vertical_speed > 0.0F) {
    tuning.fly_vertical_speed = movement_tuning.fly_vertical_speed;
  }
}

void apply_web_player_look(
    WebPlayerMovementState& state,
    f32 mouse_dx,
    f32 mouse_dy,
    const player::PlayerTuning& tuning) noexcept {
  state.yaw_radians =
      wrap_radians(state.yaw_radians + mouse_dx * tuning.mouse_sensitivity_radians);
  state.pitch_radians = std::clamp(
      state.pitch_radians - mouse_dy * tuning.mouse_sensitivity_radians,
      -player::DefaultMaxPitchRadians,
      player::DefaultMaxPitchRadians);
}

protocol::PlayerMoveFlags web_player_move_flags(
    const WebPlayerMovementState& state,
    const WebPlayerMovementInput& input) noexcept {
  protocol::PlayerMoveFlags flags = protocol::PlayerMoveFlags::None;
  if (state.grounded) {
    flags |= protocol::PlayerMoveFlags::OnGround;
  }
  if (input.jump) {
    flags |= protocol::PlayerMoveFlags::Jumping;
  }
  if (state.sneaking) {
    flags |= protocol::PlayerMoveFlags::Sneaking;
  }
  if (input.sprint) {
    flags |= protocol::PlayerMoveFlags::Sprinting;
  }
  if (state.swimming) {
    flags |= protocol::PlayerMoveFlags::Swimming;
  }
  if (state.movement_mode == player::MovementMode::Flying) {
    flags |= protocol::PlayerMoveFlags::Flying;
  }
  return flags;
}

f32 web_player_eye_height(
    const WebPlayerMovementState& state,
    const player::PlayerTuning& tuning) noexcept {
  if (state.swimming) {
    return tuning.swimming_eye_height;
  }
  return state.sneaking ? tuning.sneaking_eye_height : tuning.standing_eye_height;
}

} // namespace arv::web
