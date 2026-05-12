#include "arv/render/opengl_renderer.hpp"

#include "arv/core/log.hpp"
#include "arv/core/profiling.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

#if ARVANE_ENABLE_TRACY
#include <tracy/TracyOpenGL.hpp>
#endif

namespace arv::render {
namespace {

constexpr f32 UiTextureSlotWhite = 0.0F;
constexpr f32 UiTextureSlotFont = 1.0F;
constexpr f32 UiTextureSlotTerrain = 2.0F;
constexpr f32 UiTextureSlotItem = 3.0F;
constexpr f32 UiTextureSlotPlayerSkin = 4.0F;

static_assert(sizeof(Vec3f) == sizeof(f32) * 3U);

[[nodiscard]] f32 texture_slot_for_ui_source(UiTextureSource source) noexcept {
  switch (source) {
    case UiTextureSource::White:
      return UiTextureSlotWhite;
    case UiTextureSource::FontAtlas:
      return UiTextureSlotFont;
    case UiTextureSource::TerrainAtlas:
      return UiTextureSlotTerrain;
    case UiTextureSource::ItemAtlas:
      return UiTextureSlotItem;
    case UiTextureSource::PlayerSkinAtlas:
      return UiTextureSlotPlayerSkin;
  }
  return UiTextureSlotWhite;
}

[[nodiscard]] u32 full_mipmap_max_level(u32 width, u32 height) noexcept {
  u32 size = std::max(width, height);
  u32 level = 0;
  while (size > 1U) {
    size /= 2U;
    ++level;
  }
  return level;
}

[[nodiscard]] GLsizeiptr next_buffer_capacity(GLsizeiptr required) noexcept {
  GLsizeiptr capacity = 4096;
  while (capacity < required) {
    capacity *= 2;
  }
  return capacity;
}

void ensure_buffer_capacity(
    GLenum target,
    GLuint& buffer,
    GLsizeiptr& capacity,
    GLsizeiptr required,
    GLenum usage) {
  if (buffer == 0) {
    glGenBuffers(1, &buffer);
  }
  glBindBuffer(target, buffer);
  if (required > capacity) {
    capacity = next_buffer_capacity(required);
    glBufferData(target, capacity, nullptr, usage);
  }
}

void append_ui_quad(
    std::vector<UIVertex>& vertices,
    std::vector<u32>& indices,
    std::vector<UiDrawRun>& runs,
    const std::array<Vec2f, 4>& positions,
    const std::array<Vec2f, 4>& uvs,
    Vec4f color,
    f32 texture_slot,
    u32 atlas_page,
    UiClip clip) {
  const u32 base = static_cast<u32>(vertices.size());
  const u32 first_index = static_cast<u32>(indices.size());
  vertices.emplace_back(UIVertex{positions[0], uvs[0], color, texture_slot, atlas_page});
  vertices.emplace_back(UIVertex{positions[1], uvs[1], color, texture_slot, atlas_page});
  vertices.emplace_back(UIVertex{positions[2], uvs[2], color, texture_slot, atlas_page});
  vertices.emplace_back(UIVertex{positions[3], uvs[3], color, texture_slot, atlas_page});
  indices.emplace_back(base);
  indices.emplace_back(base + 1U);
  indices.emplace_back(base + 2U);
  indices.emplace_back(base + 2U);
  indices.emplace_back(base + 3U);
  indices.emplace_back(base);
  if (!runs.empty() && runs.back().clip == clip &&
      runs.back().first_index + runs.back().index_count == first_index) {
    runs.back().index_count += 6U;
  } else {
    runs.push_back({first_index, 6U, clip});
  }
}

[[nodiscard]] UiClip clip_from_draw_item(Vec2f min, Vec2f max, bool enabled) noexcept {
  return {min, max, enabled};
}

void append_ui_shape_quad(
    std::vector<UIShapeVertex>& vertices,
    std::vector<u32>& indices,
    std::vector<UiDrawRun>& runs,
    Vec2f min,
    Vec2f max,
    Vec4f color,
    Vec4f border_color,
    f32 border_width,
    f32 radius,
    UiClip clip) {
  if ((color.w <= 0.0F && border_color.w <= 0.0F) || max.x <= min.x || max.y <= min.y) {
    return;
  }
  const f32 width = max.x - min.x;
  const f32 height = max.y - min.y;
  const f32 max_inset = std::min(width, height) * 0.5F;
  radius = std::clamp(radius, 0.0F, max_inset);
  border_width = std::clamp(border_width, 0.0F, max_inset);
  if (border_width <= 0.0F || border_color.w <= 0.0F) {
    border_color = {};
    border_width = 0.0F;
  }
  if (color.w <= 0.0F && border_color.w <= 0.0F) {
    return;
  }

  const u32 base = static_cast<u32>(vertices.size());
  const u32 first_index = static_cast<u32>(indices.size());
  const Vec2f rect_size{width, height};
  vertices.emplace_back(UIShapeVertex{
      {min.x, min.y},
      {0.0F, 0.0F},
      color,
      rect_size,
      border_color,
      radius,
      border_width});
  vertices.emplace_back(UIShapeVertex{
      {max.x, min.y},
      {1.0F, 0.0F},
      color,
      rect_size,
      border_color,
      radius,
      border_width});
  vertices.emplace_back(UIShapeVertex{
      {max.x, max.y},
      {1.0F, 1.0F},
      color,
      rect_size,
      border_color,
      radius,
      border_width});
  vertices.emplace_back(UIShapeVertex{
      {min.x, max.y},
      {0.0F, 1.0F},
      color,
      rect_size,
      border_color,
      radius,
      border_width});
  indices.emplace_back(base);
  indices.emplace_back(base + 1U);
  indices.emplace_back(base + 2U);
  indices.emplace_back(base + 2U);
  indices.emplace_back(base + 3U);
  indices.emplace_back(base);
  if (!runs.empty() && runs.back().clip == clip &&
      runs.back().first_index + runs.back().index_count == first_index) {
    runs.back().index_count += 6U;
  } else {
    runs.push_back({first_index, 6U, clip});
  }
}

void append_ui_rect_item(
    std::vector<UIShapeVertex>& vertices,
    std::vector<u32>& indices,
    std::vector<UiDrawRun>& runs,
    const UIRectDrawItem& rect) {
  const UiClip clip = clip_from_draw_item(rect.clip_min, rect.clip_max, rect.clip);
  const Vec2f min = rect.min;
  const Vec2f max = rect.max;
  append_ui_shape_quad(
      vertices,
      indices,
      runs,
      min,
      max,
      rect.color,
      rect.border_color,
      rect.border_width,
      rect.radius,
      clip);
}

void apply_ui_scissor(UiClip clip, u32 framebuffer_height) {
  if (!clip.enabled) {
    glDisable(GL_SCISSOR_TEST);
    return;
  }
  const f32 x_min = std::max(0.0F, clip.min.x);
  const f32 y_min = std::max(0.0F, clip.min.y);
  const f32 x_max = std::max(x_min, clip.max.x);
  const f32 y_max = std::max(y_min, clip.max.y);
  glEnable(GL_SCISSOR_TEST);
  glScissor(
      static_cast<GLint>(std::floor(x_min)),
      static_cast<GLint>(std::floor(static_cast<f32>(framebuffer_height) - y_max)),
      static_cast<GLsizei>(std::ceil(x_max - x_min)),
      static_cast<GLsizei>(std::ceil(y_max - y_min)));
}

[[nodiscard]] i32 compare_f32(f32 lhs, f32 rhs) noexcept {
  if (lhs < rhs) {
    return -1;
  }
  if (lhs > rhs) {
    return 1;
  }
  return 0;
}

[[nodiscard]] i32 compare_vec3(Vec3f lhs, Vec3f rhs) noexcept {
  if (const i32 x = compare_f32(lhs.x, rhs.x); x != 0) {
    return x;
  }
  if (const i32 y = compare_f32(lhs.y, rhs.y); y != 0) {
    return y;
  }
  return compare_f32(lhs.z, rhs.z);
}

[[nodiscard]] i32 compare_bone_rotation_overrides(
    const std::vector<EntityBoneRotationOverride>& lhs,
    const std::vector<EntityBoneRotationOverride>& rhs) noexcept {
  const usize count = std::min(lhs.size(), rhs.size());
  for (usize index = 0; index < count; ++index) {
    if (lhs[index].bone_name < rhs[index].bone_name) {
      return -1;
    }
    if (rhs[index].bone_name < lhs[index].bone_name) {
      return 1;
    }
    if (const i32 rotation = compare_vec3(lhs[index].rotation_degrees, rhs[index].rotation_degrees);
        rotation != 0) {
      return rotation;
    }
  }
  if (lhs.size() < rhs.size()) {
    return -1;
  }
  if (lhs.size() > rhs.size()) {
    return 1;
  }
  return 0;
}

[[nodiscard]] i32 compare_entity_animation_playbacks(
    const std::vector<EntityAnimationPlayback>& lhs,
    const std::vector<EntityAnimationPlayback>& rhs) noexcept {
  const usize count = std::min(lhs.size(), rhs.size());
  for (usize index = 0; index < count; ++index) {
    if (lhs[index].name < rhs[index].name) {
      return -1;
    }
    if (rhs[index].name < lhs[index].name) {
      return 1;
    }
    if (const i32 time = compare_f32(lhs[index].time_seconds, rhs[index].time_seconds);
        time != 0) {
      return time;
    }
  }
  if (lhs.size() < rhs.size()) {
    return -1;
  }
  if (lhs.size() > rhs.size()) {
    return 1;
  }
  return 0;
}

void apply_bone_rotation_overrides(
    const EntityModelAsset& model,
    std::span<Vec3f> bone_rotations,
    const std::vector<EntityBoneRotationOverride>& overrides) noexcept {
  for (const EntityBoneRotationOverride& bone_override : overrides) {
    if (bone_override.bone_name.empty()) {
      continue;
    }
    for (usize bone_index = 0; bone_index < model.bones.size() &&
                               bone_index < bone_rotations.size();
         ++bone_index) {
      if (model.bones[bone_index].name != bone_override.bone_name) {
        continue;
      }
      bone_rotations[bone_index] =
          bone_rotations[bone_index] + bone_override.rotation_degrees;
      break;
    }
  }
}

} // namespace

void OpenGLRenderer::set_chunk_shader_sources(std::string vertex_source, std::string fragment_source) {
  chunk_vertex_shader_source_ = std::move(vertex_source);
  chunk_fragment_shader_source_ = std::move(fragment_source);
  if (chunk_program_ != 0) {
    glDeleteProgram(chunk_program_);
    chunk_program_ = 0;
  }
}

void OpenGLRenderer::set_entity_shader_sources(std::string vertex_source, std::string fragment_source) {
  entity_vertex_shader_source_ = std::move(vertex_source);
  entity_fragment_shader_source_ = std::move(fragment_source);
  if (entity_program_ != 0) {
    glDeleteProgram(entity_program_);
    entity_program_ = 0;
  }
}

void OpenGLRenderer::set_ui_shader_sources(std::string vertex_source, std::string fragment_source) {
  ui_vertex_shader_source_ = std::move(vertex_source);
  ui_fragment_shader_source_ = std::move(fragment_source);
  if (ui_program_ != 0) {
    glDeleteProgram(ui_program_);
    ui_program_ = 0;
  }
}

void OpenGLRenderer::set_sky_shader_sources(std::string vertex_source, std::string fragment_source) {
  sky_vertex_shader_source_ = std::move(vertex_source);
  sky_fragment_shader_source_ = std::move(fragment_source);
  if (sky_program_ != 0) {
    glDeleteProgram(sky_program_);
    sky_program_ = 0;
  }
}

void OpenGLRenderer::set_chunk_texture_atlas(TextureArrayUploadData upload) {
  destroy_gpu_texture(terrain_atlas_);
  if (upload.valid()) {
    note_texture_upload(upload);
    terrain_atlas_ = create_texture_array(upload, true);
  }
}

void OpenGLRenderer::set_entity_texture_atlas(TextureArrayUploadData upload) {
  destroy_gpu_texture(entity_atlas_);
  if (upload.valid()) {
    note_texture_upload(upload);
    entity_atlas_ = create_texture_array(upload, true);
  }
}

void OpenGLRenderer::set_ui_font_atlas(TextureArrayUploadData upload) {
  destroy_gpu_texture(font_atlas_);
  if (upload.valid()) {
    note_texture_upload(upload);
    font_atlas_ = create_texture_array(upload, false, TextureFilter::Linear);
  }
}

void OpenGLRenderer::set_ui_item_atlas(TextureArrayUploadData upload) {
  destroy_gpu_texture(item_atlas_);
  if (upload.valid()) {
    note_texture_upload(upload);
    item_atlas_ = create_texture_array(upload, false);
  }
}

void OpenGLRenderer::configure_player_skin_atlas(PlayerSkinAtlasConfig config) {
  player_skin_atlas_.destroy_on_render_thread();
  player_skin_atlas_.configure(config);
}

std::optional<TextureRegionHandle> OpenGLRenderer::upload_player_skin_texture(
    std::string skin_hash,
    TextureUploadData upload) {
  if (!upload.valid() ||
      upload.width > std::numeric_limits<u16>::max() ||
      upload.height > std::numeric_limits<u16>::max()) {
    return std::nullopt;
  }
  note_texture_upload(upload);
  const std::span<const std::byte> bytes{
      reinterpret_cast<const std::byte*>(upload.rgba_pixels.data()),
      upload.rgba_pixels.size()};
  return player_skin_atlas_.upload_skin_rgba8_on_render_thread(
      std::move(skin_hash),
      bytes,
      static_cast<u16>(upload.width),
      static_cast<u16>(upload.height),
      render_frame_index_,
      std::chrono::steady_clock::now());
}

std::optional<AtlasRegion> OpenGLRenderer::resolve_player_skin_texture(
    TextureRegionHandle handle) const {
  return player_skin_atlas_.resolve(handle);
}

const ResidentSkinTexture* OpenGLRenderer::resident_player_skin_texture(
    std::string_view skin_hash) const noexcept {
  return player_skin_atlas_.resident(skin_hash);
}

bool OpenGLRenderer::mark_player_skin_texture_used(
    TextureRegionHandle handle,
    bool pinned) {
  return player_skin_atlas_.mark_used(
      handle,
      render_frame_index_,
      std::chrono::steady_clock::now(),
      pinned);
}

u16 OpenGLRenderer::player_skin_atlas_page_width() const noexcept {
  return player_skin_atlas_.page_width();
}

u16 OpenGLRenderer::player_skin_atlas_page_height() const noexcept {
  return player_skin_atlas_.page_height();
}

void OpenGLRenderer::set_entity_model(EntityModelAsset model) {
  const EntityModelID id = model.id;
  if (const auto found = entity_gpu_models_.find(id); found != entity_gpu_models_.end()) {
    destroy_entity_gpu_model(found->second);
    entity_gpu_models_.erase(found);
  }
  entity_gpu_models_[id] = create_entity_gpu_model(model);
  entity_models_[id] = std::move(model);
}

void OpenGLRenderer::set_entity_animation(EntityAnimationAsset animation) {
  const EntityAnimationID id = animation.id;
  entity_animations_[id] = std::move(animation);
}

void OpenGLRenderer::clear_entity_models() {
  for (auto& [id, model] : entity_gpu_models_) {
    (void)id;
    destroy_entity_gpu_model(model);
  }
  entity_gpu_models_.clear();
  entity_models_.clear();
}

void OpenGLRenderer::clear_entity_animations() {
  entity_animations_.clear();
}

void OpenGLRenderer::resize(u32 width, u32 height) {
  if (viewport_set_ && width_ == width && height_ == height) {
    return;
  }
  width_ = width;
  height_ = height;
  viewport_set_ = true;
  glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
}

void OpenGLRenderer::submit_frame(FramePacket packet) {
  ARV_PROFILE_ZONE("Render submit");
  frame_ = std::move(packet);
}

void OpenGLRenderer::set_chunk_mesh_upload_budget(u32 max_uploads_per_frame) noexcept {
  max_chunk_mesh_uploads_per_frame_ = std::max<u32>(max_uploads_per_frame, 1U);
}

void OpenGLRenderer::enqueue_chunk_mesh_upload(ChunkMeshUpload upload) {
  const ChunkMeshKey key{upload.mesh.world_id, upload.mesh.coord};
  const auto found = chunks_.find(key);
  if (found != chunks_.end() && !should_accept_chunk_mesh_upload(&found->second.state, upload)) {
    return;
  }

  for (auto pending = pending_chunk_mesh_uploads_.begin();
       pending != pending_chunk_mesh_uploads_.end();) {
    const ChunkMeshKey pending_key{pending->mesh.world_id, pending->mesh.coord};
    if (!(pending_key == key)) {
      ++pending;
      continue;
    }

    if (chunk_mesh_upload_at_least_as_new(
            pending->mesh.source_version,
            pending->mesh.source_light_version,
            upload.mesh.source_version,
            upload.mesh.source_light_version)) {
      return;
    }
    pending = pending_chunk_mesh_uploads_.erase(pending);
  }

  pending_chunk_mesh_uploads_.emplace_back(std::move(upload));
}

void OpenGLRenderer::remove_chunk_mesh(ChunkMeshKey key) {
  for (auto pending = pending_chunk_mesh_uploads_.begin();
       pending != pending_chunk_mesh_uploads_.end();) {
    const ChunkMeshKey pending_key{pending->mesh.world_id, pending->mesh.coord};
    if (pending_key == key) {
      pending = pending_chunk_mesh_uploads_.erase(pending);
    } else {
      ++pending;
    }
  }

  const auto found = chunks_.find(key);
  if (found == chunks_.end()) {
    return;
  }
  destroy_chunk_gpu_state(found->second);
  chunks_.erase(found);
}

void OpenGLRenderer::render() {
  ARV_PROFILE_ZONE("Render submit");
  ++render_frame_index_;
  draw_calls_last_frame_ = 0;
  chunk_draw_calls_last_frame_ = 0;
  ui_draw_calls_last_frame_ = 0;
  vertices_submitted_last_frame_ = 0;
  triangles_submitted_last_frame_ = 0;
  gpu_buffer_uploads_last_frame_ = 0;
  gpu_buffer_upload_bytes_last_frame_ = 0;
  texture_uploads_last_frame_ = pending_texture_uploads_;
  texture_upload_bytes_last_frame_ = pending_texture_upload_bytes_;
  pending_texture_uploads_ = 0;
  pending_texture_upload_bytes_ = 0;
#if ARVANE_ENABLE_TRACY
  if (!gpu_profile_context_initialized_) {
    TracyGpuContext;
    gpu_profile_context_initialized_ = true;
  }
  TracyGpuZone("GPU queue timing");
#endif
  process_chunk_mesh_uploads();
  ensure_white_texture();

  glEnable(GL_DEPTH_TEST);
  glClearColor(0.08F, 0.10F, 0.12F, 1.0F);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  if (!ensure_chunk_program()) {
    return;
  }

  glUseProgram(chunk_program_);
  glUniform3f(
      chunk_camera_position_uniform_,
      frame_.world_view.position.x,
      frame_.world_view.position.y,
      frame_.world_view.position.z);
  glUniform3f(
      chunk_camera_forward_uniform_,
      frame_.world_view.forward.x,
      frame_.world_view.forward.y,
      frame_.world_view.forward.z);
  glUniform3f(
      chunk_camera_right_uniform_,
      frame_.world_view.right.x,
      frame_.world_view.right.y,
      frame_.world_view.right.z);
  glUniform3f(
      chunk_camera_up_uniform_,
      frame_.world_view.up.x,
      frame_.world_view.up.y,
      frame_.world_view.up.z);
  const f32 aspect = height_ == 0 ? 1.0F : static_cast<f32>(width_) / static_cast<f32>(height_);
  glUniform1f(chunk_camera_aspect_uniform_, aspect);
  glUniform1f(chunk_camera_fov_y_uniform_, frame_.world_view.fov_y_degrees);
  glUniform1f(chunk_camera_near_uniform_, frame_.world_view.near_plane);
  glUniform1f(chunk_camera_far_uniform_, frame_.world_view.far_plane);
  const auto animation_now = std::chrono::steady_clock::now().time_since_epoch();
  glUniform1f(
      chunk_animation_time_uniform_,
      std::chrono::duration<f32>(animation_now).count());
  glUniform2f(
      chunk_atlas_size_uniform_,
      static_cast<f32>(terrain_atlas_.width == 0 ? 1U : terrain_atlas_.width),
      static_cast<f32>(terrain_atlas_.height == 0 ? 1U : terrain_atlas_.height));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(
      GL_TEXTURE_2D_ARRAY,
      terrain_atlas_.id == 0 ? white_texture_.id : terrain_atlas_.id);
  glUniform1i(chunk_terrain_atlas_uniform_, 0);
  glUniform3f(
      chunk_daylight_color_uniform_,
      frame_.sky.daylight_color.x,
      frame_.sky.daylight_color.y,
      frame_.sky.daylight_color.z);
  glUniform1f(chunk_daylight_strength_uniform_, frame_.sky.daylight_strength);
  glUniform3f(
      chunk_ambient_color_uniform_,
      frame_.sky.ambient_color.x,
      frame_.sky.ambient_color.y,
      frame_.sky.ambient_color.z);

  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  draw_chunk_list(frame_.opaque_chunks, false);
  draw_entities();
  draw_sky();
  glUseProgram(chunk_program_);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(
      GL_TEXTURE_2D_ARRAY,
      terrain_atlas_.id == 0 ? white_texture_.id : terrain_atlas_.id);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  draw_chunk_list(frame_.transparent_chunks, true);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glBindVertexArray(0);
  glUseProgram(0);

  draw_ui();
  draw_entity_previews();
  player_skin_atlas_.run_periodic_cleanup(render_frame_index_, std::chrono::steady_clock::now());
#if ARVANE_ENABLE_TRACY
  TracyGpuCollect;
#endif
}

void OpenGLRenderer::begin_shutdown() {
  pending_chunk_mesh_uploads_.clear();
  for (auto& [key, chunk] : chunks_) {
    (void)key;
    destroy_chunk_gpu_state(chunk);
  }
  chunks_.clear();
  clear_entity_models();
  if (chunk_program_ != 0) {
    glDeleteProgram(chunk_program_);
    chunk_program_ = 0;
  }
  if (ui_program_ != 0) {
    glDeleteProgram(ui_program_);
    ui_program_ = 0;
  }
  if (entity_program_ != 0) {
    glDeleteProgram(entity_program_);
    entity_program_ = 0;
  }
  if (sky_program_ != 0) {
    glDeleteProgram(sky_program_);
    sky_program_ = 0;
  }
  if (sky_vao_ != 0) {
    glDeleteVertexArrays(1, &sky_vao_);
    sky_vao_ = 0;
  }
  destroy_gpu_texture(terrain_atlas_);
  destroy_gpu_texture(entity_atlas_);
  destroy_gpu_texture(font_atlas_);
  destroy_gpu_texture(item_atlas_);
  destroy_gpu_texture(white_texture_);
  player_skin_atlas_.destroy_on_render_thread();
  if (ui_index_buffer_ != 0) {
    glDeleteBuffers(1, &ui_index_buffer_);
  }
  if (ui_vertex_buffer_ != 0) {
    glDeleteBuffers(1, &ui_vertex_buffer_);
  }
  if (ui_shape_index_buffer_ != 0) {
    glDeleteBuffers(1, &ui_shape_index_buffer_);
  }
  if (ui_shape_vertex_buffer_ != 0) {
    glDeleteBuffers(1, &ui_shape_vertex_buffer_);
  }
  if (ui_vao_ != 0) {
    glDeleteVertexArrays(1, &ui_vao_);
  }
  if (ui_shape_vao_ != 0) {
    glDeleteVertexArrays(1, &ui_shape_vao_);
  }
  ui_index_buffer_ = 0;
  ui_vertex_buffer_ = 0;
  ui_vao_ = 0;
  ui_vertex_capacity_bytes_ = 0;
  ui_index_capacity_bytes_ = 0;
  ui_shape_index_buffer_ = 0;
  ui_shape_vertex_buffer_ = 0;
  ui_shape_vao_ = 0;
  ui_shape_vertex_capacity_bytes_ = 0;
  ui_shape_index_capacity_bytes_ = 0;
  gpu_profile_context_initialized_ = false;
  render_frame_index_ = 0;
  frame_ = {};
}

void OpenGLRenderer::process_chunk_mesh_uploads() {
  ARV_PROFILE_ZONE("Render mesh uploads");
  const auto start = std::chrono::steady_clock::now();
  mesh_uploads_last_frame_ = 0;
  mesh_upload_discards_last_frame_ = 0;
  u32 upload_attempts = 0;

  while (upload_attempts < max_chunk_mesh_uploads_per_frame_ &&
         !pending_chunk_mesh_uploads_.empty()) {
    ChunkMeshUpload upload = std::move(pending_chunk_mesh_uploads_.front());
    pending_chunk_mesh_uploads_.pop_front();
    ++upload_attempts;
    const ChunkMeshUploadReceipt receipt{
        {upload.mesh.world_id, upload.mesh.coord},
        upload.mesh.source_version,
        upload.mesh.source_content_version,
        upload.mesh.source_light_version,
        false};
    if (upload_chunk_mesh_now(std::move(upload))) {
      ++mesh_uploads_last_frame_;
      chunk_mesh_upload_receipts_.emplace_back(
          receipt.key,
          receipt.source_version,
          receipt.source_content_version,
          receipt.source_light_version,
          true);
    } else {
      ++mesh_upload_discards_last_frame_;
      chunk_mesh_upload_receipts_.emplace_back(receipt);
    }
  }

  const auto end = std::chrono::steady_clock::now();
  mesh_upload_time_ms_last_frame_ =
      std::chrono::duration<double, std::milli>(end - start).count();
}

std::vector<ChunkMeshUploadReceipt> OpenGLRenderer::drain_chunk_mesh_upload_receipts() {
  std::vector<ChunkMeshUploadReceipt> receipts;
  receipts.swap(chunk_mesh_upload_receipts_);
  return receipts;
}

bool OpenGLRenderer::upload_chunk_mesh_now(ChunkMeshUpload upload) {
  const ChunkMeshKey key{upload.mesh.world_id, upload.mesh.coord};
  const auto found = chunks_.find(key);
  if (found != chunks_.end() && !should_accept_chunk_mesh_upload(&found->second.state, upload)) {
    return false;
  }

  ChunkGpuState gpu_state;
  if (found != chunks_.end()) {
    gpu_state = std::move(found->second);
  }

  upload_cpu_mesh_to_gpu(gpu_state.opaque, upload.mesh.opaque);
  upload_cpu_mesh_to_gpu(gpu_state.transparent, upload.mesh.transparent);
  gpu_state.state.opaque_mesh = gpu_state.opaque.index_count == 0 ? MeshID{} : allocate_mesh_id();
  gpu_state.state.transparent_mesh = gpu_state.transparent.index_count == 0 ? MeshID{} : allocate_mesh_id();
  gpu_state.state.uploaded_version = upload.mesh.source_version;
  gpu_state.state.uploaded_light_version = upload.mesh.source_light_version;
  gpu_state.state.resident = true;
  chunks_[key] = gpu_state;
  return true;
}

MeshID OpenGLRenderer::allocate_mesh_id() noexcept {
  return MeshID{next_mesh_id_++};
}

OpenGLRenderer::GpuMesh OpenGLRenderer::create_gpu_mesh(const CpuMeshBuffer& mesh) {
  GpuMesh gpu;
  upload_cpu_mesh_to_gpu(gpu, mesh);
  return gpu;
}

void OpenGLRenderer::upload_cpu_mesh_to_gpu(GpuMesh& gpu, const CpuMeshBuffer& mesh) {
  ARV_PROFILE_ZONE("Chunk mesh upload");
  if ((mesh.vertex_bytes.empty() || mesh.indices.empty()) && gpu.vao == 0) {
    gpu.index_count = 0;
    return;
  }

  if (gpu.vao == 0) {
    glGenVertexArrays(1, &gpu.vao);
  }
  glBindVertexArray(gpu.vao);

  const GLsizeiptr vertex_bytes =
      static_cast<GLsizeiptr>(mesh.vertex_bytes.size());
  ensure_buffer_capacity(
      GL_ARRAY_BUFFER,
      gpu.vertex_buffer,
      gpu.vertex_capacity_bytes,
      vertex_bytes,
      GL_DYNAMIC_DRAW);
  if (vertex_bytes > 0) {
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, mesh.vertex_bytes.data());
    ++gpu_buffer_uploads_last_frame_;
    gpu_buffer_upload_bytes_last_frame_ += static_cast<u64>(vertex_bytes);
  }

