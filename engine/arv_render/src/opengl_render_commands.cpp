#include "arv/render/opengl_render_commands.hpp"

namespace arv::render {

OpenGLRenderCommandTarget::OpenGLRenderCommandTarget(OpenGLRenderer& renderer) : renderer_(renderer) {
}

void OpenGLRenderCommandTarget::resize(u32 width, u32 height) {
  renderer_.resize(width, height);
}

void OpenGLRenderCommandTarget::submit_frame(FramePacket packet) {
  renderer_.submit_frame(std::move(packet));
}

void OpenGLRenderCommandTarget::render() {
  renderer_.render();
}

void OpenGLRenderCommandTarget::begin_shutdown() {
  renderer_.begin_shutdown();
}

void OpenGLRenderCommandTarget::set_chunk_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  renderer_.set_chunk_shader_sources(std::move(vertex_source), std::move(fragment_source));
}

void OpenGLRenderCommandTarget::set_entity_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  renderer_.set_entity_shader_sources(std::move(vertex_source), std::move(fragment_source));
}

void OpenGLRenderCommandTarget::set_ui_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  renderer_.set_ui_shader_sources(std::move(vertex_source), std::move(fragment_source));
}

void OpenGLRenderCommandTarget::set_sky_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  renderer_.set_sky_shader_sources(std::move(vertex_source), std::move(fragment_source));
}

void OpenGLRenderCommandTarget::set_particle_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  renderer_.set_particle_shader_sources(std::move(vertex_source), std::move(fragment_source));
}

void OpenGLRenderCommandTarget::set_chunk_texture_atlas(TextureArrayUploadData upload) {
  renderer_.set_chunk_texture_atlas(std::move(upload));
}

void OpenGLRenderCommandTarget::set_entity_texture_atlas(TextureArrayUploadData upload) {
  renderer_.set_entity_texture_atlas(std::move(upload));
}

void OpenGLRenderCommandTarget::set_particle_texture_atlas(TextureArrayUploadData upload) {
  renderer_.set_particle_texture_atlas(std::move(upload));
}

void OpenGLRenderCommandTarget::set_ui_font_atlas(TextureArrayUploadData upload) {
  renderer_.set_ui_font_atlas(std::move(upload));
}

void OpenGLRenderCommandTarget::set_ui_item_atlas(TextureArrayUploadData upload) {
  renderer_.set_ui_item_atlas(std::move(upload));
}

void OpenGLRenderCommandTarget::configure_player_skin_atlas(PlayerSkinAtlasConfig config) {
  renderer_.configure_player_skin_atlas(config);
}

std::optional<TextureRegionHandle> OpenGLRenderCommandTarget::upload_player_skin_texture(
    std::string skin_hash,
    TextureUploadData upload) {
  return renderer_.upload_player_skin_texture(std::move(skin_hash), std::move(upload));
}

std::optional<PlayerSkinTextureSnapshot>
OpenGLRenderCommandTarget::player_skin_texture_snapshot(
    std::string_view skin_hash,
    TextureRegionHandle handle) const {
  const ResidentSkinTexture* resident = renderer_.resident_player_skin_texture(skin_hash);
  const std::optional<AtlasRegion> region = renderer_.resolve_player_skin_texture(handle);
  if (resident == nullptr || !region.has_value()) {
    return std::nullopt;
  }

  PlayerSkinTextureSnapshot snapshot;
  snapshot.region = region.value();
  snapshot.atlas_width = renderer_.player_skin_atlas_page_width();
  snapshot.atlas_height = renderer_.player_skin_atlas_page_height();
  snapshot.image_width = resident->width;
  snapshot.image_height = resident->height;
  return snapshot;
}

bool OpenGLRenderCommandTarget::mark_player_skin_texture_used(
    TextureRegionHandle handle,
    bool pinned) {
  return renderer_.mark_player_skin_texture_used(handle, pinned);
}

void OpenGLRenderCommandTarget::set_entity_model(EntityModelAsset model) {
  renderer_.set_entity_model(std::move(model));
}

void OpenGLRenderCommandTarget::set_entity_animation(EntityAnimationAsset animation) {
  renderer_.set_entity_animation(std::move(animation));
}

void OpenGLRenderCommandTarget::clear_entity_models() {
  renderer_.clear_entity_models();
}

void OpenGLRenderCommandTarget::clear_entity_animations() {
  renderer_.clear_entity_animations();
}

void OpenGLRenderCommandTarget::set_chunk_mesh_upload_budget(u32 max_uploads_per_frame) {
  renderer_.set_chunk_mesh_upload_budget(max_uploads_per_frame);
}

void OpenGLRenderCommandTarget::enqueue_chunk_mesh_upload(ChunkMeshUpload upload) {
  renderer_.enqueue_chunk_mesh_upload(std::move(upload));
}

void OpenGLRenderCommandTarget::remove_chunk_mesh(ChunkMeshKey key) {
  renderer_.remove_chunk_mesh(key);
}

std::vector<ChunkMeshUploadReceipt>
OpenGLRenderCommandTarget::drain_chunk_mesh_upload_receipts() {
  return renderer_.drain_chunk_mesh_upload_receipts();
}

std::vector<RenderCommandDiagnostic> OpenGLRenderCommandTarget::drain_diagnostics() {
  return {};
}

RenderMetricsSnapshot OpenGLRenderCommandTarget::metrics_snapshot() const {
  RenderMetricsSnapshot snapshot;
  snapshot.pending_mesh_upload_count = renderer_.pending_mesh_upload_count();
  snapshot.mesh_uploads_last_frame = renderer_.mesh_uploads_last_frame();
  snapshot.mesh_upload_time_ms_last_frame = renderer_.mesh_upload_time_ms_last_frame();
  snapshot.mesh_upload_discards_last_frame = renderer_.mesh_upload_discards_last_frame();
  snapshot.draw_calls_last_frame = renderer_.draw_calls_last_frame();
  snapshot.chunk_draw_calls_last_frame = renderer_.chunk_draw_calls_last_frame();
  snapshot.ui_draw_calls_last_frame = renderer_.ui_draw_calls_last_frame();
  snapshot.vertices_submitted_last_frame = renderer_.vertices_submitted_last_frame();
  snapshot.triangles_submitted_last_frame = renderer_.triangles_submitted_last_frame();
  snapshot.gpu_buffer_uploads_last_frame = renderer_.gpu_buffer_uploads_last_frame();
  snapshot.gpu_buffer_upload_bytes_last_frame =
      renderer_.gpu_buffer_upload_bytes_last_frame();
  snapshot.texture_uploads_last_frame = renderer_.texture_uploads_last_frame();
  snapshot.texture_upload_bytes_last_frame = renderer_.texture_upload_bytes_last_frame();
  snapshot.gpu_profile_context_initialized = renderer_.gpu_profile_context_initialized();
  return snapshot;
}

} // namespace arv::render
