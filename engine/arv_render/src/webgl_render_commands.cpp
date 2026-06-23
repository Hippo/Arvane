#include "arv/render/webgl_render_commands.hpp"

#include "arv/render/webgl_shader.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace arv::render {
namespace {

[[nodiscard]] WebGLTextureArraySampling sampling_for_upload(
    const TextureArrayUploadData& upload) noexcept {
  return upload.limit_mipmap_levels
      ? WebGLTextureArraySampling::NearestMipmap
      : WebGLTextureArraySampling::Nearest;
}

[[nodiscard]] bool fits_u16(u32 value) noexcept {
  return value <= static_cast<u32>(std::numeric_limits<u16>::max());
}

[[nodiscard]] bool has_mesh_for_layer(
    const WebGLChunkSectionMesh& mesh,
    WebGLChunkMeshLayer layer) noexcept {
  return layer == WebGLChunkMeshLayer::Opaque
      ? mesh.opaque.index_count > 0
      : mesh.transparent.index_count > 0;
}

[[nodiscard]] bool cpu_chunk_mesh_has_data(const CpuMeshBuffer& mesh) noexcept {
  return !mesh.vertex_bytes.empty() && !mesh.indices.empty();
}

[[nodiscard]] bool webgl_indexed_mesh_upload_failed(
    const CpuMeshBuffer& source,
    const WebGLIndexedMesh& mesh) noexcept {
  return cpu_chunk_mesh_has_data(source) &&
         (mesh.vao == 0U || mesh.vbo == 0U || mesh.ebo == 0U ||
          mesh.index_count <= 0);
}

} // namespace

WebGLRenderCommandTarget::WebGLRenderCommandTarget() {
  configure_default_webgl_state();
  initialize_default_programs();
  player_skin_allocator_.configure({
      player_skin_config_.page_size,
      player_skin_config_.page_size,
      player_skin_config_.max_pages});
  metrics_.gpu_profile_context_initialized = true;
}

WebGLRenderCommandTarget::~WebGLRenderCommandTarget() {
  begin_shutdown();
}

void WebGLRenderCommandTarget::initialize_default_programs() {
  if (initialized_ || shutdown_) {
    return;
  }

  const WebGLProgramSetLinkResult programs = link_default_webgl_programs();
  if (!programs) {
    record_diagnostic("failed to link default WebGL renderer programs: " + programs.error);
    return;
  }

  sky_renderer_ = create_webgl_sky_renderer(programs.sky_program);
  chunk_renderer_ = create_webgl_chunk_renderer(programs.chunk_program);
  color_renderer_ = create_webgl_color_renderer(programs.color_program);
  entity_renderer_ = create_webgl_entity_renderer(programs.entity_program);
  particle_renderer_ = create_webgl_particle_renderer(programs.particle_program);
  ui_renderer_ = create_webgl_ui_renderer(programs.ui_program);
  initialized_ = true;
}

void WebGLRenderCommandTarget::resize(u32 width, u32 height) {
  viewport_width_ = width;
  viewport_height_ = height;
  resize_webgl_viewport(static_cast<i32>(width), static_cast<i32>(height));
}

void WebGLRenderCommandTarget::submit_frame(FramePacket packet) {
  frame_ = std::move(packet);
}

