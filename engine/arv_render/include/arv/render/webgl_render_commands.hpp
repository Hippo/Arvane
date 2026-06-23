#pragma once

#include "arv/render/paged_texture_region_allocator.hpp"
#include "arv/render/render_commands.hpp"
#include "arv/render/webgl_renderer.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace arv::render {

class WebGLRenderCommandTarget final : public IRenderCommandTarget {
public:
  WebGLRenderCommandTarget();
  ~WebGLRenderCommandTarget() override;

  WebGLRenderCommandTarget(const WebGLRenderCommandTarget&) = delete;
  WebGLRenderCommandTarget& operator=(const WebGLRenderCommandTarget&) = delete;
  WebGLRenderCommandTarget(WebGLRenderCommandTarget&&) = delete;
  WebGLRenderCommandTarget& operator=(WebGLRenderCommandTarget&&) = delete;

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
  [[nodiscard]] WebGLTextureArrayView player_skin_atlas_view() const noexcept;
  [[nodiscard]] std::optional<WebGLAtlasRegion> player_skin_atlas_region(
      TextureRegionHandle handle) const;

  void set_fallback_color_vertices(std::vector<WebGLColorVertex> vertices);
  void clear_fallback_color_vertices();

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
  struct TextureArrayState {
    u32 texture{};
    u32 width{};
    u32 height{};
    u32 layers{};
  };

  struct ResidentPlayerSkin {
    std::string skin_hash{};
    TextureRegionHandle handle{};
    u16 width{};
    u16 height{};
    u64 last_used_frame{};
    bool pinned_this_frame{};
  };

  struct ChunkMeshRecord {
    WebGLChunkSectionMesh mesh{};
    ChunkRenderState state{};
  };

  void initialize_default_programs();
  void destroy_resources();
  void record_diagnostic(std::string message);
  [[nodiscard]] bool replace_texture(
      TextureArrayState& target,
      WebGLTextureArrayUploadResult upload);
  [[nodiscard]] WebGLTextureArrayView texture_view(const TextureArrayState& state) const noexcept;
  [[nodiscard]] std::optional<WebGLAtlasRegion> resolve_player_skin_region(
      TextureRegionHandle handle) const;
  [[nodiscard]] std::vector<WebGLChunkSectionMesh> collect_chunk_meshes(
      const std::vector<ChunkDrawItem>& draws,
      WebGLChunkMeshLayer layer) const;
  void publish_empty_metrics();

  u32 viewport_width_{};
  u32 viewport_height_{};
  FramePacket frame_{};
  bool initialized_{};
  bool shutdown_{};

  WebGLSkyRenderer sky_renderer_{};
  WebGLChunkRenderer chunk_renderer_{};
  WebGLColorRenderer color_renderer_{};
  WebGLEntityRenderer entity_renderer_{};
  WebGLParticleRenderer particle_renderer_{};
  WebGLUiRenderer ui_renderer_{};
  WebGLArrayMesh fallback_color_mesh_{};

  TextureArrayState chunk_atlas_{};
  TextureArrayState entity_atlas_{};
  TextureArrayState particle_atlas_{};
  TextureArrayState ui_font_atlas_{};
  TextureArrayState ui_item_atlas_{};
  TextureArrayState player_skin_atlas_texture_{};
  PlayerSkinAtlasConfig player_skin_config_{};
  PagedTextureRegionAllocator player_skin_allocator_{};
  HashMap<std::string, usize> player_skin_by_hash_{};
  HashMap<u32, usize> player_skin_by_handle_id_{};
  std::vector<ResidentPlayerSkin> player_skins_{};

  HashMap<EntityModelID, WebGLIndexedMesh> gpu_entity_models_{};
  std::vector<EntityModelAsset> entity_models_{};
  std::vector<EntityAnimationAsset> entity_animations_{};

  HashMap<ChunkMeshKey, ChunkMeshRecord> chunk_meshes_{};
  std::vector<ChunkMeshUploadReceipt> chunk_mesh_upload_receipts_{};
  u32 chunk_mesh_upload_budget_{1U};

  std::vector<RenderCommandDiagnostic> diagnostics_{};
  RenderMetricsSnapshot metrics_{};
  u64 frame_index_{};
};

} // namespace arv::render
