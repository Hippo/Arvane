#include "arv/client/item_rendering.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace arv::client {
namespace {

constexpr f32 FirstPersonEyeToBodyOriginY = 1.80F;
constexpr f32 FirstPersonBaseBackOffset = 0.42F;
constexpr f32 FirstPersonSneakBackOffset = 0.30F;
constexpr f32 ModelUnitsToWorld = 1.0F / 16.0F;
constexpr f32 FirstPersonHeldItemForwardOffset = 0.38F;
constexpr f32 FirstPersonHeldItemRightOffset = 0.03F;
constexpr f32 FirstPersonHeldItemUpOffset = 0.18F;
constexpr f32 FirstPersonArmPitchFollow = 0.45F;
constexpr Vec3f FirstPersonRightArmPivot{-6.0F, 24.0F, 1.0F};
constexpr Vec3f FirstPersonRightHandGripOffset{0.0F, -12.0F, 0.0F};
constexpr Vec3f FirstPersonRightArmHoldRotation{72.0F, 10.0F, -10.0F};

[[nodiscard]] Quatf camera_basis_rotation(
    Vec3f right,
    Vec3f up,
    Vec3f forward) noexcept {
  const f32 m00 = right.x;
  const f32 m01 = up.x;
  const f32 m02 = forward.x;
  const f32 m10 = right.y;
  const f32 m11 = up.y;
  const f32 m12 = forward.y;
  const f32 m20 = right.z;
  const f32 m21 = up.z;
  const f32 m22 = forward.z;
  const f32 trace = m00 + m11 + m22;

  if (trace > 0.0F) {
    const f32 scale = std::sqrt(trace + 1.0F) * 2.0F;
    return normalize_quat({(m21 - m12) / scale,
                           (m02 - m20) / scale,
                           (m10 - m01) / scale,
                           0.25F * scale});
  }
  if (m00 > m11 && m00 > m22) {
    const f32 scale = std::sqrt(1.0F + m00 - m11 - m22) * 2.0F;
    return normalize_quat({0.25F * scale,
                           (m01 + m10) / scale,
                           (m02 + m20) / scale,
                           (m21 - m12) / scale});
  }
  if (m11 > m22) {
    const f32 scale = std::sqrt(1.0F + m11 - m00 - m22) * 2.0F;
    return normalize_quat({(m01 + m10) / scale,
                           0.25F * scale,
                           (m12 + m21) / scale,
                           (m02 - m20) / scale});
  }

  const f32 scale = std::sqrt(1.0F + m22 - m00 - m11) * 2.0F;
  return normalize_quat({(m02 + m20) / scale,
                         (m12 + m21) / scale,
                         0.25F * scale,
                         (m10 - m01) / scale});
}

[[nodiscard]] Vec3f rotate_euler_degrees_local(Vec3f value, Vec3f degrees) noexcept {
  const Vec3f radians{
      degrees_to_radians(degrees.x),
      degrees_to_radians(degrees.y),
      degrees_to_radians(degrees.z)};
  const f32 cx = std::cos(radians.x);
  const f32 sx = std::sin(radians.x);
  const f32 cy = std::cos(radians.y);
  const f32 sy = std::sin(radians.y);
  const f32 cz = std::cos(radians.z);
  const f32 sz = std::sin(radians.z);

  value = {value.x, value.y * cx - value.z * sx, value.y * sx + value.z * cx};
  value = {value.x * cy + value.z * sy, value.y, -value.x * sy + value.z * cy};
  value = {value.x * cz - value.y * sz, value.x * sz + value.y * cz, value.z};
  return value;
}

[[nodiscard]] Vec3f camera_horizontal_forward(
    const render::CameraSnapshot& camera) noexcept {
  const Vec3f forward{camera.forward.x, 0.0F, camera.forward.z};
  const f32 forward_length = length(forward);
  if (forward_length <= 0.0001F) {
    return {0.0F, 0.0F, 1.0F};
  }
  return forward * (1.0F / forward_length);
}

[[nodiscard]] f32 camera_yaw_radians(
    const render::CameraSnapshot& camera) noexcept {
  const Vec3f forward = camera_horizontal_forward(camera);
  return std::atan2(forward.x, forward.z);
}

[[nodiscard]] f32 camera_pitch_degrees(
    const render::CameraSnapshot& camera) noexcept {
  return radians_to_degrees(
      std::asin(std::clamp(camera.forward.y, -1.0F, 1.0F)));
}

[[nodiscard]] Vec3f first_person_right_arm_rotation(
    const render::CameraSnapshot& camera,
    HeldItemAnimationPose animation_pose) noexcept {
  return {
      FirstPersonRightArmHoldRotation.x -
          animation_pose.rotation_degrees.x * 0.55F +
          camera_pitch_degrees(camera) * FirstPersonArmPitchFollow,
      FirstPersonRightArmHoldRotation.y +
          animation_pose.rotation_degrees.y * 0.35F,
      FirstPersonRightArmHoldRotation.z +
          animation_pose.rotation_degrees.z * 0.25F};
}

[[nodiscard]] Vec3f block_center_position(BlockPos block) noexcept {
  return {
      static_cast<f32>(block.value.x) + 0.5F,
      static_cast<f32>(block.value.y) + 0.5F,
      static_cast<f32>(block.value.z) + 0.5F};
}

[[nodiscard]] f32 approach(f32 current, f32 target, f32 step) noexcept {
  if (current < target) {
    return std::min(current + step, target);
  }
  return std::max(current - step, target);
}

} // namespace