void WebGLRenderCommandTarget::render() {
  if (shutdown_) {
    return;
  }
  initialize_default_programs();

  metrics_ = {};
  metrics_.gpu_profile_context_initialized = initialized_;
  metrics_.pending_mesh_upload_count = 0U;
  ++frame_index_;

  configure_default_webgl_state();
  clear_webgl_frame(
      frame_.clear_color.x,
      frame_.clear_color.y,
      frame_.clear_color.z,
      frame_.clear_color.w);

  RenderView view = frame_.world_view;
  if (view.viewport_width == 0U || view.viewport_height == 0U) {
    view = render_view_from_camera(frame_.camera);
  }
  const u32 viewport_width = viewport_width_ == 0U ? view.viewport_width : viewport_width_;
  const u32 viewport_height = viewport_height_ == 0U ? view.viewport_height : viewport_height_;

  draw_webgl_sky(
      sky_renderer_,
      frame_.sky,
      view.forward,
      view.right,
      view.up,
      static_cast<f32>(std::max(viewport_width, 1U)) /
          static_cast<f32>(std::max(viewport_height, 1U)),
      view.fov_y_degrees);

  const std::vector<WebGLChunkSectionMesh> opaque_chunks =
      collect_chunk_meshes(frame_.opaque_chunks, WebGLChunkMeshLayer::Opaque);
  const std::vector<WebGLChunkSectionMesh> transparent_chunks =
      collect_chunk_meshes(frame_.transparent_chunks, WebGLChunkMeshLayer::Transparent);

  if (!opaque_chunks.empty() && chunk_atlas_.texture != 0U) {
    draw_webgl_chunk_meshes(
        chunk_renderer_,
        opaque_chunks,
        texture_view(chunk_atlas_),
        view.view_projection,
        frame_.sky.cloud_time_seconds,
        1.0F,
        WebGLChunkMeshLayer::Opaque);
    ++metrics_.chunk_draw_calls_last_frame;
    ++metrics_.draw_calls_last_frame;
  }

  if (!frame_.entities.empty()) {
    draw_webgl_entity_models(
        entity_renderer_,
        frame_.entities,
        gpu_entity_models_,
        entity_models_,
        entity_animations_,
        texture_view(entity_atlas_),
        texture_view(player_skin_atlas_texture_),
        view.view_projection,
        view.position,
        false,
        [this](TextureRegionHandle handle) {
          return resolve_player_skin_region(handle);
        });
    ++metrics_.draw_calls_last_frame;
  }

  const bool should_draw_transparent_chunks =
      !transparent_chunks.empty() && chunk_atlas_.texture != 0U;
  const bool should_draw_translucent_entities = !frame_.entities.empty();
  const bool should_draw_particles =
      !frame_.particles.empty() && particle_atlas_.texture != 0U;
  if (should_draw_transparent_chunks || should_draw_translucent_entities ||
      should_draw_particles) {
    begin_webgl_translucent_pass();
    if (should_draw_transparent_chunks) {
      draw_webgl_chunk_meshes(
          chunk_renderer_,
          transparent_chunks,
          texture_view(chunk_atlas_),
          view.view_projection,
          frame_.sky.cloud_time_seconds,
          0.55F,
          WebGLChunkMeshLayer::Transparent);
      ++metrics_.chunk_draw_calls_last_frame;
      ++metrics_.draw_calls_last_frame;
    }
    if (should_draw_translucent_entities) {
      draw_webgl_entity_models(
          entity_renderer_,
          frame_.entities,
          gpu_entity_models_,
          entity_models_,
          entity_animations_,
          texture_view(entity_atlas_),
          texture_view(player_skin_atlas_texture_),
          view.view_projection,
          view.position,
          true,
          [this](TextureRegionHandle handle) {
            return resolve_player_skin_region(handle);
          });
      ++metrics_.draw_calls_last_frame;
    }
    const usize alpha_particles = draw_webgl_particles(
        particle_renderer_,
        frame_.particles,
        texture_view(particle_atlas_),
        view,
        viewport_width,
        viewport_height,
        ParticleRenderBlendMode::Alpha);
    if (alpha_particles > 0U) {
      ++metrics_.draw_calls_last_frame;
      metrics_.vertices_submitted_last_frame += alpha_particles * 4U;
      metrics_.triangles_submitted_last_frame += alpha_particles * 2U;
    }
    const usize additive_particles = draw_webgl_particles(
        particle_renderer_,
        frame_.particles,
        texture_view(particle_atlas_),
        view,
        viewport_width,
        viewport_height,
        ParticleRenderBlendMode::Additive);
    if (additive_particles > 0U) {
      ++metrics_.draw_calls_last_frame;
      metrics_.vertices_submitted_last_frame += additive_particles * 4U;
      metrics_.triangles_submitted_last_frame += additive_particles * 2U;
    }
    end_webgl_translucent_pass();
  }

  if (fallback_color_mesh_.vertex_count > 0) {
    draw_webgl_color_mesh(
        color_renderer_,
        fallback_color_mesh_,
        view.view_projection);
    ++metrics_.draw_calls_last_frame;
  }

  if (!frame_.ui_rects.empty() ||
      !frame_.ui_textured_quads.empty() ||
      !frame_.ui_glyphs.empty()) {
    draw_webgl_ui_frame(
        frame_,
        ui_renderer_,
        static_cast<i32>(viewport_width),
        static_cast<i32>(viewport_height),
        {
            .terrain_texture = chunk_atlas_.texture,
            .item_texture = ui_item_atlas_.texture,
            .font_texture = ui_font_atlas_.texture,
        });
    ++metrics_.ui_draw_calls_last_frame;
    ++metrics_.draw_calls_last_frame;
  }

  reset_webgl_draw_bindings();
}

