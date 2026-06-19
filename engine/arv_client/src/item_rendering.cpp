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

[[nodiscard]] Vec3f first_person_right_arm_rotation(
    HeldItemAnimationPose animation_pose) noexcept {
  return {
      FirstPersonRightArmHoldRotation.x -
          animation_pose.rotation_degrees.x * 0.55F,
      FirstPersonRightArmHoldRotation.y +
          animation_pose.rotation_degrees.y * 0.35F,
      FirstPersonRightArmHoldRotation.z +
          animation_pose.rotation_degrees.z * 0.25F};
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
      first_person_right_arm_rotation(animation_pose),
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
      camera.forward * (0.26F + animation_pose.position.z) +
      camera.right * (0.02F + animation_pose.position.x) +
      camera.up * (0.20F + animation_pose.position.y);
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
    HeldItemAnimationPose animation_pose) {
  player_draw.bone_rotation_overrides.push_back(
      {"right_arm", first_person_right_arm_rotation(animation_pose), true});
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

} // namespace arv::client
