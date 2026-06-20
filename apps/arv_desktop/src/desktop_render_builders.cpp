#include "desktop_render_builders.hpp"

#include "arv/client/block_container_animation.hpp"
#include "arv/client/client_frame_composer.hpp"
#include "arv/client/gameplay_interactions.hpp"
#include "arv/client/item_rendering.hpp"
#include "arv/client/player_skin_rendering.hpp"
#include "arv/client/sky_rendering.hpp"

#include <chrono>
#include <cmath>
#include <optional>
#include <span>
#include <string_view>

namespace arv::desktop {
namespace {

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

[[nodiscard]] const std::vector<client::BlockAttachedModelSectionEntry>&
block_attached_entries_for_chunk(
    ChunkViewState& chunk_view,
    const voxel::ChunkSection& chunk,
    ChunkSectionCoord coord,
    const client::EntityRenderAssets& entity_render_assets) {
  const u64 content_version = chunk.content_version();
  auto cached = chunk_view.block_attached_model_cache.find(coord);
  if (cached != chunk_view.block_attached_model_cache.end() &&
      cached->second.content_version == content_version) {
    return cached->second.entries;
  }

  ChunkViewState::BlockAttachedModelCache next;
  next.content_version = content_version;
  client::collect_block_attached_model_section_entries(
      entity_render_assets,
      chunk.blocks(),
      next.entries);
  auto [iter, inserted] =
      chunk_view.block_attached_model_cache.insert_or_assign(coord, std::move(next));
  (void)inserted;
  return iter->second.entries;
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
  const auto inside_render_window =
      [&](ChunkSectionCoord coord) {
    return chunk_inside_render_window(chunk_view, distances, coord);
  };
  const client::ChunkDrawAppendSummary append_summary =
      client::append_chunk_draw_items(
          frame,
          std::span<const render::ChunkDrawItem>{resident_chunks},
          {.inside_render_window =
               FunctionRef<bool(ChunkSectionCoord)>{inside_render_window}});
  visible_chunks = append_summary.appended;
  distance_culled = append_summary.outside_render_window;
  frustum_culled = append_summary.outside_frustum;

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
    const client::EntityRenderAssets& entity_render_assets,
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
  client::configure_frame_sky(frame, sky_world.clock(), render_config.sky);
  const std::optional<PlayerID> local_player_id = session.client->local_player_id();
  const std::optional<EntityNetID> local_entity_id = session.client->local_entity_id();

  const std::optional<player::ItemStack> visible_held_stack =
      client::visible_held_item_stack(*session.client);
  const client::HeldItemAnimationPose held_item_pose =
      session.held_item_animation.sample(entity_animation_time);

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

  const auto player_skin_info =
      [&](PlayerID player_id) -> std::optional<client::PlayerEntitySkinInfo> {
    const protocol::PlayerProfileInfo* profile =
        session.client->profile_info_for_player(player_id);
    if (profile == nullptr) {
      return std::nullopt;
    }
    return client::PlayerEntitySkinInfo{
        .slim_model = profile->skin.slim_model,
        .skin_hash = profile->skin.skin_hash};
  };
  const auto skin_ready =
      [](std::string_view, render::TextureRegionHandle) {
    return true;
  };
  const auto ready_player_skin =
      [&](std::string_view skin_hash)
          -> std::optional<render::TextureRegionHandle> {
    return client::ready_player_skin_receipt(
        player_skin_regions,
        skin_hash,
        {.ready =
             FunctionRef<bool(
                 std::string_view,
                 render::TextureRegionHandle)>{skin_ready}});
  };
  const std::vector<client::ReplicatedEntityMirror> replicated_entities =
      session.client->replicated_entity_mirrors();
  const std::vector<client::DroppedItemMirror> dropped_items =
      session.client->dropped_item_mirrors();
  const std::vector<render::ChunkDrawItem>& resident_chunks =
      chunk_draw_items(chunk_view);
  const auto block_attached_section_at =
      [&](usize index) -> client::BlockAttachedModelSectionRef {
    const render::ChunkDrawItem& item = resident_chunks[index];
    return {.world_id = item.key.world_id, .coord = item.key.coord};
  };
  const auto block_attached_entries_at =
      [&](usize index) -> std::span<const client::BlockAttachedModelSectionEntry> {
    const ChunkSectionCoord coord = resident_chunks[index].key.coord;
    const voxel::ChunkSection* chunk = session.client_world.find_chunk(coord);
    if (chunk == nullptr) {
      chunk_view.block_attached_model_cache.erase(coord);
      return {};
    }
    const std::vector<client::BlockAttachedModelSectionEntry>& entries =
        block_attached_entries_for_chunk(
            chunk_view,
            *chunk,
            coord,
            entity_render_assets);
    return std::span<const client::BlockAttachedModelSectionEntry>{entries};
  };
  const auto inside_render_window =
      [&](ChunkSectionCoord coord) {
    return chunk_inside_render_window(chunk_view, chunk_distances, coord);
  };

  (void)client::append_client_entity_frame_draws(
      frame,
      entity_render_assets,
      {.replicated_entities =
           std::span<const client::ReplicatedEntityMirror>{replicated_entities},
       .dropped_items =
           std::span<const client::DroppedItemMirror>{dropped_items},
       .world_id = frame.world_view.world_id,
       .local_entity_id = local_entity_id,
       .local_player_id = local_player_id,
       .first_person_camera = frame.camera,
       .show_local_player_body = render_config.show_local_player_body,
       .visible_held_stack = visible_held_stack,
       .held_item_pose = held_item_pose,
       .first_person_sneak_visual = &session.first_person_sneak_visual,
       .block_container_animation = &session.block_container_animation,
       .active_container_block_source =
           session.client->active_container_block_source(),
       .block_outline_target = session.block_outline_target,
       .block_break = session.client->block_break_progress(),
       .animation_time_seconds = entity_animation_time,
       .block_attached_sections =
           {.count = resident_chunks.size(),
            .section_at =
                FunctionRef<client::BlockAttachedModelSectionRef(usize)>{
                    block_attached_section_at},
            .entries_at =
                FunctionRef<std::span<const client::BlockAttachedModelSectionEntry>(
                    usize)>{block_attached_entries_at}},
       .block_attached_section_options =
           {.inside_render_window =
                FunctionRef<bool(ChunkSectionCoord)>{inside_render_window}},
       .callbacks =
           {.replicated_entities =
                {.player_skin_info =
                     FunctionRef<std::optional<client::PlayerEntitySkinInfo>(
                         PlayerID)>{player_skin_info},
                 .ready_player_skin =
                     FunctionRef<std::optional<render::TextureRegionHandle>(
                         std::string_view)>{ready_player_skin}}}});
}

} // namespace arv::desktop