void append_item_draw(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const player::ItemStack& stack,
    Vec3f position,
    Quatf rotation,
    f32 scale) {
  const auto model = entity_render_assets.model_by_item.find(stack.item);
  if (model == entity_render_assets.model_by_item.end()) {
    return;
  }
  render::EntityDrawItem item;
  item.model_id = model->second;
  item.position = position;
  item.rotation = rotation;
  item.scale = scale;
  frame.entities.push_back(std::move(item));
}

void append_dropped_item_draws(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    std::span<const DroppedItemMirror> dropped_items,
    WorldID world_id,
    Seconds animation_time_seconds) {
  for (usize dropped_index = 0; dropped_index < dropped_items.size(); ++dropped_index) {
    const DroppedItemMirror& dropped = dropped_items[dropped_index];
    if (dropped.world_id != world_id || dropped.stack.count == 0U) {
      continue;
    }
    const f32 phase =
        animation_time_seconds * 2.75F + static_cast<f32>(dropped_index) * 0.37F;
    append_item_draw(
        frame,
        entity_render_assets,
        dropped.stack,
        {dropped.render_position.x,
         dropped.render_position.y + 0.42F + std::sin(phase) * 0.055F,
         dropped.render_position.z},
        yaw_rotation(animation_time_seconds * 1.8F + static_cast<f32>(dropped_index)),
        0.42F);
  }
}

Vec3f first_person_player_body_position(
    const render::CameraSnapshot& camera,
    f32 sneak_progress) noexcept {
  const f32 clamped_sneak = std::clamp(sneak_progress, 0.0F, 1.0F);
  Vec3f position =
      camera.position -
      camera_horizontal_forward(camera) *
          (FirstPersonBaseBackOffset + FirstPersonSneakBackOffset * clamped_sneak);
  position.y -= FirstPersonEyeToBodyOriginY;
  return position;
}

Quatf first_person_player_body_rotation(
    const render::CameraSnapshot& camera) noexcept {
  return model_forward_yaw_rotation(camera_yaw_radians(camera));
}

f32 first_person_sneak_target_progress(
    std::span<const protocol::EntityAnimationPlayback> animations) noexcept {
  for (const protocol::EntityAnimationPlayback& animation : animations) {
    if (animation.name == "sneak") {
      return std::clamp(animation.time_seconds / 0.25F, 0.0F, 1.0F);
    }
  }
  for (const protocol::EntityAnimationPlayback& animation : animations) {
    if (animation.name == "sneak_idle" ||
        animation.name == "sneak_walk") {
      return 1.0F;
    }
  }
  return 0.0F;
}

f32 update_first_person_sneak_visual_progress(
    FirstPersonSneakVisualState& state,
    std::span<const protocol::EntityAnimationPlayback> animations,
    Seconds now_seconds) noexcept {
  const f32 target = first_person_sneak_target_progress(animations);
  if (!state.initialized) {
    state.initialized = true;
    state.updated_at = now_seconds;
    state.progress = target;
    return state.progress;
  }

  const f32 delta_seconds =
      std::max(static_cast<f32>(now_seconds - state.updated_at), 0.0F);
  state.updated_at = now_seconds;
  const f32 speed = target > state.progress ? 4.0F : 5.0F;
  state.progress = approach(state.progress, target, delta_seconds * speed);
  return state.progress;
}