  const GLsizeiptr index_bytes =
      static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(u32));
  ensure_buffer_capacity(
      GL_ELEMENT_ARRAY_BUFFER,
      gpu.index_buffer,
      gpu.index_capacity_bytes,
      index_bytes,
      GL_DYNAMIC_DRAW);
  if (index_bytes > 0) {
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes, mesh.indices.data());
    ++gpu_buffer_uploads_last_frame_;
    gpu_buffer_upload_bytes_last_frame_ += static_cast<u64>(index_bytes);
  }

  glEnableVertexAttribArray(0);
  glVertexAttribIPointer(
      0,
      1,
      GL_UNSIGNED_INT,
      sizeof(ChunkVertex),
      reinterpret_cast<const void*>(offsetof(ChunkVertex, pos_ao)));
  glEnableVertexAttribArray(1);
  glVertexAttribIPointer(
      1,
      1,
      GL_UNSIGNED_INT,
      sizeof(ChunkVertex),
      reinterpret_cast<const void*>(offsetof(ChunkVertex, uv)));
  glEnableVertexAttribArray(2);
  glVertexAttribIPointer(
      2,
      1,
      GL_UNSIGNED_INT,
      sizeof(ChunkVertex),
      reinterpret_cast<const void*>(offsetof(ChunkVertex, light_face_material_hints)));
  glEnableVertexAttribArray(3);
  glVertexAttribIPointer(
      3,
      1,
      GL_UNSIGNED_INT,
      sizeof(ChunkVertex),
      reinterpret_cast<const void*>(offsetof(ChunkVertex, atlas_page)));

  gpu.vertex_count = static_cast<GLsizei>(mesh.vertex_bytes.size() / sizeof(ChunkVertex));
  gpu.index_count = static_cast<GLsizei>(mesh.indices.size());
  glBindVertexArray(0);
}

