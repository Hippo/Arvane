#include "desktop_render_builders.hpp"

#include <cmath>
#include <numbers>
#include <optional>

namespace arv::desktop {
namespace {

[[nodiscard]] Vec3f interpolate(Vec3f from, Vec3f to, f32 alpha) noexcept {
  return {
      from.x + (to.x - from.x) * alpha,
      from.y + (to.y - from.y) * alpha,
      from.z + (to.z - from.z) * alpha};
}

[[nodiscard]] Quatf multiply_quat(Quatf lhs, Quatf rhs) noexcept {
  return {
      lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
      lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
      lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
      lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z};
}

[[nodiscard]] Quatf normalize_quat(Quatf value) noexcept {
  const f32 length = std::sqrt(
      value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
  if (length <= 0.0F) {
    return {};
  }
  return {value.x / length, value.y / length, value.z / length, value.w / length};
}

[[nodiscard]] Quatf yaw_rotation(f32 radians) noexcept {
  const f32 half = radians * 0.5F;
  return {0.0F, std::sin(half), 0.0F, std::cos(half)};
}

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

void append_item_draw(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    player::ItemStack stack,
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

} // namespace

usize populate_chunk_draw_lists(render::FramePacket& frame, ChunkViewState& chunk_view) {
  const std::vector<render::ChunkDrawItem>& visible_chunks = chunk_draw_items(chunk_view);
  frame.opaque_chunks = visible_chunks;
  frame.transparent_chunks = visible_chunks;
  return visible_chunks.size();
}

void populate_world_frame_draw_items(
    render::FramePacket& frame,
    const DesktopGameSession& session,
    const EntityRenderAssets& entity_render_assets,
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::time_point app_start_time,
    f32 interpolation_alpha) {
  const f32 entity_animation_time =
      std::chrono::duration<f32>(now - app_start_time).count();

  for (const client::ReplicatedEntityMirror& mirror :
       session.client->replicated_entity_mirrors()) {
    if (mirror.world_id != frame.world_view.world_id) {
      continue;
    }
    const auto model = entity_render_assets.model_by_type.find(mirror.entity_type);
    if (model == entity_render_assets.model_by_type.end()) {
      continue;
    }
    render::EntityDrawItem item;
    item.model_id = model->second;
    item.position = interpolate(mirror.previous_position, mirror.position, interpolation_alpha);
    item.rotation = mirror.rotation;
    item.scale = 1.0F;
    item.animation_name = "walking";
    item.animation_time_seconds = entity_animation_time;
    frame.entities.push_back(std::move(item));
  }

  if (session.integrated_server != nullptr) {
    const std::vector<server::DroppedItemStack>& dropped_items =
        session.integrated_server->dropped_item_stacks();
    for (usize dropped_index = 0; dropped_index < dropped_items.size(); ++dropped_index) {
      const server::DroppedItemStack& dropped = dropped_items[dropped_index];
      if (dropped.world_id != frame.world_view.world_id || dropped.stack.count == 0U) {
        continue;
      }
      const f32 phase =
          entity_animation_time * 2.75F + static_cast<f32>(dropped_index) * 0.37F;
      append_item_draw(
          frame,
          entity_render_assets,
          dropped.stack,
          {dropped.position.x,
           dropped.position.y + 0.42F + std::sin(phase) * 0.055F,
           dropped.position.z},
          yaw_rotation(entity_animation_time * 1.8F + static_cast<f32>(dropped_index)),
          0.42F);
    }
  }

  if (!session.client->has_authoritative_inventory()) {
    return;
  }
  const usize selected_slot = session.client->selected_hotbar_slot();
  const player::Inventory& inventory = session.client->inventory();
  const std::optional<player::ItemStack>& held_stack =
      selected_slot < inventory.slot_count() ? inventory.slot(selected_slot)
                                             : std::optional<player::ItemStack>{};
  if (!held_stack.has_value() || held_stack->count == 0U) {
    return;
  }

  const render::CameraSnapshot& camera = frame.camera;
  constexpr f32 HalfPi = std::numbers::pi_v<f32> * 0.5F;
  append_item_draw(
      frame,
      entity_render_assets,
      held_stack.value(),
      camera.position + camera.forward * 0.70F + camera.right * 0.34F -
          camera.up * 0.30F,
      multiply_quat(
          camera_basis_rotation(camera.right, camera.up, camera.forward),
          yaw_rotation(-HalfPi)),
      0.42F);
}

} // namespace arv::desktop