FirstPersonHeldItemPose first_person_held_item_pose(
    const render::CameraSnapshot& camera,
    HeldItemAnimationPose animation_pose,
    f32 sneak_progress) noexcept {
  constexpr f32 HalfPi = PiRadians * 0.5F;
  FirstPersonHeldItemPose pose;
  pose.body_position = first_person_player_body_position(camera, sneak_progress);
  pose.body_rotation = first_person_player_body_rotation(camera);
  pose.hand_rotation_override = {
      "right_arm",
      first_person_right_arm_rotation(camera, animation_pose),
      true};

  const Vec3f hand_model_position =
      FirstPersonRightArmPivot +
      rotate_euler_degrees_local(
          FirstPersonRightHandGripOffset,
          pose.hand_rotation_override.rotation_degrees);
  const Vec3f hand_world_position =
      pose.body_position +
      rotate_vector(
          pose.body_rotation,
          hand_model_position * ModelUnitsToWorld);

  pose.item_position =
      hand_world_position +
      camera.forward * (FirstPersonHeldItemForwardOffset + animation_pose.position.z) +
      camera.right * (FirstPersonHeldItemRightOffset + animation_pose.position.x) +
      camera.up * (FirstPersonHeldItemUpOffset + animation_pose.position.y);
  const Quatf base_rotation = multiply_quat(
      camera_basis_rotation(camera.right, camera.up, camera.forward),
      yaw_rotation(-HalfPi));
  pose.item_rotation = multiply_quat(
      base_rotation,
      euler_degrees_rotation(animation_pose.rotation_degrees));
  const f32 animation_scale = std::max(
      (animation_pose.scale.x + animation_pose.scale.y + animation_pose.scale.z) / 3.0F,
      0.0F);
  pose.item_scale = 0.42F * animation_scale;
  return pose;
}

void apply_first_person_held_item_arm_pose(
    render::EntityDrawItem& player_draw,
    const render::CameraSnapshot& camera,
    HeldItemAnimationPose animation_pose) {
  player_draw.bone_rotation_overrides.push_back(
      {"right_arm", first_person_right_arm_rotation(camera, animation_pose), true});
}

PlayerEntityModelSelection select_player_entity_model(
    const EntityRenderAssets& entity_render_assets,
    const PlayerEntityModelSelectionInput& input) noexcept {
  PlayerEntityModelSelection selection;
  selection.model_id = input.base_model_id;

  if (input.slim_model &&
      entity_render_assets.player_slim_model.value != 0U) {
    selection.model_id = entity_render_assets.player_slim_model;
  }

  bool using_first_person_skin = false;
  if (input.ready_player_skin_region.has_value() &&
      input.ready_player_skin_region->valid()) {
    selection.player_skin_region = input.ready_player_skin_region.value();
    if (input.slim_model &&
        entity_render_assets.player_slim_skin_model.value != 0U) {
      selection.model_id = entity_render_assets.player_slim_skin_model;
    } else if (entity_render_assets.player_skin_model.value != 0U) {
      selection.model_id = entity_render_assets.player_skin_model;
    }
    if (input.first_person && input.slim_model &&
        entity_render_assets.first_person_player_slim_skin_model.value != 0U) {
      selection.model_id =
          entity_render_assets.first_person_player_slim_skin_model;
      using_first_person_skin = true;
    } else if (input.first_person &&
               entity_render_assets.first_person_player_skin_model.value != 0U) {
      selection.model_id = entity_render_assets.first_person_player_skin_model;
      using_first_person_skin = true;
    }
  }

  if (input.first_person &&
      entity_render_assets.first_person_player_model.value != 0U &&
      !using_first_person_skin) {
    if (input.slim_model &&
        entity_render_assets.first_person_player_slim_model.value != 0U) {
      selection.model_id = entity_render_assets.first_person_player_slim_model;
    } else {
      selection.model_id = entity_render_assets.first_person_player_model;
    }
  }

  return selection;
}

