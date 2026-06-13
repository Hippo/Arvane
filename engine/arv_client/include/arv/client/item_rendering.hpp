#pragma once

#include "arv/client/dropped_item_mirror.hpp"
#include "arv/client/entity_render_assets.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/render/render_types.hpp"

#include <span>

namespace arv::client {

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

void append_held_item_draw(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const player::ItemStack& stack,
    const render::CameraSnapshot& camera,
    HeldItemAnimationPose animation_pose = {});

} // namespace arv::client