void WebGLRenderCommandTarget::begin_shutdown() {
  if (shutdown_) {
    return;
  }
  destroy_resources();
  shutdown_ = true;
}

void WebGLRenderCommandTarget::set_chunk_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  const WebGLProgramLinkResult program = link_webgl_program(
      "chunk",
      vertex_source.c_str(),
      fragment_source.c_str());
  if (program) {
    chunk_renderer_ = create_webgl_chunk_renderer(program.program);
  } else {
    record_diagnostic("failed to link WebGL chunk shader: " + program.error);
  }
}

void WebGLRenderCommandTarget::set_entity_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  const WebGLProgramLinkResult program = link_webgl_program(
      "entity",
      vertex_source.c_str(),
      fragment_source.c_str());
  if (program) {
    entity_renderer_ = create_webgl_entity_renderer(program.program);
  } else {
    record_diagnostic("failed to link WebGL entity shader: " + program.error);
  }
}

void WebGLRenderCommandTarget::set_ui_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  const WebGLProgramLinkResult program = link_webgl_program(
      "ui",
      vertex_source.c_str(),
      fragment_source.c_str());
  if (program) {
    destroy_webgl_ui_renderer(ui_renderer_);
    ui_renderer_ = create_webgl_ui_renderer(program.program);
  } else {
    record_diagnostic("failed to link WebGL UI shader: " + program.error);
  }
}

void WebGLRenderCommandTarget::set_sky_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  const WebGLProgramLinkResult program = link_webgl_program(
      "sky",
      vertex_source.c_str(),
      fragment_source.c_str());
  if (program) {
    destroy_webgl_sky_renderer(sky_renderer_);
    sky_renderer_ = create_webgl_sky_renderer(program.program);
  } else {
    record_diagnostic("failed to link WebGL sky shader: " + program.error);
  }
}

void WebGLRenderCommandTarget::set_particle_shader_sources(
    std::string vertex_source,
    std::string fragment_source) {
  const WebGLProgramLinkResult program = link_webgl_program(
      "particle",
      vertex_source.c_str(),
      fragment_source.c_str());
  if (program) {
    destroy_webgl_particle_renderer(particle_renderer_);
    particle_renderer_ = create_webgl_particle_renderer(program.program);
  } else {
    record_diagnostic("failed to link WebGL particle shader: " + program.error);
  }
}

void WebGLRenderCommandTarget::set_contact_shadow_shader_sources(
    std::string /*vertex_source*/,
    std::string /*fragment_source*/) {
}

void WebGLRenderCommandTarget::set_chunk_texture_atlas(TextureArrayUploadData upload) {
  (void)replace_texture(
      chunk_atlas_,
      upload_webgl_texture_array("chunk atlas", upload, sampling_for_upload(upload)));
}

void WebGLRenderCommandTarget::set_entity_texture_atlas(TextureArrayUploadData upload) {
  (void)replace_texture(
      entity_atlas_,
      upload_webgl_texture_array("entity atlas", upload, sampling_for_upload(upload)));
}

void WebGLRenderCommandTarget::set_particle_texture_atlas(TextureArrayUploadData upload) {
  (void)replace_texture(
      particle_atlas_,
      upload_webgl_texture_array("particle atlas", upload, sampling_for_upload(upload)));
}

void WebGLRenderCommandTarget::set_ui_font_atlas(TextureArrayUploadData upload) {
  (void)replace_texture(
      ui_font_atlas_,
      upload_webgl_texture_array("font atlas", upload, WebGLTextureArraySampling::Linear));
}

