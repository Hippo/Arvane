#include "arv/client/client_frame_composer.hpp"

#include "arv/physics/raycast.hpp"
#include "arv/render/render_culling.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace arv::client {
namespace {

constexpr f32 ContactShadowSurfaceBias = 0.015F;
constexpr f32 ContactShadowProbeLift = 0.10F;
constexpr f32 ContactShadowMinAlpha = 0.01F;
constexpr f32 ReplicatedEntityContactShadowAlpha = 0.32F;
constexpr f32 DroppedItemContactShadowAlpha = 0.22F;
constexpr f32 BlockAttachedContactShadowAlpha = 0.18F;
constexpr f32 SpaceBodyShellDistanceFraction = 0.65F;
constexpr f32 SpaceBodyShellSafetyMargin = 96.0F;
constexpr f32 SpaceBodyMinRenderedRadius = 18.0F;
constexpr f32 SpaceBodyMaxRenderedRadiusFraction = 0.25F;

[[nodiscard]] bool can_append_contact_shadow(
    const render::FramePacket& frame,
    const ContactShadowOptions& options) noexcept {
  return options.enabled && options.max_shadows_per_frame > 0U &&
         frame.contact_shadows.size() < options.max_shadows_per_frame;
}

[[nodiscard]] Vec2f clamped_contact_half_extents(Aabb3f bounds) noexcept {
  const f32 half_x = (bounds.max.x - bounds.min.x) * 0.5F;
  const f32 half_z = (bounds.max.z - bounds.min.z) * 0.5F;
  return {
      std::clamp(half_x, 0.18F, 1.35F),
      std::clamp(half_z, 0.18F, 1.35F)};
}

[[nodiscard]] Aabb3f default_replicated_entity_bounds(
    const ReplicatedEntityMirror& mirror) noexcept {
  constexpr Vec3f DefaultBox{0.6F, 1.8F, 0.6F};
  const Vec3f position = mirror.render_position;
  return {
      {position.x - DefaultBox.x * 0.5F,
       position.y,
       position.z - DefaultBox.z * 0.5F},
      {position.x + DefaultBox.x * 0.5F,
       position.y + DefaultBox.y,
       position.z + DefaultBox.z * 0.5F}};
}

struct SpaceBodyDrawPlacement {
  Vec3f position{};
  f32 scale{1.0F};
};

[[nodiscard]] SpaceBodyDrawPlacement space_body_draw_placement(
    const render::FramePacket& frame,
    const protocol::ClientboundSpaceBodyView& body) noexcept {
  const f32 visual_radius = std::max(1.0F, body.visual_radius);
  const Vec3f camera_position =
      finite(frame.camera.position) ? frame.camera.position : frame.world_view.position;
  const f32 far_plane =
      std::isfinite(frame.camera.far_plane)
          ? std::max(frame.camera.far_plane, 16.0F)
          : std::max(frame.world_view.far_plane, 16.0F);
  const Vec3f to_body = body.position - camera_position;
  const f32 distance_to_body = length(to_body);
  if (!std::isfinite(distance_to_body) || distance_to_body <= 0.001F ||
      distance_to_body + visual_radius <= far_plane - SpaceBodyShellSafetyMargin) {
    return {body.position, visual_radius * 2.0F};
  }

  const Vec3f direction = normalize_or_zero(to_body);
  if (length(direction) <= 0.0F) {
    return {body.position, visual_radius * 2.0F};
  }
  const f32 shell_distance = std::clamp(
      far_plane * SpaceBodyShellDistanceFraction,
      64.0F,
      std::max(64.0F, far_plane - SpaceBodyShellSafetyMargin));
  const f32 projected_radius = std::clamp(
      visual_radius * shell_distance / distance_to_body,
      SpaceBodyMinRenderedRadius,
      shell_distance * SpaceBodyMaxRenderedRadiusFraction);
  return {camera_position + direction * shell_distance, projected_radius * 2.0F};
}

[[nodiscard]] Aabb3f replicated_entity_bounds_for(
    const ClientEntityFrameInput& input,
    const ReplicatedEntityMirror& mirror) {
  if (input.contact_shadows.replicated_entity_bounds) {
    return input.contact_shadows.replicated_entity_bounds(mirror);
  }
  return default_replicated_entity_bounds(mirror);
}

[[nodiscard]] bool append_probe_contact_shadow(
    render::FramePacket& frame,
    const ContactShadowOptions& options,
    Vec3f caster_center,
    f32 caster_base_y,
    Vec2f half_extents,
    f32 base_alpha) {
  if (!can_append_contact_shadow(frame, options) ||
      options.collision_query == nullptr ||
      options.max_probe_distance <= 0.0F ||
      half_extents.x <= 0.0F ||
      half_extents.y <= 0.0F ||
      base_alpha <= 0.0F) {
    return false;
  }

  const Vec3f probe_origin{
      caster_center.x,
      caster_base_y + ContactShadowProbeLift,
      caster_center.z};
  const std::optional<physics::BlockRaycastHit> hit =
      physics::raycast_blocks(
          *options.collision_query,
          probe_origin,
          {0.0F, -1.0F, 0.0F},
          options.max_probe_distance + ContactShadowProbeLift);
  if (!hit.has_value() || hit->face != voxel::BlockFace::PositiveY) {
    return false;
  }

  const f32 height_above_receiver =
      std::max(caster_base_y - hit->hit_point.y, 0.0F);
  const f32 fade =
      std::clamp(1.0F - height_above_receiver / options.max_probe_distance, 0.0F, 1.0F);
  const f32 alpha = base_alpha * fade;
  if (alpha <= ContactShadowMinAlpha) {
    return false;
  }

  frame.contact_shadows.push_back(
      {.world_id = frame.world_view.world_id,
       .center = {caster_center.x, hit->hit_point.y + ContactShadowSurfaceBias, caster_center.z},
       .half_extents = half_extents,
       .alpha = alpha});
  return true;
}

[[nodiscard]] bool append_fixed_contact_shadow(
    render::FramePacket& frame,
    const ContactShadowOptions& options,
    Vec3f center,
    Vec2f half_extents,
    f32 alpha) {
  if (!can_append_contact_shadow(frame, options) ||
      half_extents.x <= 0.0F ||
      half_extents.y <= 0.0F ||
      alpha <= ContactShadowMinAlpha) {
    return false;
  }
  frame.contact_shadows.push_back(
      {.world_id = frame.world_view.world_id,
       .center = center,
       .half_extents = half_extents,
       .alpha = alpha});
  return true;
}

[[nodiscard]] bool replicated_entity_is_hidden_local_player(
    const ReplicatedEntityMirror& mirror,
    const ClientEntityFrameInput& input) noexcept {
  const bool is_local_player =
      (input.local_entity_id.has_value() &&
       mirror.entity_id == input.local_entity_id.value()) ||
      (input.local_player_id.has_value() &&
       mirror.owning_player.has_value() &&
       mirror.owning_player.value() == input.local_player_id.value());
  return is_local_player && input.first_person_sneak_visual != nullptr &&
         !input.show_local_player_body;
}

[[nodiscard]] usize append_replicated_entity_contact_shadows(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const ClientEntityFrameInput& input) {
  const usize before = frame.contact_shadows.size();
  for (const ReplicatedEntityMirror& mirror : input.replicated_entities) {
    if (!can_append_contact_shadow(frame, input.contact_shadows)) {
      break;
    }
    if (mirror.world_id != input.world_id ||
        replicated_entity_is_hidden_local_player(mirror, input)) {
      continue;
    }
    if (entity_render_assets.model_by_type.find(mirror.entity_type) ==
        entity_render_assets.model_by_type.end()) {
      continue;
    }

    const Aabb3f bounds = replicated_entity_bounds_for(input, mirror);
    const Vec3f center{
        (bounds.min.x + bounds.max.x) * 0.5F,
        bounds.min.y,
        (bounds.min.z + bounds.max.z) * 0.5F};
    (void)append_probe_contact_shadow(
        frame,
        input.contact_shadows,
        center,
        bounds.min.y,
        clamped_contact_half_extents(bounds),
        ReplicatedEntityContactShadowAlpha);
  }
  return frame.contact_shadows.size() - before;
}

[[nodiscard]] usize append_dropped_item_contact_shadows(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    std::span<const DroppedItemMirror> dropped_items,
    WorldID world_id,
    const ContactShadowOptions& options) {
  const usize before = frame.contact_shadows.size();
  for (const DroppedItemMirror& dropped : dropped_items) {
    if (!can_append_contact_shadow(frame, options)) {
      break;
    }
    if (dropped.world_id != world_id || dropped.stack.count == 0U ||
        entity_render_assets.model_by_item.find(dropped.stack.item) ==
            entity_render_assets.model_by_item.end()) {
      continue;
    }
    (void)append_probe_contact_shadow(
        frame,
        options,
        dropped.render_position,
        dropped.render_position.y,
        {0.30F, 0.22F},
        DroppedItemContactShadowAlpha);
  }
  return frame.contact_shadows.size() - before;
}

[[nodiscard]] usize append_block_attached_contact_shadows_for_visible_sections(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    BlockAttachedModelSectionRange sections,
    const ChunkDrawAppendOptions& options,
    const ContactShadowOptions& shadow_options) {
  const usize before = frame.contact_shadows.size();
  if (!sections.section_at || !sections.entries_at) {
    return 0U;
  }

  for (usize index = 0; index < sections.count; ++index) {
    if (!can_append_contact_shadow(frame, shadow_options)) {
      break;
    }
    const BlockAttachedModelSectionRef section = sections.section_at(index);
    if (section.world_id != frame.world_view.world_id) {
      continue;
    }
    if (options.inside_render_window &&
        !options.inside_render_window(section.coord)) {
      continue;
    }
    if (options.frustum_cull &&
        !render::chunk_aabb_intersects_view(frame.world_view, section.coord)) {
      continue;
    }

    const std::span<const BlockAttachedModelSectionEntry> entries =
        sections.entries_at(index);
    for (const BlockAttachedModelSectionEntry& entry : entries) {
      if (!can_append_contact_shadow(frame, shadow_options)) {
        break;
      }
      if (!block_variant_has_attached_model(entity_render_assets, entry.variant_id)) {
        continue;
      }
      Vec3f center = block_floor_center(chunk_local_to_block(section.coord, entry.local));
      center.y += ContactShadowSurfaceBias;
      (void)append_fixed_contact_shadow(
          frame,
          shadow_options,
          center,
          {0.46F, 0.46F},
          BlockAttachedContactShadowAlpha);
    }
  }

  return frame.contact_shadows.size() - before;
}

} // namespace

