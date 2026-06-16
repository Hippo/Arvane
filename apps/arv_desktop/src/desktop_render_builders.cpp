#include "desktop_render_builders.hpp"

#include "arv/client/item_rendering.hpp"
#include "arv/client/sky_rendering.hpp"
#include "arv/render/render_culling.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>

namespace arv::desktop {
namespace {

[[nodiscard]] f32 approach(f32 current, f32 target, f32 step) noexcept {
  if (current < target) {
    return std::min(current + step, target);
  }
  return std::max(current - step, target);
}

[[nodiscard]] f32 first_person_sneak_target_progress(
    const std::vector<protocol::EntityAnimationPlayback>& animations) noexcept {
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

[[nodiscard]] f32 update_first_person_sneak_visual_progress(
    DesktopGameSession& session,
    const std::vector<protocol::EntityAnimationPlayback>& animations,
    std::chrono::steady_clock::time_point now) noexcept {
  const f32 target = first_person_sneak_target_progress(animations);
  if (!session.first_person_sneak_visual_updated_at.has_value()) {
    session.first_person_sneak_visual_updated_at = now;
    session.first_person_sneak_visual_progress = target;
    return session.first_person_sneak_visual_progress;
  }

  const f32 delta_seconds = std::max(
      std::chrono::duration<f32>(
          now - session.first_person_sneak_visual_updated_at.value())
          .count(),
      0.0F);
  session.first_person_sneak_visual_updated_at = now;
  const f32 speed = target > session.first_person_sneak_visual_progress ? 4.0F : 5.0F;
  session.first_person_sneak_visual_progress = approach(
      session.first_person_sneak_visual_progress,
      target,
      delta_seconds * speed);
  return session.first_person_sneak_visual_progress;
}

[[nodiscard]] Vec3f first_person_player_body_position(
    const client::CameraShell& camera,
    f32 sneak_progress) noexcept {
  constexpr f32 EyeToBodyOriginY = 1.80F;
  constexpr f32 BaseBackOffset = 0.42F;
  constexpr f32 SneakBackOffset = 0.30F;

  Vec3f position =
      camera.position() -
      horizontal_forward(camera.yaw_radians()) *
          (BaseBackOffset + SneakBackOffset * sneak_progress);
  position.y -= EyeToBodyOriginY;
  return position;
}

[[nodiscard]] bool chunk_inside_render_window(
    const ChunkViewState& chunk_view,
    const io::ChunkDistanceConfig& distances,
    ChunkSectionCoord coord) noexcept {
  if (!chunk_view.target_center.has_value()) {
    return false;
  }
  const ChunkSectionCoord center = chunk_view.target_center.value();
  return coord.value.y >= chunk_view.target_min_chunk_y &&
         coord.value.y <= chunk_view.target_max_chunk_y &&
         std::abs(coord.value.x - center.value.x) <= distances.render_distance &&
         std::abs(coord.value.z - center.value.z) <= distances.render_distance;
}

[[nodiscard]] const std::vector<ChunkViewState::BlockAttachedModelEntry>&
block_attached_entries_for_chunk(
    ChunkViewState& chunk_view,
    const voxel::ChunkSection& chunk,
    ChunkSectionCoord coord,
    std::span<const render::EntityModelID> block_attached_models) {
  const u64 content_version = chunk.content_version();
  auto cached = chunk_view.block_attached_model_cache.find(coord);
  if (cached != chunk_view.block_attached_model_cache.end() &&
      cached->second.content_version == content_version) {
    return cached->second.entries;
  }

  ChunkViewState::BlockAttachedModelCache next;
  next.content_version = content_version;
  for (usize index = 0; index < voxel::ChunkVolume; ++index) {
    const voxel::BlockVariantID variant_id =
        chunk.block(voxel::local_pos(index));
    if (variant_id.value >= block_attached_models.size() ||
        block_attached_models[variant_id.value].value == 0U) {
      continue;
    }
    next.entries.push_back({voxel::local_pos(index), variant_id});
  }
  auto [iter, inserted] =
      chunk_view.block_attached_model_cache.insert_or_assign(coord, std::move(next));
  (void)inserted;
  return iter->second.entries;
}

void append_block_attached_model_draws(
    render::FramePacket& frame,
    DesktopGameSession& session,
    ChunkViewState& chunk_view,
    const EntityRenderAssets& entity_render_assets,
    const io::ChunkDistanceConfig& distances) {
  const std::vector<render::ChunkDrawItem>& resident_chunks =
      chunk_draw_items(chunk_view);
  const std::vector<render::EntityModelID>& block_attached_models =
      entity_render_assets.block_attached_model_by_variant;
  for (const render::ChunkDrawItem& draw_item : resident_chunks) {
    const ChunkSectionCoord coord = draw_item.key.coord;
    if (draw_item.key.world_id != frame.world_view.world_id) {
      continue;
    }
    if (!chunk_inside_render_window(chunk_view, distances, coord)) {
      continue;
    }
    if (!render::chunk_aabb_intersects_view(frame.world_view, coord)) {
      continue;
    }

    const voxel::ChunkSection* chunk = session.client_world.find_chunk(coord);
    if (chunk == nullptr) {
      chunk_view.block_attached_model_cache.erase(coord);
      continue;
    }
    const std::vector<ChunkViewState::BlockAttachedModelEntry>& entries =
        block_attached_entries_for_chunk(
            chunk_view,
            *chunk,
            coord,
            block_attached_models);
    for (const ChunkViewState::BlockAttachedModelEntry& entry : entries) {
      const voxel::BlockVariantID variant_id = entry.variant_id;
      if (variant_id.value >= block_attached_models.size()) {
        continue;
      }
      const render::EntityModelID model_id =
          block_attached_models[variant_id.value];
      if (model_id.value == 0U) {
        continue;
      }

      const BlockPos block =
          chunk_local_to_block(coord, entry.local);
      render::EntityDrawItem item;
      item.model_id = model_id;
      item.position = block_floor_center(block);
      if (variant_id.value < entity_render_assets.block_attached_rotation_by_variant.size()) {
        item.rotation =
            entity_render_assets.block_attached_rotation_by_variant[variant_id.value];
      }
      item.scale = 1.0F;
      frame.entities.push_back(std::move(item));
    }
  }
}

} // namespace

usize populate_chunk_draw_lists(
    render::FramePacket& frame,
    ChunkViewState& chunk_view,
    const io::ChunkDistanceConfig& distances) {
  const std::vector<render::ChunkDrawItem>& resident_chunks =
      chunk_draw_items(chunk_view);
  frame.opaque_chunks.clear();
  frame.transparent_chunks.clear();
  frame.opaque_chunks.reserve(resident_chunks.size());
  frame.transparent_chunks.reserve(resident_chunks.size());

  usize visible_chunks = 0;
  usize distance_culled = 0;
  usize frustum_culled = 0;
  for (const render::ChunkDrawItem& item : resident_chunks) {
    if (item.key.world_id != frame.world_view.world_id ||
        (!item.has_opaque_mesh && !item.has_transparent_mesh)) {
      continue;
    }
    if (!chunk_inside_render_window(chunk_view, distances, item.key.coord)) {
      ++distance_culled;
      continue;
    }
    if (!render::chunk_aabb_intersects_view(frame.world_view, item.key.coord)) {
      ++frustum_culled;
      continue;
    }

    if (item.has_opaque_mesh) {
      frame.opaque_chunks.emplace_back(item);
    }
    if (item.has_transparent_mesh) {
      frame.transparent_chunks.emplace_back(item);
    }
    ++visible_chunks;
  }

  chunk_view.visible_chunks_last_frame = visible_chunks;
  chunk_view.opaque_chunks_last_frame = frame.opaque_chunks.size();
  chunk_view.transparent_chunks_last_frame = frame.transparent_chunks.size();
  chunk_view.distance_culled_chunks_last_frame = distance_culled;
  chunk_view.frustum_culled_chunks_last_frame = frustum_culled;
  return visible_chunks;
}

void populate_world_frame_draw_items(
    render::FramePacket& frame,
    DesktopGameSession& session,
    ChunkViewState& chunk_view,
    const EntityRenderAssets& entity_render_assets,
    const HashMap<std::string, render::TextureRegionHandle>& player_skin_regions,
    const io::ChunkDistanceConfig& chunk_distances,
    const io::RenderConfig& render_config,
    bool show_chunk_boundaries,
    std::chrono::steady_clock::time_point now,
    std::chrono::steady_clock::time_point app_start_time,
    f32 interpolation_alpha) {
  (void)interpolation_alpha;
  const f32 entity_animation_time =
      std::chrono::duration<f32>(now - app_start_time).count();
  const world::WorldRuntime& sky_world =
      session.integrated_server == nullptr
          ? session.client_world
          : session.integrated_server->voxel_world();
  frame.sky = client::sky_uniforms_from_clock(sky_world.clock(), render_config.sky);
  const std::optional<PlayerID> local_player_id = session.client->local_player_id();
  const std::optional<EntityNetID> local_entity_id = session.client->local_entity_id();

  if (show_chunk_boundaries && entity_render_assets.chunk_bounds_model.value != 0U) {
    const ChunkSectionCoord chunk = world_position_chunk_section_coord(session.client->player_position());
    render::EntityDrawItem bounds;
    bounds.model_id = entity_render_assets.chunk_bounds_model;
    bounds.position = {
        static_cast<f32>(chunk.value.x * ChunkSize) + static_cast<f32>(ChunkSize) * 0.5F,
        static_cast<f32>(chunk.value.y * ChunkSize) + static_cast<f32>(ChunkSize) * 0.5F,
        static_cast<f32>(chunk.value.z * ChunkSize) + static_cast<f32>(ChunkSize) * 0.5F};
    bounds.scale = static_cast<f32>(ChunkSize);
    frame.entities.push_back(std::move(bounds));
  }

  for (const client::ReplicatedEntityMirror& mirror :
       session.client->replicated_entity_mirrors()) {
    if (mirror.world_id != frame.world_view.world_id) {
      continue;
    }
    const auto model = entity_render_assets.model_by_type.find(mirror.entity_type);
    if (model == entity_render_assets.model_by_type.end()) {
      continue;
    }
    const auto animation = entity_render_assets.animation_by_type.find(mirror.entity_type);
    render::EntityModelID model_id = model->second;
    const bool is_local_player =
        (local_entity_id.has_value() && mirror.entity_id == local_entity_id.value()) ||
        (local_player_id.has_value() && mirror.owning_player == local_player_id.value());
    if (is_local_player && !render_config.show_local_player_body) {
      continue;
    }
    const protocol::PlayerProfileInfo* profile =
        mirror.owning_player.has_value()
            ? session.client->profile_info_for_player(mirror.owning_player.value())
            : nullptr;
    const bool slim_player = profile != nullptr && profile->skin.slim_model;
    if (slim_player && entity_render_assets.player_slim_model.value != 0U) {
      model_id = entity_render_assets.player_slim_model;
    }
    bool using_first_person_skin = false;
    render::TextureRegionHandle player_skin_region;
    if (profile != nullptr && !profile->skin.skin_hash.empty()) {
      if (const auto region = player_skin_regions.find(profile->skin.skin_hash);
          region != player_skin_regions.end() && region->second.valid()) {
        player_skin_region = region->second;
        if (slim_player && entity_render_assets.player_slim_skin_model.value != 0U) {
          model_id = entity_render_assets.player_slim_skin_model;
        } else if (entity_render_assets.player_skin_model.value != 0U) {
          model_id = entity_render_assets.player_skin_model;
        }
        if (is_local_player && slim_player &&
            entity_render_assets.first_person_player_slim_skin_model.value != 0U) {
          model_id = entity_render_assets.first_person_player_slim_skin_model;
          using_first_person_skin = true;
        } else if (is_local_player &&
                   entity_render_assets.first_person_player_skin_model.value != 0U) {
          model_id = entity_render_assets.first_person_player_skin_model;
          using_first_person_skin = true;
        }
      }
    }
    if (is_local_player && entity_render_assets.first_person_player_model.value != 0U) {
      if (!using_first_person_skin) {
        if (slim_player &&
            entity_render_assets.first_person_player_slim_model.value != 0U) {
          model_id = entity_render_assets.first_person_player_slim_model;
        } else {
          model_id = entity_render_assets.first_person_player_model;
        }
      }
    }
    Vec3f position =
        is_local_player
            ? session.client->player_position()
            : mirror.render_position;
    if (is_local_player) {
      const f32 sneak_progress = update_first_person_sneak_visual_progress(
          session,
          mirror.animations,
          now);
      position = first_person_player_body_position(
          session.client->camera(),
          sneak_progress);
    }
    render::EntityDrawItem item;
    item.model_id = model_id;
    if (animation != entity_render_assets.animation_by_type.end()) {
      item.animation_id = animation->second;
    }
    item.position = position;
    item.rotation =
        is_local_player
            ? model_forward_yaw_rotation(session.client->camera().yaw_radians())
            : mirror.render_rotation;
    item.scale = 1.0F;
    item.animations.reserve(mirror.animations.size());
    for (const protocol::EntityAnimationPlayback& animation : mirror.animations) {
      item.animations.push_back({animation.name, animation.time_seconds});
    }
    item.player_skin_region = player_skin_region;
    if (mirror.owning_player.has_value()) {
      item.bone_rotation_overrides.push_back({"head", mirror.head_rotation_degrees});
    }
    frame.entities.push_back(std::move(item));
  }

  const std::vector<client::DroppedItemMirror> dropped_items =
      session.client->dropped_item_mirrors();
  client::append_dropped_item_draws(
      frame,
      entity_render_assets,
      dropped_items,
      frame.world_view.world_id,
      entity_animation_time);

  append_block_attached_model_draws(
      frame,
      session,
      chunk_view,
      entity_render_assets,
      chunk_distances);

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

  const f32 animation_time_seconds =
      std::chrono::duration<f32>(now - app_start_time).count();
  const client::HeldItemAnimationPose animation_pose =
      session.held_item_animation.sample(animation_time_seconds);
  client::append_held_item_draw(
      frame,
      entity_render_assets,
      held_stack.value(),
      frame.camera,
      animation_pose);
}

} // namespace arv::desktop
