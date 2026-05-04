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

[[nodiscard]] Quatf pitch_rotation(f32 radians) noexcept {
  const f32 half = radians * 0.5F;
  return {std::sin(half), 0.0F, 0.0F, std::cos(half)};
}

[[nodiscard]] Quatf roll_rotation(f32 radians) noexcept {
  const f32 half = radians * 0.5F;
  return {0.0F, 0.0F, std::sin(half), std::cos(half)};
}

[[nodiscard]] f32 degrees_to_radians(f32 degrees) noexcept {
  return degrees * std::numbers::pi_v<f32> / 180.0F;
}

[[nodiscard]] Quatf euler_degrees_rotation(Vec3f degrees) noexcept {
  return multiply_quat(
      pitch_rotation(degrees_to_radians(degrees.x)),
      multiply_quat(
          yaw_rotation(degrees_to_radians(degrees.y)),
          roll_rotation(degrees_to_radians(degrees.z))));
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

[[nodiscard]] render::SkyUniforms sky_uniforms_from_clock(
    const world::WorldClock& clock) noexcept {
  const world::CelestialState celestial = world::build_celestial_state(clock);
  render::SkyUniforms uniforms;
  uniforms.enabled = true;
  uniforms.sun_direction = celestial.sun_direction;
  uniforms.daylight_strength = celestial.daylight_strength;
  uniforms.moon_direction = celestial.moon_direction;
  uniforms.moonlight_strength = celestial.moonlight_strength;
  uniforms.daylight_color = celestial.daylight_color;
  uniforms.star_visibility = celestial.star_visibility;
  uniforms.ambient_color = celestial.ambient_color;
  uniforms.day_fraction = celestial.day_fraction;
  uniforms.fog_color = celestial.fog_color;
  uniforms.cloud_time_seconds = world::day_time_seconds(clock);
  uniforms.cirrus_amount = 0.35F;
  uniforms.cumulus_amount = 0.60F;
  uniforms.exposure = 1.15F;
  return uniforms;
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
  const world::WorldRuntime& sky_world =
      session.integrated_server == nullptr
          ? session.client_world
          : session.integrated_server->voxel_world();
  frame.sky = sky_uniforms_from_clock(sky_world.clock());

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
    item.animation_name = mirror.animation_name;
    item.animation_time_seconds = mirror.animation_time_seconds;
    frame.entities.push_back(std::move(item));
  }

  const std::vector<client::DroppedItemMirror> dropped_items =
      session.client->dropped_item_mirrors();
  for (usize dropped_index = 0; dropped_index < dropped_items.size(); ++dropped_index) {
    const client::DroppedItemMirror& dropped = dropped_items[dropped_index];
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

  const protocol::BlockBreakProgress& block_break =
      session.client->block_break_progress();
  if (session.block_outline_target.has_value()) {
    const BlockOutlineTarget& target = session.block_outline_target.value();
    if (target.variant.value < entity_render_assets.outline_model_by_variant.size()) {
      const render::EntityModelID model_id =
          entity_render_assets.outline_model_by_variant[target.variant.value];
      if (model_id.value != 0U) {
        render::EntityDrawItem item;
        item.model_id = model_id;
        item.position = {
            static_cast<f32>(target.block.value.x) + 0.5F,
            static_cast<f32>(target.block.value.y) + 0.5F,
            static_cast<f32>(target.block.value.z) + 0.5F};
        item.scale = 1.0F;
        frame.entities.push_back(std::move(item));
      }
    }
  }

  if (block_break.active &&
      block_break.variant.value < entity_render_assets.break_overlay_model_by_variant.size() &&
      block_break.stage < BreakStageTextureCount) {
    const render::EntityModelID model_id =
        entity_render_assets
            .break_overlay_model_by_variant[block_break.variant.value][block_break.stage];
    if (model_id.value != 0U) {
      render::EntityDrawItem item;
      item.model_id = model_id;
      item.position = {
          static_cast<f32>(block_break.block.value.x) + 0.5F,
          static_cast<f32>(block_break.block.value.y) + 0.5F,
          static_cast<f32>(block_break.block.value.z) + 0.5F};
      item.scale = 1.0F;
      frame.entities.push_back(std::move(item));
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
  const f32 animation_time_seconds =
      std::chrono::duration<f32>(now - app_start_time).count();
  const client::HeldItemAnimationPose animation_pose =
      session.held_item_animation.sample(animation_time_seconds);
  // Animation position is local held-item space: X right, Y up, Z forward.
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
      held_stack.value(),
      held_position,
      held_rotation,
      0.42F * animation_scale);
}

} // namespace arv::desktop