ChunkDrawAppendResult append_chunk_draw_item(
    render::FramePacket& frame,
    const render::ChunkDrawItem& item,
    const ChunkDrawAppendOptions& options) {
  if (item.key.world_id != frame.world_view.world_id) {
    return ChunkDrawAppendResult::WrongWorld;
  }
  if (!item.has_opaque_mesh && !item.has_transparent_mesh) {
    return ChunkDrawAppendResult::Empty;
  }
  if (options.inside_render_window &&
      !options.inside_render_window(item.key.coord)) {
    return ChunkDrawAppendResult::OutsideRenderWindow;
  }
  if (options.frustum_cull &&
      !render::chunk_aabb_intersects_view(frame.world_view, item.key.coord)) {
    return ChunkDrawAppendResult::OutsideFrustum;
  }

  if (item.has_opaque_mesh) {
    frame.opaque_chunks.push_back(item);
  }
  if (item.has_transparent_mesh) {
    frame.transparent_chunks.push_back(item);
  }
  return ChunkDrawAppendResult::Appended;
}

ChunkDrawAppendSummary append_chunk_draw_items(
    render::FramePacket& frame,
    ChunkDrawAppendRange items,
    const ChunkDrawAppendOptions& options) {
  ChunkDrawAppendSummary summary;
  if (!items.item_at) {
    return summary;
  }

  for (usize index = 0; index < items.count; ++index) {
    switch (append_chunk_draw_item(frame, items.item_at(index), options)) {
      case ChunkDrawAppendResult::Appended:
        ++summary.appended;
        break;
      case ChunkDrawAppendResult::WrongWorld:
        ++summary.wrong_world;
        break;
      case ChunkDrawAppendResult::Empty:
        ++summary.empty;
        break;
      case ChunkDrawAppendResult::OutsideRenderWindow:
        ++summary.outside_render_window;
        break;
      case ChunkDrawAppendResult::OutsideFrustum:
        ++summary.outside_frustum;
        break;
    }
  }
  return summary;
}

