#include "web_chunk_renderer.hpp"

#include "arv/client/chunk_snapshot.hpp"
#include "arv/client/client_frame_composer.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/remote_chunk_stream.hpp"
#include "arv/render/chunk_mesher.hpp"
#include "web_application.hpp"
#include "web_client_assets.hpp"
#include "web_platform.hpp"
#include "web_render_host.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace arv::web {
namespace {

void upload_chunk_mesh(
    const protocol::ClientboundChunkSectionData& section,
    client::ChunkSectionDataLookup section_lookup) {
  if (!ensure_block_render_assets_loaded()) {
    return;
  }
  const u64 dependency_signature =
      client::chunk_meshing_dependency_signature(section, section_lookup);
  const voxel::ChunkSnapshot snapshot =
      client::chunk_meshing_snapshot_from_section_data(section, section_lookup);
  render::ChunkMeshData mesh = render::build_chunk_mesh(
      section.world_id,
      snapshot,
      g_app.block_assets.loaded.block_registry,
      &g_app.block_assets.loaded.block_atlas.render_data,
      &g_app.block_assets.loaded.fluid_render_data);
  mesh.source_version = section.chunk_version;
  mesh.source_content_version = section.chunk_content_version;
  mesh.source_light_version = section.chunk_light_version;

  render::ChunkDrawItem draw_item;
  draw_item.key = {section.world_id, section.coord};
  draw_item.expected_version = section.chunk_version;
  draw_item.expected_light_version = section.chunk_light_version;
  draw_item.has_opaque_mesh = !mesh.opaque.indices.empty();
  draw_item.has_transparent_mesh = !mesh.transparent.indices.empty();

  render::ChunkMeshUpload upload;
  upload.mesh = std::move(mesh);
  enqueue_web_chunk_mesh_upload(std::move(upload));

  WebChunkDrawState next_state;
  next_state.draw_item = draw_item;
  next_state.content_version = section.chunk_content_version;
  next_state.light_version = section.chunk_light_version;
  next_state.dependency_signature = dependency_signature;
  next_state.mesh_upload_confirmed = false;

  auto found = std::find_if(
      g_app.renderer.chunk_draws.begin(),
      g_app.renderer.chunk_draws.end(),
      [&](const WebChunkDrawState& state) {
        return state.draw_item.key == draw_item.key;
      });
  if (found == g_app.renderer.chunk_draws.end()) {
    g_app.renderer.chunk_draws.push_back(next_state);
  } else if (render::chunk_mesh_upload_at_least_as_new(
                 next_state.draw_item.expected_version,
                 next_state.draw_item.expected_light_version,
                 found->draw_item.expected_version,
                 found->draw_item.expected_light_version)) {
    *found = next_state;
  }
}

void drain_chunk_mesh_upload_receipts() {
  for (const render::ChunkMeshUploadReceipt& receipt :
       drain_web_chunk_mesh_upload_receipts()) {
    auto found = std::find_if(
        g_app.renderer.chunk_draws.begin(),
        g_app.renderer.chunk_draws.end(),
        [&](const WebChunkDrawState& state) {
          return state.draw_item.key == receipt.key;
        });
    if (found == g_app.renderer.chunk_draws.end()) {
      continue;
    }

    const WebChunkUploadReceiptResult result =
        apply_web_chunk_upload_receipt(*found, receipt);
    if (result != WebChunkUploadReceiptResult::Accepted) {
      continue;
    }

    if ((found->draw_item.has_opaque_mesh || found->draw_item.has_transparent_mesh) &&
        !g_app.renderer.announced_first_upload) {
      g_app.renderer.announced_first_upload = true;
      set_status("remote chunk mesh uploaded");
    }
  }
}

} // namespace

void sync_web_chunk_meshes() {
  if (g_app.session == nullptr) {
    clear_chunk_meshes();
    return;
  }
  if (!ensure_block_render_assets_loaded()) {
    return;
  }

  const std::vector<protocol::ClientboundChunkSectionData>& sections =
      g_app.chunk_stream.sections();
  const WorldID active_world_id =
      g_app.session == nullptr ? WorldID{} : g_app.session->camera().world_id();
  const client::RemoteChunkSectionIndex section_index =
      g_app.chunk_stream.build_section_index(active_world_id);
  auto section_lookup =
      [&](ChunkSectionCoord coord)
          -> const protocol::ClientboundChunkSectionData* {
    const auto found = section_index.find(coord);
    return found == section_index.end() ? nullptr : found->second;
  };
  const auto stale_begin = std::remove_if(
      g_app.renderer.chunk_draws.begin(),
      g_app.renderer.chunk_draws.end(),
      [&](const WebChunkDrawState& state) {
        const bool missing =
            state.draw_item.key.world_id != active_world_id ||
            section_index.find(state.draw_item.key.coord) == section_index.end();
        if (missing) {
          remove_web_chunk_mesh(state.draw_item.key);
        }
        return missing;
      });
  g_app.renderer.chunk_draws.erase(
      stale_begin,
      g_app.renderer.chunk_draws.end());

  drain_chunk_mesh_upload_receipts();

  int uploads_this_frame = 0;
  for (const protocol::ClientboundChunkSectionData& section : sections) {
    if (section.world_id != active_world_id) {
      continue;
    }
    const u64 dependency_signature =
        client::chunk_meshing_dependency_signature(
            section,
            client::ChunkSectionDataLookup{section_lookup});
    const auto found = std::find_if(
        g_app.renderer.chunk_draws.begin(),
        g_app.renderer.chunk_draws.end(),
        [&](const WebChunkDrawState& state) {
          return state.draw_item.key.world_id == section.world_id &&
                 state.draw_item.key.coord == section.coord;
        });
    if (found != g_app.renderer.chunk_draws.end() &&
        web_chunk_draw_state_is_current(
            *found,
            section.chunk_content_version,
            section.chunk_light_version,
            dependency_signature)) {
      continue;
    }
    upload_chunk_mesh(section, client::ChunkSectionDataLookup{section_lookup});
    if (++uploads_this_frame >= 1) {
      break;
    }
  }

  drain_chunk_mesh_upload_receipts();
}

void append_web_chunk_draws_to_frame(render::FramePacket& frame) {
  client::ChunkDrawAppendOptions options;
  options.frustum_cull = false;
  const auto item_at =
      [](usize index) -> const render::ChunkDrawItem& {
    return g_app.renderer.chunk_draws[index].draw_item;
  };
  (void)client::append_chunk_draw_items(
      frame,
      {.count = g_app.renderer.chunk_draws.size(),
       .item_at = FunctionRef<const render::ChunkDrawItem&(usize)>{item_at}},
      options);
}

} // namespace arv::web
