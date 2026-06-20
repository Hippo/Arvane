#include "arv/client/client_frame_composer.hpp"

#include "arv/render/render_culling.hpp"

namespace arv::client {

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

  if (input.require_local_entity_for_world_draws &&
      !input.local_entity_id.has_value()) {
    return result;
  }

  const usize before_dropped = frame.entities.size();
  append_dropped_item_draws(
      frame,
      entity_render_assets,
      input.dropped_items,
      input.world_id,
      input.animation_time_seconds);
  result.dropped_items_appended = frame.entities.size() - before_dropped;

  result.block_attached_models_appended =
      append_block_attached_model_draws_for_visible_sections(
          frame,
          entity_render_assets,
          input.block_attached_sections,
          input.block_container_animation,
          input.animation_time_seconds,
          input.block_attached_section_options)
          .models_appended;

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