render::EntityDrawItem make_replicated_entity_draw_item(
    const EntityRenderAssets& entity_render_assets,
    const ReplicatedEntityDrawInput& input) {
  render::EntityDrawItem item;
  item.model_id = input.model_id;
  if (const auto animation =
          entity_render_assets.animation_by_type.find(input.entity_type);
      animation != entity_render_assets.animation_by_type.end()) {
    item.animation_id = animation->second;
  }
  item.position =
      input.first_person
          ? first_person_player_body_position(
                input.first_person_camera,
                input.first_person_sneak_progress)
          : input.render_position;
  item.rotation =
      input.first_person
          ? first_person_player_body_rotation(input.first_person_camera)
          : input.render_rotation;
  item.scale = 1.0F;
  item.animations.reserve(input.animations.size());
  for (const protocol::EntityAnimationPlayback& animation : input.animations) {
    item.animations.push_back({animation.name, animation.time_seconds});
  }
  item.player_skin_region = input.player_skin_region;
  if (input.has_owning_player) {
    item.bone_rotation_overrides.push_back({"head", input.head_rotation_degrees});
  }
  if (input.first_person && input.apply_held_item_arm_pose) {
    apply_first_person_held_item_arm_pose(
        item,
        input.first_person_camera,
        input.held_item_pose);
  }
  return item;
}

ReplicatedEntityFrameResult append_replicated_entity_draws(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const ReplicatedEntityFrameInput& input) {
  ReplicatedEntityFrameResult result;
  result.first_person_sneak_progress = input.first_person_sneak_progress;

  for (const ReplicatedEntityMirror& mirror : input.mirrors) {
    if (mirror.world_id != input.world_id) {
      continue;
    }
    const auto model =
        entity_render_assets.model_by_type.find(mirror.entity_type);
    if (model == entity_render_assets.model_by_type.end()) {
      continue;
    }

    const bool is_local_player =
        (input.local_entity_id.has_value() &&
         mirror.entity_id == input.local_entity_id.value()) ||
        (input.local_player_id.has_value() &&
         mirror.owning_player.has_value() &&
         mirror.owning_player.value() == input.local_player_id.value());
    if (is_local_player && input.first_person_sneak_visual != nullptr) {
      result.first_person_sneak_progress =
          update_first_person_sneak_visual_progress(
              *input.first_person_sneak_visual,
              mirror.animations,
              input.animation_time_seconds);
      if (!input.show_local_player_body) {
        continue;
      }
    }

    PlayerEntitySkinInfo skin_info;
    if (mirror.owning_player.has_value() && input.callbacks.player_skin_info) {
      const std::optional<PlayerEntitySkinInfo> found =
          input.callbacks.player_skin_info(mirror.owning_player.value());
      if (found.has_value()) {
        skin_info = found.value();
      }
    }

    std::optional<render::TextureRegionHandle> ready_skin;
    if (!skin_info.skin_hash.empty() && input.callbacks.ready_player_skin) {
      ready_skin = input.callbacks.ready_player_skin(skin_info.skin_hash);
    }

    const PlayerEntityModelSelection model_selection =
        select_player_entity_model(
            entity_render_assets,
            {.base_model_id = model->second,
             .slim_model = skin_info.slim_model,
             .first_person = is_local_player,
             .ready_player_skin_region = ready_skin});

    frame.entities.push_back(make_replicated_entity_draw_item(
        entity_render_assets,
        {.entity_type = mirror.entity_type,
         .render_position = mirror.render_position,
         .render_rotation = mirror.render_rotation,
         .animations = mirror.animations,
         .head_rotation_degrees = mirror.head_rotation_degrees,
         .has_owning_player = mirror.owning_player.has_value(),
         .model_id = model_selection.model_id,
         .player_skin_region = model_selection.player_skin_region,
         .first_person = is_local_player,
         .first_person_camera = input.first_person_camera,
         .first_person_sneak_progress =
             result.first_person_sneak_progress,
         .apply_held_item_arm_pose =
             is_local_player && input.apply_local_held_item_arm_pose,
         .held_item_pose = input.held_item_pose}));
    ++result.appended;
  }

  return result;
}

void append_held_item_draw(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const player::ItemStack& stack,
    const render::CameraSnapshot& camera,
    HeldItemAnimationPose animation_pose,
    f32 sneak_progress) {
  const FirstPersonHeldItemPose pose =
      first_person_held_item_pose(camera, animation_pose, sneak_progress);
  append_item_draw(
      frame,
      entity_render_assets,
      stack,
      pose.item_position,
      pose.item_rotation,
      pose.item_scale);
}