void WebGLRenderCommandTarget::set_ui_item_atlas(TextureArrayUploadData upload) {
  (void)replace_texture(
      ui_item_atlas_,
      upload_webgl_texture_array("item atlas", upload, sampling_for_upload(upload)));
}

void WebGLRenderCommandTarget::configure_player_skin_atlas(PlayerSkinAtlasConfig config) {
  player_skin_config_ = config;
  delete_webgl_texture(player_skin_atlas_texture_.texture);
  player_skin_atlas_texture_ = {};
  player_skin_allocator_.clear();
  player_skin_by_hash_.clear();
  player_skin_by_handle_id_.clear();
  player_skins_.clear();

  const WebGLTextureArrayLimits limits =
      query_webgl_texture_array_limits(config.page_size, config.max_pages);
  const u32 page_size = std::max<u32>(
      1U,
      std::min<u32>(config.page_size, limits.max_texture_size));
  const u32 layers = std::max<u32>(
      1U,
      std::min<u32>(config.max_pages, limits.max_array_layers));
  if (!fits_u16(page_size) || !fits_u16(layers)) {
    record_diagnostic("player skin atlas exceeds supported WebGL atlas dimensions");
    return;
  }
  player_skin_allocator_.configure({
      static_cast<u16>(page_size),
      static_cast<u16>(page_size),
      static_cast<u16>(layers)});
  (void)replace_texture(
      player_skin_atlas_texture_,
      allocate_empty_webgl_texture_array(
          "player skin atlas",
          page_size,
          page_size,
          layers,
          WebGLTextureArraySampling::Nearest));
}

std::optional<TextureRegionHandle> WebGLRenderCommandTarget::upload_player_skin_texture(
    std::string skin_hash,
    TextureUploadData upload) {
  if (skin_hash.empty() ||
      !upload.valid() ||
      !fits_u16(upload.width) ||
      !fits_u16(upload.height)) {
    record_diagnostic("player skin texture upload was invalid");
    return std::nullopt;
  }

  const auto existing = player_skin_by_hash_.find(skin_hash);
  if (existing != player_skin_by_hash_.end() && existing->second < player_skins_.size()) {
    ResidentPlayerSkin& resident = player_skins_[existing->second];
    resident.last_used_frame = frame_index_;
    resident.pinned_this_frame = true;
    return resident.handle;
  }

  if (player_skin_atlas_texture_.texture == 0U) {
    configure_player_skin_atlas(player_skin_config_);
  }
  if (player_skin_atlas_texture_.texture == 0U) {
    record_diagnostic("player skin atlas texture is not available");
    return std::nullopt;
  }

  const auto handle = player_skin_allocator_.allocate(
      static_cast<u16>(upload.width),
      static_cast<u16>(upload.height));
  if (!handle.has_value()) {
    record_diagnostic("player skin atlas has no free region for uploaded skin");
    return std::nullopt;
  }
  const auto region = player_skin_allocator_.resolve(handle.value());
  if (!region.has_value()) {
    record_diagnostic("player skin atlas region handle could not be resolved");
    return std::nullopt;
  }

  const WebGLTextureArrayRegionUploadResult upload_result =
      upload_webgl_texture_array_region(
          "player skin atlas",
          player_skin_atlas_texture_.texture,
          region->x,
          region->y,
          region->page_index,
          upload.width,
          upload.height,
          upload.rgba_pixels);
  if (!upload_result) {
    player_skin_allocator_.free(handle.value());
    record_diagnostic(upload_result.error);
    return std::nullopt;
  }

  const usize index = player_skins_.size();
  ResidentPlayerSkin resident;
  resident.skin_hash = std::move(skin_hash);
  resident.handle = handle.value();
  resident.width = static_cast<u16>(upload.width);
  resident.height = static_cast<u16>(upload.height);
  resident.last_used_frame = frame_index_;
  resident.pinned_this_frame = true;
  player_skins_.push_back(std::move(resident));
  player_skin_by_hash_[player_skins_[index].skin_hash] = index;
  player_skin_by_handle_id_[player_skins_[index].handle.id] = index;
  ++metrics_.texture_uploads_last_frame;
  metrics_.texture_upload_bytes_last_frame += upload.rgba_pixels.size();
  return handle;
}