void OpenGLRenderer::destroy_gpu_mesh(GpuMesh& mesh) {
  if (mesh.index_buffer != 0) {
    glDeleteBuffers(1, &mesh.index_buffer);
  }
  if (mesh.vertex_buffer != 0) {
    glDeleteBuffers(1, &mesh.vertex_buffer);
  }
  if (mesh.vao != 0) {
    glDeleteVertexArrays(1, &mesh.vao);
  }
  mesh = {};
}

void OpenGLRenderer::destroy_chunk_gpu_state(ChunkGpuState& state) {
  destroy_gpu_mesh(state.opaque);
  destroy_gpu_mesh(state.transparent);
  state.state = {};
}

void OpenGLRenderer::destroy_gpu_texture(GpuTexture& texture) {
  if (texture.id != 0) {
    glDeleteTextures(1, &texture.id);
  }
  texture = {};
}

OpenGLRenderer::EntityGpuModel OpenGLRenderer::create_entity_gpu_model(
    const EntityModelAsset& model) {
  EntityGpuModel gpu;
  glGenVertexArrays(1, &gpu.vao);
  glBindVertexArray(gpu.vao);

  glGenBuffers(1, &gpu.vertex_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, gpu.vertex_buffer);
  const GLsizeiptr vertex_bytes =
      static_cast<GLsizeiptr>(model.vertices.size() * sizeof(EntityModelMeshVertex));
  glBufferData(
      GL_ARRAY_BUFFER,
      vertex_bytes,
      model.vertices.empty() ? nullptr : model.vertices.data(),
      GL_STATIC_DRAW);

  glGenBuffers(1, &gpu.index_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.index_buffer);
  const GLsizeiptr index_bytes =
      static_cast<GLsizeiptr>(model.indices.size() * sizeof(u32));
  glBufferData(
      GL_ELEMENT_ARRAY_BUFFER,
      index_bytes,
      model.indices.empty() ? nullptr : model.indices.data(),
      GL_STATIC_DRAW);
  gpu.index_count = static_cast<GLsizei>(model.indices.size());

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(
      0,
      3,
      GL_FLOAT,
      GL_FALSE,
      sizeof(EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(EntityModelMeshVertex, position)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(
      1,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(EntityModelMeshVertex, uv)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(
      2,
      3,
      GL_FLOAT,
      GL_FALSE,
      sizeof(EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(EntityModelMeshVertex, color)));
  glEnableVertexAttribArray(3);
  glVertexAttribIPointer(
      3,
      1,
      GL_UNSIGNED_INT,
      sizeof(EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(EntityModelMeshVertex, bone_index)));
  glEnableVertexAttribArray(4);
  glVertexAttribIPointer(
      4,
      1,
      GL_UNSIGNED_INT,
      sizeof(EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(EntityModelMeshVertex, flags)));
  glEnableVertexAttribArray(5);
  glVertexAttribIPointer(
      5,
      1,
      GL_UNSIGNED_INT,
      sizeof(EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(EntityModelMeshVertex, texture_domain)));
  glEnableVertexAttribArray(6);
  glVertexAttribIPointer(
      6,
      1,
      GL_UNSIGNED_INT,
      sizeof(EntityModelMeshVertex),
      reinterpret_cast<const void*>(offsetof(EntityModelMeshVertex, atlas_page)));

  glGenBuffers(1, &gpu.instance_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, gpu.instance_buffer);
  glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

  glEnableVertexAttribArray(7);
  glVertexAttribPointer(
      7,
      3,
      GL_FLOAT,
      GL_FALSE,
      sizeof(EntityInstanceData),
      reinterpret_cast<const void*>(offsetof(EntityInstanceData, position)));
  glVertexAttribDivisor(7, 1);
  glEnableVertexAttribArray(8);
  glVertexAttribPointer(
      8,
      4,
      GL_FLOAT,
      GL_FALSE,
      sizeof(EntityInstanceData),
      reinterpret_cast<const void*>(offsetof(EntityInstanceData, rotation)));
  glVertexAttribDivisor(8, 1);
  glEnableVertexAttribArray(9);
  glVertexAttribPointer(
      9,
      1,
      GL_FLOAT,
      GL_FALSE,
      sizeof(EntityInstanceData),
      reinterpret_cast<const void*>(offsetof(EntityInstanceData, scale)));
  glVertexAttribDivisor(9, 1);
  glEnableVertexAttribArray(10);
  glVertexAttribPointer(
      10,
      4,
      GL_FLOAT,
      GL_FALSE,
      sizeof(EntityInstanceData),
      reinterpret_cast<const void*>(offsetof(EntityInstanceData, player_skin_region)));
  glVertexAttribDivisor(10, 1);
  glEnableVertexAttribArray(11);
  glVertexAttribIPointer(
      11,
      1,
      GL_UNSIGNED_INT,
      sizeof(EntityInstanceData),
      reinterpret_cast<const void*>(offsetof(EntityInstanceData, player_skin_page)));
  glVertexAttribDivisor(11, 1);

  glBindVertexArray(0);
  return gpu;
}

void OpenGLRenderer::destroy_entity_gpu_model(EntityGpuModel& model) {
  if (model.instance_buffer != 0) {
    glDeleteBuffers(1, &model.instance_buffer);
  }
  if (model.index_buffer != 0) {
    glDeleteBuffers(1, &model.index_buffer);
  }
  if (model.vertex_buffer != 0) {
    glDeleteBuffers(1, &model.vertex_buffer);
  }
  if (model.vao != 0) {
    glDeleteVertexArrays(1, &model.vao);
  }
  model = {};
}

OpenGLRenderer::GpuTexture OpenGLRenderer::create_texture_array(
    const TextureArrayUploadData& upload,
    bool mipmaps,
    TextureFilter filter) {
  GpuTexture texture;
  if (!upload.valid()) {
    return texture;
  }
  GLint max_texture_size = 0;
  GLint max_array_layers = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_array_layers);
  if ((max_texture_size > 0 &&
       (upload.width > static_cast<u32>(max_texture_size) ||
        upload.height > static_cast<u32>(max_texture_size))) ||
      (max_array_layers > 0 && upload.layers > static_cast<u32>(max_array_layers))) {
    core::warn(
        "render",
        "texture array upload {}x{}x{} exceeds GL limits {}x{} layers",
        upload.width,
        upload.height,
        upload.layers,
        max_texture_size,
        max_array_layers);
    return texture;
  }
  glGenTextures(1, &texture.id);
  glBindTexture(GL_TEXTURE_2D_ARRAY, texture.id);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  const GLenum mag_filter = filter == TextureFilter::Linear ? GL_LINEAR : GL_NEAREST;
  const GLenum min_filter = mipmaps
                                ? (filter == TextureFilter::Linear ? GL_LINEAR_MIPMAP_LINEAR
                                                                   : GL_NEAREST_MIPMAP_NEAREST)
                                : mag_filter;
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, mag_filter);
  const u32 full_max_level = mipmaps ? full_mipmap_max_level(upload.width, upload.height) : 0U;
  const u32 max_level = mipmaps && upload.limit_mipmap_levels
                            ? std::min(upload.max_mipmap_level, full_max_level)
                            : full_max_level;
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(max_level));
  glTexParameteri(
      GL_TEXTURE_2D_ARRAY,
      GL_TEXTURE_MIN_FILTER,
      min_filter);
  glTexImage3D(
      GL_TEXTURE_2D_ARRAY,
      0,
      GL_RGBA8,
      static_cast<GLsizei>(upload.width),
      static_cast<GLsizei>(upload.height),
      static_cast<GLsizei>(upload.layers),
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      upload.rgba_pixels.data());
  if (mipmaps) {
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
  }
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  texture.width = upload.width;
  texture.height = upload.height;
  texture.layers = upload.layers;
  return texture;
}

