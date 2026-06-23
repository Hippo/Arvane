#pragma once

#include "arv/render/opengl_renderer.hpp"
#include "arv/render/render_commands.hpp"

namespace arv::render {

class OpenGLRenderCommandTarget final : public IRenderCommandTarget {
public:
  explicit OpenGLRenderCommandTarget(OpenGLRenderer& renderer);

  void resize(u32 width, u32 height) override;
  void submit_frame(FramePacket packet) override;
  void render() override;
  void begin_shutdown() override;

  void set_chunk_shader_sources(
      std::string vertex_source,
      std::string fragment_source) override;
  void set_entity_shader_sources(
      std::string vertex_source,
      std::string fragment_source) override;
  void set_ui_shader_sources(
      std::string vertex_source,
      std::string fragment_source) override;
  void set_sky_shader_sources(
      std::string vertex_source,
      std::string fragment_source) override;
  void set_particle_shader_sources(
      std::string vertex_source,
      std::string fragment_source) override;
  void set_contact_shadow_shader_sources(
      std::string vertex_source,
      std::string fragment_source) override;

  void set_chunk_texture_atlas(TextureArrayUploadData upload) override;
  void set_entity_texture_atlas(TextureArrayUploadData upload) override;
  void set_particle_texture_atlas(TextureArrayUploadData upload) override;
  void set_ui_font_atlas(TextureArrayUploadData upload) override;
  void set_ui_item_atlas(TextureArrayUploadData upload) override;

  void configure_player_skin_atlas(PlayerSkinAtlasConfig config) override;
  [[nodiscard]] std::optional<TextureRegionHandle> upload_player_skin_texture(
      std::string skin_hash,
      TextureUploadData upload) override;
  [[nodiscard]] std::optional<PlayerSkinTextureSnapshot> player_skin_texture_snapshot(
      std::string_view skin_hash,
      TextureRegionHandle handle) const override;
  [[nodiscard]] bool mark_player_skin_texture_used(
      TextureRegionHandle handle,
      bool pinned) override;

  void set_entity_model(EntityModelAsset model) override;
  void set_entity_animation(EntityAnimationAsset animation) override;
  void clear_entity_models() override;
  void clear_entity_animations() override;

  void set_chunk_mesh_upload_budget(u32 max_uploads_per_frame) override;
  void enqueue_chunk_mesh_upload(ChunkMeshUpload upload) override;
  void remove_chunk_mesh(ChunkMeshKey key) override;
  [[nodiscard]] std::vector<ChunkMeshUploadReceipt>
  drain_chunk_mesh_upload_receipts() override;

  [[nodiscard]] std::vector<RenderCommandDiagnostic>
  drain_diagnostics() override;
  [[nodiscard]] RenderMetricsSnapshot metrics_snapshot() const override;

private:
  OpenGLRenderer& renderer_;
};

} // namespace arv::render