std::optional<PlayerSkinTextureSnapshot>
WebGLRenderCommandTarget::player_skin_texture_snapshot(
    std::string_view skin_hash,
    TextureRegionHandle handle) const {
  const auto found = player_skin_by_hash_.find(std::string{skin_hash});
  if (found == player_skin_by_hash_.end() || found->second >= player_skins_.size()) {
    return std::nullopt;
  }
  const ResidentPlayerSkin& resident = player_skins_[found->second];
  if (!(resident.handle == handle)) {
    return std::nullopt;
  }
  const std::optional<AtlasRegion> region = player_skin_allocator_.resolve(handle);
  if (!region.has_value()) {
    return std::nullopt;
  }

  PlayerSkinTextureSnapshot snapshot;
  snapshot.region = region.value();
  snapshot.atlas_width = player_skin_allocator_.page_width();
  snapshot.atlas_height = player_skin_allocator_.page_height();
  snapshot.image_width = resident.width;
  snapshot.image_height = resident.height;
  return snapshot;
}

bool WebGLRenderCommandTarget::mark_player_skin_texture_used(
    TextureRegionHandle handle,
    bool pinned) {
  const auto found = player_skin_by_handle_id_.find(handle.id);
  if (found == player_skin_by_handle_id_.end() || found->second >= player_skins_.size()) {
    return false;
  }
  ResidentPlayerSkin& resident = player_skins_[found->second];
  if (!(resident.handle == handle)) {
    return false;
  }
  resident.last_used_frame = frame_index_;
  resident.pinned_this_frame = pinned;
  return true;
}

WebGLTextureArrayView WebGLRenderCommandTarget::player_skin_atlas_view() const noexcept {
  return texture_view(player_skin_atlas_texture_);
}

std::optional<WebGLAtlasRegion> WebGLRenderCommandTarget::player_skin_atlas_region(
    TextureRegionHandle handle) const {
  return resolve_player_skin_region(handle);
}

void WebGLRenderCommandTarget::set_fallback_color_vertices(
    std::vector<WebGLColorVertex> vertices) {
  destroy_webgl_array_mesh(fallback_color_mesh_);
  if (vertices.empty()) {
    return;
  }
  fallback_color_mesh_ =
      upload_webgl_color_mesh(vertices, WebGLBufferUsage::Dynamic);
}

void WebGLRenderCommandTarget::clear_fallback_color_vertices() {
  destroy_webgl_array_mesh(fallback_color_mesh_);
}

void WebGLRenderCommandTarget::set_entity_model(EntityModelAsset model) {
  WebGLIndexedMesh uploaded_mesh = upload_webgl_entity_model_mesh(model);
  if ((!model.vertices.empty() || !model.indices.empty()) &&
      (uploaded_mesh.vao == 0U || uploaded_mesh.vbo == 0U ||
       uploaded_mesh.ebo == 0U || uploaded_mesh.index_count <= 0)) {
    destroy_webgl_indexed_mesh(uploaded_mesh);
    record_diagnostic("entity model mesh upload failed");
    return;
  }

  for (auto& [id, mesh] : gpu_entity_models_) {
    if (id == model.id) {
      destroy_webgl_indexed_mesh(mesh);
      break;
    }
  }
  gpu_entity_models_[model.id] = uploaded_mesh;

  const auto existing = std::find_if(
      entity_models_.begin(),
      entity_models_.end(),
      [&](const EntityModelAsset& candidate) {
        return candidate.id == model.id;
      });
  if (existing == entity_models_.end()) {
    entity_models_.push_back(std::move(model));
  } else {
    *existing = std::move(model);
  }
}

void WebGLRenderCommandTarget::set_entity_animation(EntityAnimationAsset animation) {
  const auto existing = std::find_if(
      entity_animations_.begin(),
      entity_animations_.end(),
      [&](const EntityAnimationAsset& candidate) {
        return candidate.id == animation.id;
      });
  if (existing == entity_animations_.end()) {
    entity_animations_.push_back(std::move(animation));
  } else {
    *existing = std::move(animation);
  }
}

void WebGLRenderCommandTarget::clear_entity_models() {
  for (auto& [id, mesh] : gpu_entity_models_) {
    (void)id;
    destroy_webgl_indexed_mesh(mesh);
  }
  gpu_entity_models_.clear();
  entity_models_.clear();
}