void OpenGLRenderer::note_texture_upload(const TextureUploadData& upload) noexcept {
  ++pending_texture_uploads_;
  pending_texture_upload_bytes_ += static_cast<u64>(upload.rgba_pixels.size());
}

void OpenGLRenderer::note_texture_upload(const TextureArrayUploadData& upload) noexcept {
  ++pending_texture_uploads_;
  pending_texture_upload_bytes_ += static_cast<u64>(upload.rgba_pixels.size());
}

GLuint OpenGLRenderer::compile_shader(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint success = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (success != GL_TRUE) {
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

bool OpenGLRenderer::ensure_chunk_program() {
  if (chunk_program_ != 0) {
    return true;
  }

  if (chunk_vertex_shader_source_.empty() || chunk_fragment_shader_source_.empty()) {
    return false;
  }

  const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, chunk_vertex_shader_source_.c_str());
  const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, chunk_fragment_shader_source_.c_str());
  if (vertex_shader == 0 || fragment_shader == 0) {
    if (vertex_shader != 0) {
      glDeleteShader(vertex_shader);
    }
    if (fragment_shader != 0) {
      glDeleteShader(fragment_shader);
    }
    return false;
  }

  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glLinkProgram(program);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  GLint success = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (success != GL_TRUE) {
    glDeleteProgram(program);
    return false;
  }

  chunk_program_ = program;
  chunk_origin_uniform_ = glGetUniformLocation(chunk_program_, "u_chunk_origin");
  chunk_camera_position_uniform_ = glGetUniformLocation(chunk_program_, "u_camera_position");
  chunk_camera_forward_uniform_ = glGetUniformLocation(chunk_program_, "u_camera_forward");
  chunk_camera_right_uniform_ = glGetUniformLocation(chunk_program_, "u_camera_right");
  chunk_camera_up_uniform_ = glGetUniformLocation(chunk_program_, "u_camera_up");
  chunk_camera_aspect_uniform_ = glGetUniformLocation(chunk_program_, "u_camera_aspect");
  chunk_camera_fov_y_uniform_ = glGetUniformLocation(chunk_program_, "u_camera_fov_y_degrees");
  chunk_camera_near_uniform_ = glGetUniformLocation(chunk_program_, "u_camera_near_plane");
  chunk_camera_far_uniform_ = glGetUniformLocation(chunk_program_, "u_camera_far_plane");
  chunk_animation_time_uniform_ = glGetUniformLocation(chunk_program_, "u_animation_time_seconds");
  chunk_alpha_uniform_ = glGetUniformLocation(chunk_program_, "u_alpha");
  chunk_atlas_size_uniform_ = glGetUniformLocation(chunk_program_, "u_atlas_size");
  chunk_terrain_atlas_uniform_ = glGetUniformLocation(chunk_program_, "u_terrain_atlas");
  chunk_daylight_color_uniform_ = glGetUniformLocation(chunk_program_, "u_daylight_color");
  chunk_daylight_strength_uniform_ = glGetUniformLocation(chunk_program_, "u_daylight_strength");
  chunk_ambient_color_uniform_ = glGetUniformLocation(chunk_program_, "u_ambient_color");
  return true;
}