ChunkDrawAppendSummary append_chunk_draw_items(
    render::FramePacket& frame,
    std::span<const render::ChunkDrawItem> items,
    const ChunkDrawAppendOptions& options) {
  const auto item_at =
      [&](usize index) -> const render::ChunkDrawItem& {
    return items[index];
  };
  return append_chunk_draw_items(
      frame,
      {.count = items.size(),
       .item_at = FunctionRef<const render::ChunkDrawItem&(usize)>{item_at}},
      options);
}

BlockAttachedModelAppendSummary
append_block_attached_model_draws_for_visible_sections(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    BlockAttachedModelSectionRange sections,
    const BlockContainerAnimationState* container_animation,
    Seconds animation_time_seconds,
    const ChunkDrawAppendOptions& options) {
  BlockAttachedModelAppendSummary summary;
  if (!sections.section_at || !sections.entries_at) {
    return summary;
  }

  for (usize index = 0; index < sections.count; ++index) {
    const BlockAttachedModelSectionRef section = sections.section_at(index);
    if (section.world_id != frame.world_view.world_id) {
      ++summary.wrong_world;
      continue;
    }
    if (options.inside_render_window &&
        !options.inside_render_window(section.coord)) {
      ++summary.outside_render_window;
      continue;
    }
    if (options.frustum_cull &&
        !render::chunk_aabb_intersects_view(frame.world_view, section.coord)) {
      ++summary.outside_frustum;
      continue;
    }

    const std::span<const BlockAttachedModelSectionEntry> entries =
        sections.entries_at(index);
    if (entries.empty()) {
      ++summary.empty;
      continue;
    }

    const usize appended =
        append_block_attached_model_draws_for_section_entries(
            frame,
            entity_render_assets,
            section.coord,
            entries,
            container_animation,
            animation_time_seconds);
    if (appended == 0U) {
      ++summary.empty;
      continue;
    }
    ++summary.sections_appended;
    summary.models_appended += appended;
  }
  return summary;
}