void WebGLRenderCommandTarget::clear_entity_animations() {
  entity_animations_.clear();
}

void WebGLRenderCommandTarget::set_chunk_mesh_upload_budget(u32 max_uploads_per_frame) {
  chunk_mesh_upload_budget_ = std::max<u32>(max_uploads_per_frame, 1U);
}

void WebGLRenderCommandTarget::enqueue_chunk_mesh_upload(ChunkMeshUpload upload) {
  const ChunkMeshKey key{upload.mesh.world_id, upload.mesh.coord};
  const auto found = chunk_meshes_.find(key);
  const ChunkMeshUploadReceipt receipt{
      key,
      upload.mesh.source_version,
      upload.mesh.source_content_version,
      upload.mesh.source_light_version,
      false};
  if (found != chunk_meshes_.end() &&
      !should_accept_chunk_mesh_upload(&found->second.state, upload)) {
    chunk_mesh_upload_receipts_.push_back(receipt);
    ++metrics_.mesh_upload_discards_last_frame;
    return;
  }

  WebGLChunkSectionMesh uploaded_mesh;
  uploaded_mesh.coord = upload.mesh.coord;
  uploaded_mesh.content_version = upload.mesh.source_content_version;
  uploaded_mesh.light_version = upload.mesh.source_light_version;
  uploaded_mesh.dependency_signature = upload.mesh.source_version;
  uploaded_mesh.opaque = upload_webgl_chunk_mesh(upload.mesh.opaque);
  uploaded_mesh.transparent = upload_webgl_chunk_mesh(upload.mesh.transparent);
  if (webgl_indexed_mesh_upload_failed(upload.mesh.opaque, uploaded_mesh.opaque) ||
      webgl_indexed_mesh_upload_failed(upload.mesh.transparent, uploaded_mesh.transparent)) {
    destroy_webgl_indexed_mesh(uploaded_mesh.opaque);
    destroy_webgl_indexed_mesh(uploaded_mesh.transparent);
    record_diagnostic("chunk mesh upload failed");
    chunk_mesh_upload_receipts_.push_back(receipt);
    return;
  }

  ChunkMeshRecord record;
  if (found != chunk_meshes_.end()) {
    record = std::move(found->second);
    destroy_webgl_indexed_mesh(record.mesh.opaque);
    destroy_webgl_indexed_mesh(record.mesh.transparent);
  }

  record.mesh = uploaded_mesh;
  record.state.opaque_mesh = record.mesh.opaque.index_count == 0 ? MeshID{} : MeshID{1U};
  record.state.transparent_mesh =
      record.mesh.transparent.index_count == 0 ? MeshID{} : MeshID{1U};
  record.state.uploaded_version = upload.mesh.source_version;
  record.state.uploaded_light_version = upload.mesh.source_light_version;
  record.state.resident = true;
  chunk_meshes_[key] = std::move(record);
  chunk_mesh_upload_receipts_.push_back({
      receipt.key,
      receipt.source_version,
      receipt.source_content_version,
      receipt.source_light_version,
      true});
  ++metrics_.mesh_uploads_last_frame;
}

void WebGLRenderCommandTarget::remove_chunk_mesh(ChunkMeshKey key) {
  const auto found = chunk_meshes_.find(key);
  if (found == chunk_meshes_.end()) {
    return;
  }
  destroy_webgl_indexed_mesh(found->second.mesh.opaque);
  destroy_webgl_indexed_mesh(found->second.mesh.transparent);
  chunk_meshes_.erase(found);
}

std::vector<ChunkMeshUploadReceipt>
WebGLRenderCommandTarget::drain_chunk_mesh_upload_receipts() {
  std::vector<ChunkMeshUploadReceipt> receipts;
  receipts.swap(chunk_mesh_upload_receipts_);
  return receipts;
}

std::vector<RenderCommandDiagnostic> WebGLRenderCommandTarget::drain_diagnostics() {
  std::vector<RenderCommandDiagnostic> diagnostics;
  diagnostics.swap(diagnostics_);
  return diagnostics;
}