bool OpenGLRenderer::ensure_entity_program() {
  if (entity_program_ != 0) {
    return true;
  }

  if (entity_vertex_shader_source_.empty() || entity_fragment_shader_source_.empty()) {
    return false;
  }

  const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, entity_vertex_shader_source_.c_str());
  const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, entity_fragment_shader_source_.c_str());
  if (vertex_shader == 0 || fragment_shader == 0) {
    if (vertex_shader != 0) {
      glDeleteShader(vertex_shader);
    }
    if (fragment_shader != 0) {
      glDeleteShader(fragment_shader);
    }
    return false;
  }

  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glLinkProgram(program);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  GLint success = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (success != GL_TRUE) {
    glDeleteProgram(program);
    return false;
  }

  entity_program_ = program;
  entity_camera_position_uniform_ = glGetUniformLocation(entity_program_, "u_camera_position");
  entity_camera_forward_uniform_ = glGetUniformLocation(entity_program_, "u_camera_forward");
  entity_camera_right_uniform_ = glGetUniformLocation(entity_program_, "u_camera_right");
  entity_camera_up_uniform_ = glGetUniformLocation(entity_program_, "u_camera_up");
  entity_camera_aspect_uniform_ = glGetUniformLocation(entity_program_, "u_camera_aspect");
  entity_camera_fov_y_uniform_ = glGetUniformLocation(entity_program_, "u_camera_fov_y_degrees");
  entity_camera_near_uniform_ = glGetUniformLocation(entity_program_, "u_camera_near_plane");
  entity_camera_far_uniform_ = glGetUniformLocation(entity_program_, "u_camera_far_plane");
  entity_atlas_size_uniform_ = glGetUniformLocation(entity_program_, "u_atlas_size");
  entity_atlas_uniform_ = glGetUniformLocation(entity_program_, "u_entity_atlas");
  entity_player_skin_atlas_size_uniform_ =
      glGetUniformLocation(entity_program_, "u_player_skin_atlas_size");
  entity_player_skin_atlas_uniform_ =
      glGetUniformLocation(entity_program_, "u_player_skin_atlas");
  entity_daylight_color_uniform_ = glGetUniformLocation(entity_program_, "u_daylight_color");
  entity_daylight_strength_uniform_ = glGetUniformLocation(entity_program_, "u_daylight_strength");
  entity_ambient_color_uniform_ = glGetUniformLocation(entity_program_, "u_ambient_color");
  entity_bone_count_uniform_ = glGetUniformLocation(entity_program_, "u_bone_count");
  entity_bone_pivots_uniform_ = glGetUniformLocation(entity_program_, "u_bone_pivots[0]");
  entity_bone_rotations_uniform_ = glGetUniformLocation(entity_program_, "u_bone_rotations[0]");
  entity_bone_positions_uniform_ = glGetUniformLocation(entity_program_, "u_bone_positions[0]");
  return true;
}

bool OpenGLRenderer::ensure_ui_program() {
  if (ui_program_ != 0) {
    return true;
  }

  if (ui_vertex_shader_source_.empty() || ui_fragment_shader_source_.empty()) {
    return false;
  }

  const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, ui_vertex_shader_source_.c_str());
  const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, ui_fragment_shader_source_.c_str());
  if (vertex_shader == 0 || fragment_shader == 0) {
    if (vertex_shader != 0) {
      glDeleteShader(vertex_shader);
    }
    if (fragment_shader != 0) {
      glDeleteShader(fragment_shader);
    }
    return false;
  }

  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glLinkProgram(program);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  GLint success = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (success != GL_TRUE) {
    glDeleteProgram(program);
    return false;
  }

  ui_program_ = program;
  ui_viewport_uniform_ = glGetUniformLocation(ui_program_, "u_viewport_size");
  ui_texture_uniform_ = glGetUniformLocation(ui_program_, "u_textures");
  return true;
}

bool OpenGLRenderer::ensure_sky_program() {
  if (sky_program_ != 0) {
    return true;
  }

  if (sky_vertex_shader_source_.empty() || sky_fragment_shader_source_.empty()) {
    return false;
  }

  const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, sky_vertex_shader_source_.c_str());
  const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, sky_fragment_shader_source_.c_str());
  if (vertex_shader == 0 || fragment_shader == 0) {
    if (vertex_shader != 0) {
      glDeleteShader(vertex_shader);
    }
    if (fragment_shader != 0) {
      glDeleteShader(fragment_shader);
    }
    return false;
  }

  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex_shader);
  glAttachShader(program, fragment_shader);
  glLinkProgram(program);
  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  GLint success = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (success != GL_TRUE) {
    glDeleteProgram(program);
    return false;
  }

  sky_program_ = program;
  sky_camera_forward_uniform_ = glGetUniformLocation(sky_program_, "u_camera_forward");
  sky_camera_right_uniform_ = glGetUniformLocation(sky_program_, "u_camera_right");
  sky_camera_up_uniform_ = glGetUniformLocation(sky_program_, "u_camera_up");
  sky_camera_aspect_uniform_ = glGetUniformLocation(sky_program_, "u_camera_aspect");
  sky_camera_fov_y_uniform_ = glGetUniformLocation(sky_program_, "u_camera_fov_y_degrees");
  sky_sun_direction_uniform_ = glGetUniformLocation(sky_program_, "u_sun_direction");
  sky_moon_direction_uniform_ = glGetUniformLocation(sky_program_, "u_moon_direction");
  sky_daylight_color_uniform_ = glGetUniformLocation(sky_program_, "u_daylight_color");
  sky_ambient_color_uniform_ = glGetUniformLocation(sky_program_, "u_ambient_color");
  sky_fog_color_uniform_ = glGetUniformLocation(sky_program_, "u_fog_color");
  sky_daylight_strength_uniform_ = glGetUniformLocation(sky_program_, "u_daylight_strength");
  sky_moonlight_strength_uniform_ = glGetUniformLocation(sky_program_, "u_moonlight_strength");
  sky_star_visibility_uniform_ = glGetUniformLocation(sky_program_, "u_star_visibility");
  sky_day_fraction_uniform_ = glGetUniformLocation(sky_program_, "u_day_fraction");
  sky_cloud_time_uniform_ = glGetUniformLocation(sky_program_, "u_cloud_time_seconds");
  sky_cirrus_amount_uniform_ = glGetUniformLocation(sky_program_, "u_cirrus_amount");
  sky_cumulus_amount_uniform_ = glGetUniformLocation(sky_program_, "u_cumulus_amount");
  sky_cloud_intensity_uniform_ = glGetUniformLocation(sky_program_, "u_cloud_intensity");
  sky_exposure_uniform_ = glGetUniformLocation(sky_program_, "u_exposure");
  return true;
}

