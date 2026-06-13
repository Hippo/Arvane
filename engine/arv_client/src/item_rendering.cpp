#include "arv/client/item_rendering.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace arv::client {
namespace {

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

void append_held_item_draw(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const player::ItemStack& stack,
    const render::CameraSnapshot& camera,
    HeldItemAnimationPose animation_pose) {
  constexpr f32 HalfPi = PiRadians * 0.5F;
  const Vec3f held_position =
      camera.position + camera.forward * (0.70F + animation_pose.position.z) +
      camera.right * (0.34F + animation_pose.position.x) -
      camera.up * (0.30F - animation_pose.position.y);
  const Quatf base_rotation = multiply_quat(
      camera_basis_rotation(camera.right, camera.up, camera.forward),
      yaw_rotation(-HalfPi));
  const Quatf held_rotation = multiply_quat(
      base_rotation,
      euler_degrees_rotation(animation_pose.rotation_degrees));
  const f32 animation_scale = std::max(
      (animation_pose.scale.x + animation_pose.scale.y + animation_pose.scale.z) / 3.0F,
      0.0F);
  append_item_draw(
      frame,
      entity_render_assets,
      stack,
      held_position,
      held_rotation,
      0.42F * animation_scale);
}

} // namespace arv::client
