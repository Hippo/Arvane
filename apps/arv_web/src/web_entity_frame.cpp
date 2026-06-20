#include "web_entity_frame.hpp"

#include "arv/client/block_outline_target.hpp"
#include "arv/client/chunk_collision_query.hpp"
#include "arv/client/client_frame_composer.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/gameplay_interactions.hpp"
#include "arv/client/item_rendering.hpp"
#include "arv/client/player_skin_rendering.hpp"
#include "arv/net/transport.hpp"
#include "arv/player/player_constants.hpp"
#include "web_application.hpp"
#include "web_client_assets.hpp"
#include "web_entity_fallback.hpp"
#include "web_platform.hpp"
#include "web_render_host.hpp"

#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace arv::web {

void append_web_entity_draw_items(
    render::FramePacket& item_frame,
    Seconds animation_time_seconds) {
  if (g_app.session == nullptr || !g_app.entity_assets.ready) {
    return;
  }
  const std::optional<protocol::ContainerBlockSource> current_container_source =
      g_app.session->active_container_block_source();
  const WorldID world_id = item_frame.world_view.world_id;
  const std::optional<EntityNetID> local_entity_id =
      g_app.session->local_entity_id();
  const std::optional<PlayerID> local_player_id =
      g_app.session->local_player_id();
  const std::optional<player::ItemStack> visible_held_stack =
      client::visible_held_item_stack(*g_app.session);
  const client::HeldItemAnimationPose held_item_pose =
      g_app.input.held_item_animation.sample(animation_time_seconds);
  const std::vector<client::ReplicatedEntityMirror> mirrors =
      g_app.session->replicated_entity_mirrors();
  item_frame.entities.reserve(
      item_frame.entities.size() +
      mirrors.size() +
      g_app.session->dropped_item_count() +
      2U);

  const client::CameraShell& camera = g_app.session->camera();
  const client::ChunkSectionCollisionQuery outline_query(
      g_app.chunk_stream.sections(),
      g_app.block_assets.loaded.block_registry);
  const std::optional<client::BlockOutlineTarget> outline_target =
      client::find_block_outline_target(
          outline_query,
          g_app.block_assets.loaded.block_registry,
          camera.position(),
          camera.forward_vector(),
          player::DefaultReachDistance);

  const auto player_skin_info =
      [&](PlayerID player_id) -> std::optional<client::PlayerEntitySkinInfo> {
    const protocol::PlayerProfileInfo* profile =
        g_app.session->profile_info_for_player(player_id);
    if (profile == nullptr) {
      return std::nullopt;
    }
    return client::PlayerEntitySkinInfo{
        .slim_model = profile->skin.slim_model,
        .skin_hash = profile->skin.skin_hash};
  };
  const auto skin_ready =
      [](std::string_view skin_hash, render::TextureRegionHandle handle) {
    return web_player_skin_texture_ready(skin_hash, handle);
  };
  const auto mark_skin_used =
      [](render::TextureRegionHandle handle) {
    (void)mark_web_player_skin_texture_used(handle, true);
  };
  const auto ready_player_skin =
      [&](std::string_view skin_hash)
          -> std::optional<render::TextureRegionHandle> {
    return client::ready_player_skin_receipt(
        g_app.entity_assets.player_skin_handles,
        skin_hash,
        {.ready = FunctionRef<bool(
              std::string_view,
              render::TextureRegionHandle)>{skin_ready},
         .mark_used =
             FunctionRef<void(render::TextureRegionHandle)>{
                 mark_skin_used}});
  };
  const std::vector<client::DroppedItemMirror> dropped_items =
      g_app.session->dropped_item_mirrors();
  const std::span<const protocol::ClientboundChunkSectionData> streamed_sections =
      g_app.chunk_stream.sections();
  std::vector<client::BlockAttachedModelSectionEntry> block_attached_entries;
  const auto block_attached_section_at =
      [&](usize index) -> client::BlockAttachedModelSectionRef {
    const protocol::ClientboundChunkSectionData& section = streamed_sections[index];
    return {.world_id = section.world_id, .coord = section.coord};
  };
  const auto block_attached_entries_at =
      [&](usize index) -> std::span<const client::BlockAttachedModelSectionEntry> {
    const protocol::ClientboundChunkSectionData& section = streamed_sections[index];
      client::collect_block_attached_model_section_entries(
          g_app.entity_assets.loaded.render_assets,
          section.blocks,
          block_attached_entries);
    return std::span<const client::BlockAttachedModelSectionEntry>{
        block_attached_entries};
  };

  (void)client::append_client_entity_frame_draws(
      item_frame,
      g_app.entity_assets.loaded.render_assets,
      {.replicated_entities =
           std::span<const client::ReplicatedEntityMirror>{mirrors},
       .dropped_items =
           std::span<const client::DroppedItemMirror>{dropped_items},
       .world_id = world_id,
       .local_entity_id = local_entity_id,
       .local_player_id = local_player_id,
       .first_person_camera = item_frame.camera,
       .show_local_player_body = true,
       .visible_held_stack = visible_held_stack,
       .held_item_pose = held_item_pose,
       .first_person_sneak_visual = &g_app.first_person_sneak_visual,
       .block_container_animation = &g_app.block_container_animation,
       .active_container_block_source = current_container_source,
       .block_outline_target = outline_target,
       .block_break = g_app.session->block_break_progress(),
       .animation_time_seconds = animation_time_seconds,
       .require_local_entity_for_world_draws = true,
       .block_attached_sections =
           {.count = streamed_sections.size(),
            .section_at =
                FunctionRef<client::BlockAttachedModelSectionRef(usize)>{
                    block_attached_section_at},
            .entries_at =
                FunctionRef<std::span<const client::BlockAttachedModelSectionEntry>(
                    usize)>{block_attached_entries_at}},
       .callbacks =
           {.replicated_entities =
                {.player_skin_info =
                     FunctionRef<std::optional<
                         client::PlayerEntitySkinInfo>(PlayerID)>{
                         player_skin_info},
                 .ready_player_skin =
                     FunctionRef<std::optional<
                         render::TextureRegionHandle>(std::string_view)>{
                         ready_player_skin}}}});
}

void sync_web_entity_fallback_mesh() {
  clear_web_fallback_color_vertices();
  if (g_app.session == nullptr ||
      g_app.session->connection_state() != arv::net::ConnectionState::InGame) {
    return;
  }
  if (ensure_entity_render_assets_loaded()) {
    return;
  }

  const std::vector<arv::client::ReplicatedEntityMirror> mirrors =
      g_app.session->replicated_entity_mirrors();
  const std::vector<arv::web::WebEntityMarker> markers =
      arv::web::build_visible_fallback_entity_markers(
          mirrors,
          g_app.session->camera().world_id(),
          g_app.session->local_entity_id(),
          g_app.session->local_player_id());
  const std::vector<arv::web::WebFallbackColorVertex> vertices =
      arv::web::build_fallback_entity_vertices(markers);
  set_web_fallback_color_vertices(vertices);

  if (!vertices.empty() && !g_app.renderer.announced_first_entity_upload) {
    g_app.renderer.announced_first_entity_upload = true;
    set_status("remote entity markers uploaded");
  }
}

} // namespace arv::web