void OpenGLRenderer::draw_sky() {
  ARV_PROFILE_ZONE("Sky draw");
  if (!frame_.sky.enabled || !ensure_sky_program()) {
    return;
  }

  if (sky_vao_ == 0) {
    glGenVertexArrays(1, &sky_vao_);
  }

  glDepthMask(GL_FALSE);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);

  glUseProgram(sky_program_);
  glUniform3f(
      sky_camera_forward_uniform_,
      frame_.world_view.forward.x,
      frame_.world_view.forward.y,
      frame_.world_view.forward.z);
  glUniform3f(
      sky_camera_right_uniform_,
      frame_.world_view.right.x,
      frame_.world_view.right.y,
      frame_.world_view.right.z);
  glUniform3f(
      sky_camera_up_uniform_,
      frame_.world_view.up.x,
      frame_.world_view.up.y,
      frame_.world_view.up.z);
  const f32 aspect = height_ == 0 ? 1.0F : static_cast<f32>(width_) / static_cast<f32>(height_);
  glUniform1f(sky_camera_aspect_uniform_, aspect);
  glUniform1f(sky_camera_fov_y_uniform_, frame_.world_view.fov_y_degrees);
  glUniform3f(
      sky_sun_direction_uniform_,
      frame_.sky.sun_direction.x,
      frame_.sky.sun_direction.y,
      frame_.sky.sun_direction.z);
  glUniform3f(
      sky_moon_direction_uniform_,
      frame_.sky.moon_direction.x,
      frame_.sky.moon_direction.y,
      frame_.sky.moon_direction.z);
  glUniform3f(
      sky_daylight_color_uniform_,
      frame_.sky.daylight_color.x,
      frame_.sky.daylight_color.y,
      frame_.sky.daylight_color.z);
  glUniform3f(
      sky_ambient_color_uniform_,
      frame_.sky.ambient_color.x,
      frame_.sky.ambient_color.y,
      frame_.sky.ambient_color.z);
  glUniform3f(
      sky_fog_color_uniform_,
      frame_.sky.fog_color.x,
      frame_.sky.fog_color.y,
      frame_.sky.fog_color.z);
  glUniform1f(sky_daylight_strength_uniform_, frame_.sky.daylight_strength);
  glUniform1f(sky_moonlight_strength_uniform_, frame_.sky.moonlight_strength);
  glUniform1f(sky_star_visibility_uniform_, frame_.sky.star_visibility);
  glUniform1f(sky_day_fraction_uniform_, frame_.sky.day_fraction);
  glUniform1f(sky_cloud_time_uniform_, frame_.sky.cloud_time_seconds);
  glUniform1f(sky_cirrus_amount_uniform_, frame_.sky.cirrus_amount);
  glUniform1f(sky_cumulus_amount_uniform_, frame_.sky.cumulus_amount);
  glUniform1f(sky_cloud_intensity_uniform_, frame_.sky.cloud_intensity);
  glUniform1f(sky_exposure_uniform_, frame_.sky.exposure);

  glBindVertexArray(sky_vao_);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
  ++draw_calls_last_frame_;
  vertices_submitted_last_frame_ += 4U;
  triangles_submitted_last_frame_ += 2U;

  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
}

void OpenGLRenderer::draw_chunk_list(const std::vector<ChunkDrawItem>& draw_items, bool transparent) {
  ARV_PROFILE_ZONE("Chunk draw list");
  glUniform1f(chunk_alpha_uniform_, transparent ? 0.55F : 1.0F);
  for (const ChunkDrawItem& item : draw_items) {
    if (transparent ? !item.has_transparent_mesh : !item.has_opaque_mesh) {
      continue;
    }

    const auto found = chunks_.find(item.key);
    if (!should_draw_chunk_mesh(found == chunks_.end() ? nullptr : &found->second.state, item)) {
      continue;
    }

    const ChunkGpuState& chunk = found->second;
    const GpuMesh& mesh = transparent ? chunk.transparent : chunk.opaque;
    if (mesh.index_count == 0) {
      continue;
    }

    glUniform3f(
        chunk_origin_uniform_,
        static_cast<f32>(item.key.coord.value.x * ChunkSize),
        static_cast<f32>(item.key.coord.value.y * ChunkSize),
        static_cast<f32>(item.key.coord.value.z * ChunkSize));
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.index_count, GL_UNSIGNED_INT, nullptr);
    vertices_submitted_last_frame_ += static_cast<u64>(mesh.index_count);
    triangles_submitted_last_frame_ += static_cast<u64>(mesh.index_count / 3);
    ++chunk_draw_calls_last_frame_;
    ++draw_calls_last_frame_;
  }
}

void OpenGLRenderer::draw_entities() {
  const f32 aspect = height_ == 0 ? 1.0F : static_cast<f32>(width_) / static_cast<f32>(height_);
  draw_entities_for(frame_.entities, frame_.world_view, frame_.sky, aspect);
}