bool block_variant_has_attached_model(
    const EntityRenderAssets& entity_render_assets,
    voxel::BlockVariantID variant_id) noexcept {
  return variant_id.value <
             entity_render_assets.block_attached_model_by_variant.size() &&
         entity_render_assets.block_attached_model_by_variant[variant_id.value].value !=
             0U;
}

void collect_block_attached_model_section_entries(
    const EntityRenderAssets& entity_render_assets,
    std::span<const voxel::BlockVariantID> blocks,
    std::vector<BlockAttachedModelSectionEntry>& out) {
  out.clear();
  const usize count = std::min(blocks.size(), voxel::ChunkVolume);
  for (usize index = 0; index < count; ++index) {
    const voxel::BlockVariantID variant_id = blocks[index];
    if (!block_variant_has_attached_model(entity_render_assets, variant_id)) {
      continue;
    }
    out.push_back({voxel::local_pos(index), variant_id});
  }
}

void append_block_attached_model_draw(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    voxel::BlockVariantID variant_id,
    BlockPos block,
    const BlockContainerAnimationState* container_animation,
    Seconds animation_time_seconds) {
  if (variant_id.value >=
      entity_render_assets.block_attached_model_by_variant.size()) {
    return;
  }
  const render::EntityModelID model_id =
      entity_render_assets.block_attached_model_by_variant[variant_id.value];
  if (model_id.value == 0U) {
    return;
  }

  render::EntityDrawItem item;
  item.model_id = model_id;
  if (variant_id.value <
      entity_render_assets.block_attached_animation_by_variant.size()) {
    item.animation_id =
        entity_render_assets.block_attached_animation_by_variant[variant_id.value];
  }
  item.position = block_floor_center(block);
  if (variant_id.value <
      entity_render_assets.block_attached_rotation_by_variant.size()) {
    item.rotation =
        entity_render_assets.block_attached_rotation_by_variant[variant_id.value];
  }
  item.scale = 1.0F;
  if (container_animation != nullptr && item.animation_id.value != 0U) {
    append_block_container_animation_playback(
        item,
        *container_animation,
        protocol::ContainerBlockSource{frame.world_view.world_id, block},
        animation_time_seconds);
  }
  frame.entities.push_back(std::move(item));
}

usize append_block_attached_model_draws_for_section_entries(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    ChunkSectionCoord coord,
    std::span<const BlockAttachedModelSectionEntry> entries,
    const BlockContainerAnimationState* container_animation,
    Seconds animation_time_seconds) {
  const usize before = frame.entities.size();
  for (const BlockAttachedModelSectionEntry& entry : entries) {
    append_block_attached_model_draw(
        frame,
        entity_render_assets,
        entry.variant_id,
        chunk_local_to_block(coord, entry.local),
        container_animation,
        animation_time_seconds);
  }
  return frame.entities.size() - before;
}

void append_block_outline_draw(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const BlockOutlineTarget& target) {
  if (target.variant.value >= entity_render_assets.outline_model_by_variant.size()) {
    return;
  }
  const render::EntityModelID model_id =
      entity_render_assets.outline_model_by_variant[target.variant.value];
  if (model_id.value == 0U) {
    return;
  }

  render::EntityDrawItem item;
  item.model_id = model_id;
  item.position = block_center_position(target.block);
  item.scale = 1.0F;
  frame.entities.push_back(std::move(item));
}

void append_block_break_overlay_draw(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const protocol::BlockBreakProgress& block_break) {
  if (!block_break.active ||
      block_break.variant.value >=
          entity_render_assets.break_overlay_model_by_variant.size() ||
      block_break.stage >= BreakStageTextureCount) {
    return;
  }
  const render::EntityModelID model_id =
      entity_render_assets
          .break_overlay_model_by_variant[block_break.variant.value][block_break.stage];
  if (model_id.value == 0U) {
    return;
  }

  render::EntityDrawItem item;
  item.model_id = model_id;
  item.position = block_center_position(block_break.block);
  item.scale = 1.0F;
  frame.entities.push_back(std::move(item));
}

} // namespace arv::client
