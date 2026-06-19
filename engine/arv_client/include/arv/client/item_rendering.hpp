#pragma once

#include "arv/client/dropped_item_mirror.hpp"
#include "arv/client/entity_render_assets.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/render/render_types.hpp"

#include <span>

namespace arv::client {

struct FirstPersonHeldItemPose {
  Vec3f body_position{};
  Quatf body_rotation{};
  Vec3f item_position{};
  Quatf item_rotation{};
  f32 item_scale{1.0F};
  render::EntityBoneRotationOverride hand_rotation_override{};
};

void append_item_draw(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const player::ItemStack& stack,
    Vec3f position,
    Quatf rotation,
    f32 scale);

void append_dropped_item_draws(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    std::span<const DroppedItemMirror> dropped_items,
    WorldID world_id,
    Seconds animation_time_seconds);

[[nodiscard]] Vec3f first_person_player_body_position(
    const render::CameraSnapshot& camera,
    f32 sneak_progress = 0.0F) noexcept;

[[nodiscard]] Quatf first_person_player_body_rotation(
    const render::CameraSnapshot& camera) noexcept;

[[nodiscard]] FirstPersonHeldItemPose first_person_held_item_pose(
    const render::CameraSnapshot& camera,
    HeldItemAnimationPose animation_pose = {},
    f32 sneak_progress = 0.0F) noexcept;

void apply_first_person_held_item_arm_pose(
    render::EntityDrawItem& player_draw,
    HeldItemAnimationPose animation_pose = {});

void append_held_item_draw(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const player::ItemStack& stack,
    const render::CameraSnapshot& camera,
    HeldItemAnimationPose animation_pose = {},
    f32 sneak_progress = 0.0F);

} // namespace arv::client
