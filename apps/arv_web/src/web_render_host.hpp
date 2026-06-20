#pragma once

#include "web_entity_fallback.hpp"

#include "arv/core/types.hpp"
#include "arv/render/entity_model.hpp"
#include "arv/render/render_types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::web {

class WebRenderHost final {
public:
  WebRenderHost();
  ~WebRenderHost();

  WebRenderHost(const WebRenderHost&) = delete;
  WebRenderHost& operator=(const WebRenderHost&) = delete;
  WebRenderHost(WebRenderHost&&) = delete;
  WebRenderHost& operator=(WebRenderHost&&) = delete;

  void resize(i32 width, i32 height);
  void clear_frame(f32 red, f32 green, f32 blue, f32 alpha);

  [[nodiscard]] bool set_chunk_texture_atlas(render::TextureArrayUploadData upload);
  [[nodiscard]] bool set_entity_texture_atlas(render::TextureArrayUploadData upload);
  [[nodiscard]] bool set_particle_texture_atlas(render::TextureArrayUploadData upload);
  [[nodiscard]] bool set_ui_item_atlas(render::TextureArrayUploadData upload);
  [[nodiscard]] bool set_ui_font_atlas(render::TextureArrayUploadData upload);

  void configure_player_skin_atlas(render::PlayerSkinAtlasConfig config);
  [[nodiscard]] std::optional<render::TextureRegionHandle> upload_player_skin_texture(
      std::string skin_hash,
      render::TextureUploadData upload);
  [[nodiscard]] bool player_skin_texture_ready(
      std::string_view skin_hash,
      render::TextureRegionHandle handle) const;
  [[nodiscard]] bool mark_player_skin_texture_used(
      render::TextureRegionHandle handle,
      bool pinned);

  void set_fallback_color_vertices(std::vector<WebFallbackColorVertex> vertices);
  void clear_fallback_color_vertices();

  void clear_entity_models();
  void set_entity_model(render::EntityModelAsset model);
  void clear_entity_animations();
  void set_entity_animation(render::EntityAnimationAsset animation);

  void set_chunk_mesh_upload_budget(u32 max_uploads_per_frame);
  void enqueue_chunk_mesh_upload(render::ChunkMeshUpload upload);
  void remove_chunk_mesh(render::ChunkMeshKey key);
  [[nodiscard]] std::vector<render::ChunkMeshUploadReceipt>
  drain_chunk_mesh_upload_receipts();

  void submit_and_render(render::FramePacket packet);

private:
  struct Backend;

  [[nodiscard]] bool report_diagnostics();

  std::unique_ptr<Backend> backend_;
};

[[nodiscard]] bool initialize_web_render_host();
void shutdown_web_render_host();
[[nodiscard]] bool web_render_host_ready() noexcept;
void resize_web_render_host(i32 width, i32 height);
void clear_web_render_frame(f32 red, f32 green, f32 blue, f32 alpha);
[[nodiscard]] bool set_web_chunk_texture_atlas(render::TextureArrayUploadData upload);
[[nodiscard]] bool set_web_entity_texture_atlas(render::TextureArrayUploadData upload);
[[nodiscard]] bool set_web_particle_texture_atlas(render::TextureArrayUploadData upload);
[[nodiscard]] bool set_web_ui_item_atlas(render::TextureArrayUploadData upload);
[[nodiscard]] bool set_web_ui_font_atlas(render::TextureArrayUploadData upload);
void configure_web_player_skin_atlas(render::PlayerSkinAtlasConfig config);
[[nodiscard]] std::optional<render::TextureRegionHandle>
upload_web_player_skin_texture_to_render_host(
    std::string skin_hash,
    render::TextureUploadData upload);
[[nodiscard]] bool web_player_skin_texture_ready(
    std::string_view skin_hash,
    render::TextureRegionHandle handle) noexcept;
[[nodiscard]] bool mark_web_player_skin_texture_used(
    render::TextureRegionHandle handle,
    bool pinned) noexcept;
void set_web_fallback_color_vertices(std::vector<WebFallbackColorVertex> vertices);
void clear_web_fallback_color_vertices();
void clear_web_entity_models();
void set_web_entity_model(render::EntityModelAsset model);
void clear_web_entity_animations();
void set_web_entity_animation(render::EntityAnimationAsset animation);
void set_web_chunk_mesh_upload_budget(u32 max_uploads_per_frame);
void enqueue_web_chunk_mesh_upload(render::ChunkMeshUpload upload);
void remove_web_chunk_mesh(render::ChunkMeshKey key);
[[nodiscard]] std::vector<render::ChunkMeshUploadReceipt>
drain_web_chunk_mesh_upload_receipts();
void submit_and_render_web_frame(render::FramePacket packet);

} // namespace arv::web