void OpenGLRenderer::draw_entities_for(
    const std::vector<EntityDrawItem>& entities,
    const RenderView& view,
    const SkyUniforms& sky,
    f32 aspect) {
  ARV_PROFILE_ZONE("Entity draw batch");
  if (entities.empty() || entity_models_.empty() || entity_gpu_models_.empty()) {
    return;
  }
  if (!ensure_entity_program()) {
    return;
  }

  entity_draw_order_.clear();
  entity_draw_order_.reserve(entities.size());
  for (const EntityDrawItem& draw : entities) {
    const auto model = entity_models_.find(draw.model_id);
    const auto gpu = entity_gpu_models_.find(draw.model_id);
    if (model == entity_models_.end() || gpu == entity_gpu_models_.end()) {
      continue;
    }
    if (gpu->second.index_count == 0 || model->second.bones.size() > MaxEntityShaderBones) {
      continue;
    }
    entity_draw_order_.push_back(&draw);
  }
  if (entity_draw_order_.empty()) {
    return;
  }

  std::sort(
      entity_draw_order_.begin(),
      entity_draw_order_.end(),
      [](const EntityDrawItem* lhs, const EntityDrawItem* rhs) {
        if (lhs->model_id != rhs->model_id) {
          return lhs->model_id < rhs->model_id;
        }
        if (lhs->animation_id != rhs->animation_id) {
          return lhs->animation_id < rhs->animation_id;
        }
        if (const i32 animations = compare_entity_animation_playbacks(
                lhs->animations,
                rhs->animations);
            animations != 0) {
          return animations < 0;
        }
        return compare_bone_rotation_overrides(
                   lhs->bone_rotation_overrides,
                   rhs->bone_rotation_overrides) < 0;
      });

  const auto same_entity_batch = [](const EntityDrawItem& lhs, const EntityDrawItem& rhs) {
    return lhs.model_id == rhs.model_id &&
           lhs.animation_id == rhs.animation_id &&
           lhs.animations == rhs.animations &&
           lhs.bone_rotation_overrides == rhs.bone_rotation_overrides;
  };

  glUseProgram(entity_program_);
  glUniform3f(
      entity_camera_position_uniform_,
      view.position.x,
      view.position.y,
      view.position.z);
  glUniform3f(
      entity_camera_forward_uniform_,
      view.forward.x,
      view.forward.y,
      view.forward.z);
  glUniform3f(
      entity_camera_right_uniform_,
      view.right.x,
      view.right.y,
      view.right.z);
  glUniform3f(
      entity_camera_up_uniform_,
      view.up.x,
      view.up.y,
      view.up.z);
  glUniform1f(entity_camera_aspect_uniform_, aspect);
  glUniform1f(entity_camera_fov_y_uniform_, view.fov_y_degrees);
  glUniform1f(entity_camera_near_uniform_, view.near_plane);
  glUniform1f(entity_camera_far_uniform_, view.far_plane);
  glUniform2f(
      entity_atlas_size_uniform_,
      static_cast<f32>(entity_atlas_.width == 0 ? 1U : entity_atlas_.width),
      static_cast<f32>(entity_atlas_.height == 0 ? 1U : entity_atlas_.height));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(
      GL_TEXTURE_2D_ARRAY,
      entity_atlas_.id == 0 ? white_texture_.id : entity_atlas_.id);
  glUniform1i(entity_atlas_uniform_, 0);
  glUniform2f(
      entity_player_skin_atlas_size_uniform_,
      static_cast<f32>(player_skin_atlas_.page_width() == 0U ? 1U : player_skin_atlas_.page_width()),
      static_cast<f32>(player_skin_atlas_.page_height() == 0U ? 1U : player_skin_atlas_.page_height()));
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D_ARRAY, player_skin_atlas_.gl_texture_array());
  glUniform1i(entity_player_skin_atlas_uniform_, 1);
  glUniform3f(
      entity_daylight_color_uniform_,
      sky.daylight_color.x,
      sky.daylight_color.y,
      sky.daylight_color.z);
  glUniform1f(entity_daylight_strength_uniform_, sky.daylight_strength);
  glUniform3f(
      entity_ambient_color_uniform_,
      sky.ambient_color.x,
      sky.ambient_color.y,
      sky.ambient_color.z);

  usize batch_begin = 0;
  while (batch_begin < entity_draw_order_.size()) {
    usize batch_end = batch_begin + 1U;
    while (batch_end < entity_draw_order_.size() &&
           same_entity_batch(*entity_draw_order_[batch_begin], *entity_draw_order_[batch_end])) {
      ++batch_end;
    }

    const EntityDrawItem& first_draw = *entity_draw_order_[batch_begin];
    const EntityModelAsset& model = entity_models_.find(first_draw.model_id)->second;
    const auto animation_found = entity_animations_.find(first_draw.animation_id);
    const EntityAnimationAsset* animation =
        animation_found == entity_animations_.end() ? nullptr : &animation_found->second;
    EntityGpuModel& gpu = entity_gpu_models_.find(first_draw.model_id)->second;
    const auto bone_count = static_cast<GLsizei>(model.bones.size());
    for (usize bone_index = 0; bone_index < model.bones.size(); ++bone_index) {
      entity_bone_pivots_[bone_index] = model.bones[bone_index].pivot;
    }
    std::span<Vec3f> bone_rotations{
        entity_bone_rotations_.data(),
        model.bones.size()};
    std::span<Vec3f> bone_positions{
        entity_bone_positions_.data(),
        model.bones.size()};
    sample_entity_model_animations(
        model,
        animation,
        first_draw.animations,
        bone_rotations,
        bone_positions);
    apply_bone_rotation_overrides(
        model,
        bone_rotations,
        first_draw.bone_rotation_overrides);
    glUniform1i(entity_bone_count_uniform_, bone_count);
    if (bone_count > 0) {
      glUniform3fv(
          entity_bone_pivots_uniform_,
          bone_count,
          reinterpret_cast<const GLfloat*>(entity_bone_pivots_.data()));
      glUniform3fv(
          entity_bone_rotations_uniform_,
          bone_count,
          reinterpret_cast<const GLfloat*>(entity_bone_rotations_.data()));
      glUniform3fv(
          entity_bone_positions_uniform_,
          bone_count,
          reinterpret_cast<const GLfloat*>(entity_bone_positions_.data()));
    }

    entity_instances_.clear();
    entity_instances_.reserve(batch_end - batch_begin);
    for (usize index = batch_begin; index < batch_end; ++index) {
      const EntityDrawItem& draw = *entity_draw_order_[index];
      EntityInstanceData instance;
      instance.position = draw.position;
      instance.rotation = draw.rotation;
      instance.scale = draw.scale;
      if (draw.player_skin_region.valid()) {
        const std::optional<AtlasRegion> skin_region =
            player_skin_atlas_.resolve(draw.player_skin_region);
        if (skin_region.has_value() &&
            player_skin_atlas_.mark_used(
                draw.player_skin_region,
                render_frame_index_,
                std::chrono::steady_clock::now(),
                true)) {
          instance.player_skin_region = {
              static_cast<f32>(skin_region->x),
              static_cast<f32>(skin_region->y),
              static_cast<f32>(skin_region->width),
              static_cast<f32>(skin_region->height)};
          instance.player_skin_page = skin_region->page_index;
        }
      }
      entity_instances_.push_back(instance);
    }

    glBindVertexArray(gpu.vao);
    const GLsizeiptr instance_bytes =
        static_cast<GLsizeiptr>(entity_instances_.size() * sizeof(EntityInstanceData));
    ensure_buffer_capacity(
        GL_ARRAY_BUFFER,
        gpu.instance_buffer,
        gpu.instance_capacity_bytes,
        instance_bytes,
        GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, instance_bytes, entity_instances_.data());
    ++gpu_buffer_uploads_last_frame_;
    gpu_buffer_upload_bytes_last_frame_ += static_cast<u64>(instance_bytes);
    glDrawElementsInstanced(
        GL_TRIANGLES,
        gpu.index_count,
        GL_UNSIGNED_INT,
        nullptr,
        static_cast<GLsizei>(entity_instances_.size()));
    vertices_submitted_last_frame_ +=
        static_cast<u64>(gpu.index_count) * static_cast<u64>(entity_instances_.size());
    triangles_submitted_last_frame_ +=
        static_cast<u64>(gpu.index_count / 3) * static_cast<u64>(entity_instances_.size());
    ++draw_calls_last_frame_;

    batch_begin = batch_end;
  }

  glBindVertexArray(0);
  glUseProgram(0);
}

void OpenGLRenderer::draw_entity_previews() {
  if (frame_.entity_previews.empty() || width_ == 0U || height_ == 0U) {
    return;
  }

  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glEnable(GL_SCISSOR_TEST);

  for (const EntityPreviewDrawItem& preview : frame_.entity_previews) {
    const f32 min_x = std::clamp(preview.viewport_min.x, 0.0F, static_cast<f32>(width_));
    const f32 min_y = std::clamp(preview.viewport_min.y, 0.0F, static_cast<f32>(height_));
    const f32 max_x = std::clamp(
        preview.viewport_min.x + preview.viewport_size.x,
        0.0F,
        static_cast<f32>(width_));
    const f32 max_y = std::clamp(
        preview.viewport_min.y + preview.viewport_size.y,
        0.0F,
        static_cast<f32>(height_));
    const GLint x = static_cast<GLint>(std::floor(min_x));
    const GLint y_top = static_cast<GLint>(std::floor(min_y));
    const GLsizei w = static_cast<GLsizei>(std::ceil(max_x - min_x));
    const GLsizei h = static_cast<GLsizei>(std::ceil(max_y - min_y));
    if (w <= 0 || h <= 0) {
      continue;
    }

    const GLint gl_y = static_cast<GLint>(height_) - y_top - h;
    glViewport(x, gl_y, w, h);
    glScissor(x, gl_y, w, h);
    glClearColor(
        preview.clear_color.x,
        preview.clear_color.y,
        preview.clear_color.z,
        preview.clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    const f32 aspect = h == 0 ? 1.0F : static_cast<f32>(w) / static_cast<f32>(h);
    draw_entities_for(preview.entities, preview.view, preview.sky, aspect);
  }

  glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
  glDisable(GL_SCISSOR_TEST);
  glBindVertexArray(0);
  glUseProgram(0);
}

void OpenGLRenderer::draw_ui() {
  if (frame_.ui_rects.empty() && frame_.ui_textured_quads.empty() && frame_.ui_glyphs.empty()) {
    return;
  }
  if (!ensure_ui_program()) {
    return;
  }

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(ui_program_);
  glUniform2f(ui_viewport_uniform_, static_cast<f32>(width_), static_cast<f32>(height_));
  const std::array<GLint, 5> texture_units{{0, 1, 2, 3, 4}};
  glUniform1iv(
      ui_texture_uniform_,
      static_cast<GLsizei>(texture_units.size()),
      texture_units.data());

  bind_ui_textures();
  draw_ui_shapes();
  draw_ui_quads();
  glActiveTexture(GL_TEXTURE0);

  glBindVertexArray(0);
  glUseProgram(0);
  glDisable(GL_BLEND);
}

void OpenGLRenderer::bind_ui_textures() {
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, white_texture_.id);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D_ARRAY, font_atlas_.id == 0 ? white_texture_.id : font_atlas_.id);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D_ARRAY, terrain_atlas_.id == 0 ? white_texture_.id : terrain_atlas_.id);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D_ARRAY, item_atlas_.id == 0 ? white_texture_.id : item_atlas_.id);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(
      GL_TEXTURE_2D_ARRAY,
      player_skin_atlas_.gl_texture_array() == 0 ? white_texture_.id
                                                 : player_skin_atlas_.gl_texture_array());
}