RenderMetricsSnapshot WebGLRenderCommandTarget::metrics_snapshot() const {
  RenderMetricsSnapshot snapshot = metrics_;
  snapshot.pending_mesh_upload_count = 0U;
  snapshot.gpu_profile_context_initialized = initialized_ && !shutdown_;
  return snapshot;
}

void WebGLRenderCommandTarget::destroy_resources() {
  for (auto& [id, mesh] : gpu_entity_models_) {
    (void)id;
    destroy_webgl_indexed_mesh(mesh);
  }
  gpu_entity_models_.clear();
  entity_models_.clear();
  entity_animations_.clear();

  for (auto& [key, record] : chunk_meshes_) {
    (void)key;
    destroy_webgl_indexed_mesh(record.mesh.opaque);
    destroy_webgl_indexed_mesh(record.mesh.transparent);
  }
  chunk_meshes_.clear();
  chunk_mesh_upload_receipts_.clear();

  destroy_webgl_ui_renderer(ui_renderer_);
  destroy_webgl_sky_renderer(sky_renderer_);
  destroy_webgl_particle_renderer(particle_renderer_);
  destroy_webgl_array_mesh(fallback_color_mesh_);
  chunk_renderer_ = {};
  color_renderer_ = {};
  entity_renderer_ = {};

  delete_webgl_texture(chunk_atlas_.texture);
  delete_webgl_texture(entity_atlas_.texture);
  delete_webgl_texture(particle_atlas_.texture);
  delete_webgl_texture(ui_font_atlas_.texture);
  delete_webgl_texture(ui_item_atlas_.texture);
  delete_webgl_texture(player_skin_atlas_texture_.texture);
  chunk_atlas_ = {};
  entity_atlas_ = {};
  particle_atlas_ = {};
  ui_font_atlas_ = {};
  ui_item_atlas_ = {};
  player_skin_atlas_texture_ = {};

  player_skin_allocator_.clear();
  player_skin_by_hash_.clear();
  player_skin_by_handle_id_.clear();
  player_skins_.clear();
  initialized_ = false;
}

void WebGLRenderCommandTarget::record_diagnostic(std::string message) {
  if (!message.empty()) {
    diagnostics_.push_back({.message = std::move(message)});
  }
}

bool WebGLRenderCommandTarget::replace_texture(
    TextureArrayState& target,
    WebGLTextureArrayUploadResult upload) {
  if (!upload) {
    record_diagnostic(std::move(upload.error));
    return false;
  }
  delete_webgl_texture(target.texture);
  target.texture = upload.texture;
  target.width = upload.width;
  target.height = upload.height;
  target.layers = upload.layers;
  ++metrics_.texture_uploads_last_frame;
  metrics_.texture_upload_bytes_last_frame +=
      static_cast<u64>(upload.width) * upload.height * std::max(upload.layers, 1U) * 4U;
  return true;
}

WebGLTextureArrayView WebGLRenderCommandTarget::texture_view(
    const TextureArrayState& state) const noexcept {
  return {
      .texture = state.texture,
      .width = state.width,
      .height = state.height,
      .layers = state.layers,
  };
}

std::optional<WebGLAtlasRegion> WebGLRenderCommandTarget::resolve_player_skin_region(
    TextureRegionHandle handle) const {
  const std::optional<AtlasRegion> region = player_skin_allocator_.resolve(handle);
  if (!region.has_value()) {
    return std::nullopt;
  }
  return WebGLAtlasRegion{
      .x = region->x,
      .y = region->y,
      .width = region->width,
      .height = region->height,
      .page = region->page_index,
  };
}

std::vector<WebGLChunkSectionMesh> WebGLRenderCommandTarget::collect_chunk_meshes(
    const std::vector<ChunkDrawItem>& draws,
    WebGLChunkMeshLayer layer) const {
  std::vector<WebGLChunkSectionMesh> meshes;
  meshes.reserve(draws.size());
  for (const ChunkDrawItem& draw : draws) {
    const auto found = chunk_meshes_.find(draw.key);
    if (found == chunk_meshes_.end() ||
        !should_draw_chunk_mesh(&found->second.state, draw) ||
        !has_mesh_for_layer(found->second.mesh, layer)) {
      continue;
    }
    meshes.push_back(found->second.mesh);
  }
  return meshes;
}

} // namespace arv::render