usize append_space_body_draws(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    std::span<const protocol::ClientboundSpaceBodyView> bodies) {
  if (entity_render_assets.space_body_model.value == 0U || bodies.empty()) {
    return 0U;
  }
  const usize before = frame.entities.size();
  for (const protocol::ClientboundSpaceBodyView& body : bodies) {
    if (!std::isfinite(body.visual_radius) || body.visual_radius <= 0.0F) {
      continue;
    }
    render::EntityDrawItem item;
    item.model_id = entity_render_assets.space_body_model;
    const SpaceBodyDrawPlacement placement = space_body_draw_placement(frame, body);
    item.position = placement.position;
    item.scale = std::max(1.0F, placement.scale);
    frame.entities.push_back(std::move(item));
  }
  return frame.entities.size() - before;
}

ClientEntityFrameResult append_client_entity_frame_draws(
    render::FramePacket& frame,
    const EntityRenderAssets& entity_render_assets,
    const ClientEntityFrameInput& input) {
  ClientEntityFrameResult result;
  result.first_person_sneak_progress =
      input.first_person_sneak_visual == nullptr
          ? 0.0F
          : input.first_person_sneak_visual->progress;

  if (input.block_container_animation != nullptr) {
    update_block_container_animation(
        *input.block_container_animation,
        input.active_container_block_source,
        input.animation_time_seconds);
  }

  const ReplicatedEntityFrameResult replicated_result =
      append_replicated_entity_draws(
          frame,
          entity_render_assets,
          {.mirrors = input.replicated_entities,
           .world_id = input.world_id,
           .local_entity_id = input.local_entity_id,
           .local_player_id = input.local_player_id,
           .first_person_camera = input.first_person_camera,
           .show_local_player_body = input.show_local_player_body,
           .apply_local_held_item_arm_pose = input.visible_held_stack.has_value(),
           .held_item_pose = input.held_item_pose,
           .first_person_sneak_visual = input.first_person_sneak_visual,
           .first_person_sneak_progress = result.first_person_sneak_progress,
           .animation_time_seconds = input.animation_time_seconds,
           .callbacks = input.callbacks.replicated_entities});
  result.replicated_entities_appended = replicated_result.appended;
  result.first_person_sneak_progress =
      replicated_result.first_person_sneak_progress;
  result.contact_shadows_appended += append_replicated_entity_contact_shadows(
      frame,
      entity_render_assets,
      input);

  if (input.require_local_entity_for_world_draws &&
      !input.local_entity_id.has_value()) {
    return result;
  }

  result.space_bodies_appended =
      append_space_body_draws(frame, entity_render_assets, input.space_bodies);

  const usize before_dropped = frame.entities.size();
  append_dropped_item_draws(
      frame,
      entity_render_assets,
      input.dropped_items,
      input.world_id,
      input.animation_time_seconds);
  result.dropped_items_appended = frame.entities.size() - before_dropped;
  result.contact_shadows_appended += append_dropped_item_contact_shadows(
      frame,
      entity_render_assets,
      input.dropped_items,
      input.world_id,
      input.contact_shadows);

  result.block_attached_models_appended =
      append_block_attached_model_draws_for_visible_sections(
          frame,
          entity_render_assets,
          input.block_attached_sections,
          input.block_container_animation,
          input.animation_time_seconds,
          input.block_attached_section_options)
          .models_appended;
  result.contact_shadows_appended +=
      append_block_attached_contact_shadows_for_visible_sections(
          frame,
          entity_render_assets,
          input.block_attached_sections,
          input.block_attached_section_options,
          input.contact_shadows);

  if (input.block_outline_target.has_value()) {
    const usize before_outline = frame.entities.size();
    append_block_outline_draw(
        frame,
        entity_render_assets,
        input.block_outline_target.value());
    result.outline_draws_appended = frame.entities.size() - before_outline;
  }

  const usize before_break_overlay = frame.entities.size();
  append_block_break_overlay_draw(frame, entity_render_assets, input.block_break);
  result.break_overlay_draws_appended =
      frame.entities.size() - before_break_overlay;

  if (input.visible_held_stack.has_value()) {
    const usize before_held = frame.entities.size();
    append_held_item_draw(
        frame,
        entity_render_assets,
        input.visible_held_stack.value(),
        input.first_person_camera,
        input.held_item_pose,
        result.first_person_sneak_progress);
    result.held_item_appended = frame.entities.size() != before_held;
  }

  return result;
}

} // namespace arv::client