void OpenGLRenderer::draw_ui_shapes() {
  const usize shape_count = frame_.ui_rects.size();
  if (shape_count == 0U) {
    return;
  }
  ensure_ui_shape_buffers();

  ui_shape_vertices_.clear();
  ui_shape_indices_.clear();
  ui_shape_draw_runs_.clear();
  ui_shape_vertices_.reserve(shape_count * 4U);
  ui_shape_indices_.reserve(shape_count * 6U);
  ui_shape_draw_runs_.reserve(shape_count);

  for (const UIRectDrawItem& rect : frame_.ui_rects) {
    append_ui_rect_item(ui_shape_vertices_, ui_shape_indices_, ui_shape_draw_runs_, rect);
  }
  if (ui_shape_vertices_.empty()) {
    return;
  }

  glBindVertexArray(ui_shape_vao_);
  glVertexAttrib1f(3, UiTextureSlotWhite);
  glVertexAttribI1ui(4, 0U);
  const GLsizeiptr vertex_bytes =
      static_cast<GLsizeiptr>(ui_shape_vertices_.size() * sizeof(UIShapeVertex));
  ensure_buffer_capacity(
      GL_ARRAY_BUFFER,
      ui_shape_vertex_buffer_,
      ui_shape_vertex_capacity_bytes_,
      vertex_bytes,
      GL_DYNAMIC_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, ui_shape_vertices_.data());
  ++gpu_buffer_uploads_last_frame_;
  gpu_buffer_upload_bytes_last_frame_ += static_cast<u64>(vertex_bytes);
  const GLsizeiptr index_bytes =
      static_cast<GLsizeiptr>(ui_shape_indices_.size() * sizeof(u32));
  ensure_buffer_capacity(
      GL_ELEMENT_ARRAY_BUFFER,
      ui_shape_index_buffer_,
      ui_shape_index_capacity_bytes_,
      index_bytes,
      GL_DYNAMIC_DRAW);
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes, ui_shape_indices_.data());
  ++gpu_buffer_uploads_last_frame_;
  gpu_buffer_upload_bytes_last_frame_ += static_cast<u64>(index_bytes);
  for (const UiDrawRun& run : ui_shape_draw_runs_) {
    apply_ui_scissor(run.clip, height_);
    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(run.index_count),
        GL_UNSIGNED_INT,
        reinterpret_cast<const void*>(static_cast<usize>(run.first_index) * sizeof(u32)));
    ++ui_draw_calls_last_frame_;
    ++draw_calls_last_frame_;
  }
  glDisable(GL_SCISSOR_TEST);
  vertices_submitted_last_frame_ += static_cast<u64>(ui_shape_indices_.size());
  triangles_submitted_last_frame_ += static_cast<u64>(ui_shape_indices_.size() / 3U);
}

void OpenGLRenderer::draw_ui_quads() {
  const usize quad_count =
      frame_.ui_textured_quads.size() + (font_atlas_.id == 0 ? 0U : frame_.ui_glyphs.size());
  if (quad_count == 0U) {
    return;
  }
  ensure_ui_buffers();

  ui_vertices_.clear();
  ui_indices_.clear();
  ui_draw_runs_.clear();
  ui_vertices_.reserve(quad_count * 4U);
  ui_indices_.reserve(quad_count * 6U);
  ui_draw_runs_.reserve(quad_count);

  for (const UITexturedQuadDrawItem& quad : frame_.ui_textured_quads) {
    append_ui_quad(
        ui_vertices_,
        ui_indices_,
        ui_draw_runs_,
        quad.positions,
        quad.uvs,
        quad.color,
        texture_slot_for_ui_source(quad.source),
        quad.page,
        clip_from_draw_item(quad.clip_min, quad.clip_max, quad.clip));
  }

  if (font_atlas_.id != 0) {
    for (const UIGlyphDrawItem& glyph : frame_.ui_glyphs) {
      append_ui_quad(
          ui_vertices_,
          ui_indices_,
          ui_draw_runs_,
          {{{glyph.min.x, glyph.min.y},
            {glyph.max.x, glyph.min.y},
            {glyph.max.x, glyph.max.y},
            {glyph.min.x, glyph.max.y}}},
          {{{glyph.uv_min.x, glyph.uv_min.y},
            {glyph.uv_max.x, glyph.uv_min.y},
            {glyph.uv_max.x, glyph.uv_max.y},
            {glyph.uv_min.x, glyph.uv_max.y}}},
          glyph.color,
          UiTextureSlotFont,
          glyph.page,
          clip_from_draw_item(glyph.clip_min, glyph.clip_max, glyph.clip));
    }
  }
  if (ui_vertices_.empty()) {
    return;
  }

  glBindVertexArray(ui_vao_);
  const GLsizeiptr vertex_bytes =
      static_cast<GLsizeiptr>(ui_vertices_.size() * sizeof(UIVertex));
  ensure_buffer_capacity(
      GL_ARRAY_BUFFER,
      ui_vertex_buffer_,
      ui_vertex_capacity_bytes_,
      vertex_bytes,
      GL_DYNAMIC_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_bytes, ui_vertices_.data());
  ++gpu_buffer_uploads_last_frame_;
  gpu_buffer_upload_bytes_last_frame_ += static_cast<u64>(vertex_bytes);
  const GLsizeiptr index_bytes =
      static_cast<GLsizeiptr>(ui_indices_.size() * sizeof(u32));
  ensure_buffer_capacity(
      GL_ELEMENT_ARRAY_BUFFER,
      ui_index_buffer_,
      ui_index_capacity_bytes_,
      index_bytes,
      GL_DYNAMIC_DRAW);
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes, ui_indices_.data());
  ++gpu_buffer_uploads_last_frame_;
  gpu_buffer_upload_bytes_last_frame_ += static_cast<u64>(index_bytes);
  for (const UiDrawRun& run : ui_draw_runs_) {
    apply_ui_scissor(run.clip, height_);
    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(run.index_count),
        GL_UNSIGNED_INT,
        reinterpret_cast<const void*>(static_cast<usize>(run.first_index) * sizeof(u32)));
    ++ui_draw_calls_last_frame_;
    ++draw_calls_last_frame_;
  }
  glDisable(GL_SCISSOR_TEST);
  vertices_submitted_last_frame_ += static_cast<u64>(ui_indices_.size());
  triangles_submitted_last_frame_ += static_cast<u64>(ui_indices_.size() / 3U);
  glActiveTexture(GL_TEXTURE0);
}

void OpenGLRenderer::ensure_ui_buffers() {
  if (ui_vao_ != 0) {
    return;
  }

  glGenVertexArrays(1, &ui_vao_);
  glBindVertexArray(ui_vao_);
  glGenBuffers(1, &ui_vertex_buffer_);
  glGenBuffers(1, &ui_index_buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, ui_vertex_buffer_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui_index_buffer_);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(
      0,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(UIVertex),
      reinterpret_cast<const void*>(offsetof(UIVertex, position)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(
      1,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(UIVertex),
      reinterpret_cast<const void*>(offsetof(UIVertex, uv)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(
      2,
      4,
      GL_FLOAT,
      GL_FALSE,
      sizeof(UIVertex),
      reinterpret_cast<const void*>(offsetof(UIVertex, color)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(
      3,
      1,
      GL_FLOAT,
      GL_FALSE,
      sizeof(UIVertex),
      reinterpret_cast<const void*>(offsetof(UIVertex, texture_slot)));
  glEnableVertexAttribArray(4);
  glVertexAttribIPointer(
      4,
      1,
      GL_UNSIGNED_INT,
      sizeof(UIVertex),
      reinterpret_cast<const void*>(offsetof(UIVertex, atlas_page)));
  glVertexAttrib2f(5, 0.0F, 0.0F);
  glVertexAttrib4f(6, 0.0F, 0.0F, 0.0F, 0.0F);
  glVertexAttrib1f(7, 0.0F);
  glVertexAttrib1f(8, 0.0F);
  glBindVertexArray(0);
}

void OpenGLRenderer::ensure_ui_shape_buffers() {
  if (ui_shape_vao_ != 0) {
    return;
  }

  glGenVertexArrays(1, &ui_shape_vao_);
  glBindVertexArray(ui_shape_vao_);
  glGenBuffers(1, &ui_shape_vertex_buffer_);
  glGenBuffers(1, &ui_shape_index_buffer_);
  glBindBuffer(GL_ARRAY_BUFFER, ui_shape_vertex_buffer_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui_shape_index_buffer_);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(
      0,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(UIShapeVertex),
      reinterpret_cast<const void*>(offsetof(UIShapeVertex, position)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(
      1,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(UIShapeVertex),
      reinterpret_cast<const void*>(offsetof(UIShapeVertex, uv)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(
      2,
      4,
      GL_FLOAT,
      GL_FALSE,
      sizeof(UIShapeVertex),
      reinterpret_cast<const void*>(offsetof(UIShapeVertex, color)));
  glVertexAttrib1f(3, UiTextureSlotWhite);
  glVertexAttribI1ui(4, 0U);
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(
      5,
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(UIShapeVertex),
      reinterpret_cast<const void*>(offsetof(UIShapeVertex, rect_size)));
  glEnableVertexAttribArray(6);
  glVertexAttribPointer(
      6,
      4,
      GL_FLOAT,
      GL_FALSE,
      sizeof(UIShapeVertex),
      reinterpret_cast<const void*>(offsetof(UIShapeVertex, border_color)));
  glEnableVertexAttribArray(7);
  glVertexAttribPointer(
      7,
      1,
      GL_FLOAT,
      GL_FALSE,
      sizeof(UIShapeVertex),
      reinterpret_cast<const void*>(offsetof(UIShapeVertex, radius)));
  glEnableVertexAttribArray(8);
  glVertexAttribPointer(
      8,
      1,
      GL_FLOAT,
      GL_FALSE,
      sizeof(UIShapeVertex),
      reinterpret_cast<const void*>(offsetof(UIShapeVertex, border_width)));
  glBindVertexArray(0);
}

void OpenGLRenderer::ensure_white_texture() {
  if (white_texture_.id != 0) {
    return;
  }
  TextureArrayUploadData upload;
  upload.width = 1;
  upload.height = 1;
  upload.layers = 1;
  upload.rgba_pixels = {255U, 255U, 255U, 255U};
  white_texture_ = create_texture_array(upload, false);
}

} // namespace arv::render
