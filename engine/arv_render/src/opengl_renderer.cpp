#include "arv/render/opengl_renderer.hpp"

#include "arv/core/profiling.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
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
    const std::array<Vec2f, 4>& positions,
    const std::array<Vec2f, 4>& uvs,
    Vec4f color,
    f32 texture_slot) {
  const u32 base = static_cast<u32>(vertices.size());
  vertices.emplace_back(positions[0], uvs[0], color, texture_slot);
  vertices.emplace_back(positions[1], uvs[1], color, texture_slot);
  vertices.emplace_back(positions[2], uvs[2], color, texture_slot);
  vertices.emplace_back(positions[3], uvs[3], color, texture_slot);
  indices.emplace_back(base);
  indices.emplace_back(base + 1U);
  indices.emplace_back(base + 2U);
  indices.emplace_back(base + 2U);
  indices.emplace_back(base + 3U);
  indices.emplace_back(base);
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

void OpenGLRenderer::set_ui_shader_sources(std::string vertex_source, std::string fragment_source) {
  ui_vertex_shader_source_ = std::move(vertex_source);
  ui_fragment_shader_source_ = std::move(fragment_source);
  if (ui_program_ != 0) {
    glDeleteProgram(ui_program_);
    ui_program_ = 0;
  }
}

void OpenGLRenderer::set_chunk_texture_atlas(TextureUploadData upload) {
  destroy_gpu_texture(terrain_atlas_);
  if (upload.valid()) {
    terrain_atlas_ = create_texture(upload, true);
  }
}

void OpenGLRenderer::set_ui_font_atlas(TextureUploadData upload) {
  destroy_gpu_texture(font_atlas_);
  if (upload.valid()) {
    font_atlas_ = create_texture(upload, false);
  }
}

void OpenGLRenderer::set_ui_item_atlas(TextureUploadData upload) {
  destroy_gpu_texture(item_atlas_);
  if (upload.valid()) {
    item_atlas_ = create_texture(upload, false);
  }
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
  draw_calls_last_frame_ = 0;
  chunk_draw_calls_last_frame_ = 0;
  ui_draw_calls_last_frame_ = 0;
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
  glBindTexture(GL_TEXTURE_2D, terrain_atlas_.id == 0 ? white_texture_.id : terrain_atlas_.id);
  glUniform1i(chunk_terrain_atlas_uniform_, 0);

  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  draw_chunk_list(frame_.opaque_chunks, false);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
  draw_chunk_list(frame_.transparent_chunks, true);
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
  glBindVertexArray(0);
  glUseProgram(0);

  draw_ui();
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
  if (chunk_program_ != 0) {
    glDeleteProgram(chunk_program_);
    chunk_program_ = 0;
  }
  if (ui_program_ != 0) {
    glDeleteProgram(ui_program_);
    ui_program_ = 0;
  }
  destroy_gpu_texture(terrain_atlas_);
  destroy_gpu_texture(font_atlas_);
  destroy_gpu_texture(item_atlas_);
  destroy_gpu_texture(white_texture_);
  if (ui_index_buffer_ != 0) {
    glDeleteBuffers(1, &ui_index_buffer_);
  }
  if (ui_vertex_buffer_ != 0) {
    glDeleteBuffers(1, &ui_vertex_buffer_);
  }
  if (ui_vao_ != 0) {
    glDeleteVertexArrays(1, &ui_vao_);
  }
  ui_index_buffer_ = 0;
  ui_vertex_buffer_ = 0;
  ui_vao_ = 0;
  ui_vertex_capacity_bytes_ = 0;
  ui_index_capacity_bytes_ = 0;
  gpu_profile_context_initialized_ = false;
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

OpenGLRenderer::GpuTexture OpenGLRenderer::create_texture(const TextureUploadData& upload, bool mipmaps) {
  GpuTexture texture;
  if (!upload.valid()) {
    return texture;
  }
  glGenTextures(1, &texture.id);
  glBindTexture(GL_TEXTURE_2D, texture.id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  const u32 full_max_level = mipmaps ? full_mipmap_max_level(upload.width, upload.height) : 0U;
  const u32 max_level = mipmaps && upload.limit_mipmap_levels
                            ? std::min(upload.max_mipmap_level, full_max_level)
                            : full_max_level;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, static_cast<GLint>(max_level));
  glTexParameteri(
      GL_TEXTURE_2D,
      GL_TEXTURE_MIN_FILTER,
      mipmaps ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
  glTexImage2D(
      GL_TEXTURE_2D,
      0,
      GL_RGBA8,
      static_cast<GLsizei>(upload.width),
      static_cast<GLsizei>(upload.height),
      0,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      upload.rgba_pixels.data());
  if (mipmaps) {
    glGenerateMipmap(GL_TEXTURE_2D);
  }
  texture.width = upload.width;
  texture.height = upload.height;
  return texture;
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

void OpenGLRenderer::draw_chunk_list(const std::vector<ChunkDrawItem>& draw_items, bool transparent) {
  ARV_PROFILE_ZONE("Chunk draw list");
  glUniform1f(chunk_alpha_uniform_, transparent ? 0.55F : 1.0F);
  for (const ChunkDrawItem& item : draw_items) {
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
    ++chunk_draw_calls_last_frame_;
    ++draw_calls_last_frame_;
  }
}

void OpenGLRenderer::draw_ui() {
  if (frame_.ui_rects.empty() && frame_.ui_textured_quads.empty() && frame_.ui_glyphs.empty()) {
    return;
  }
  if (!ensure_ui_program()) {
    return;
  }
  ensure_ui_buffers();

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(ui_program_);
  glUniform2f(ui_viewport_uniform_, static_cast<f32>(width_), static_cast<f32>(height_));
  const std::array<GLint, 4> texture_units{{0, 1, 2, 3}};
  glUniform1iv(
      ui_texture_uniform_,
      static_cast<GLsizei>(texture_units.size()),
      texture_units.data());

  draw_ui_quads();

  glBindVertexArray(0);
  glUseProgram(0);
  glDisable(GL_BLEND);
}

void OpenGLRenderer::draw_ui_quads() {
  const usize quad_count =
      frame_.ui_rects.size() + frame_.ui_textured_quads.size() +
      (font_atlas_.id == 0 ? 0U : frame_.ui_glyphs.size());
  if (quad_count == 0U) {
    return;
  }

  ui_vertices_.clear();
  ui_indices_.clear();
  ui_vertices_.reserve(quad_count * 4U);
  ui_indices_.reserve(quad_count * 6U);

  for (const UIRectDrawItem& rect : frame_.ui_rects) {
    append_ui_quad(
        ui_vertices_,
        ui_indices_,
        {{{rect.min.x, rect.min.y},
          {rect.max.x, rect.min.y},
          {rect.max.x, rect.max.y},
          {rect.min.x, rect.max.y}}},
        {{{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}}},
        rect.color,
        UiTextureSlotWhite);
  }

  for (const UITexturedQuadDrawItem& quad : frame_.ui_textured_quads) {
    if (quad.page != 0) {
      continue;
    }
    append_ui_quad(
        ui_vertices_,
        ui_indices_,
        quad.positions,
        quad.uvs,
        quad.color,
        texture_slot_for_ui_source(quad.source));
  }

  if (font_atlas_.id != 0) {
    for (const UIGlyphDrawItem& glyph : frame_.ui_glyphs) {
      if (glyph.page != 0) {
        continue;
      }
      append_ui_quad(
          ui_vertices_,
          ui_indices_,
          {{{glyph.min.x, glyph.min.y},
            {glyph.max.x, glyph.min.y},
            {glyph.max.x, glyph.max.y},
            {glyph.min.x, glyph.max.y}}},
          {{{glyph.uv_min.x, glyph.uv_min.y},
            {glyph.uv_max.x, glyph.uv_min.y},
            {glyph.uv_max.x, glyph.uv_max.y},
            {glyph.uv_min.x, glyph.uv_max.y}}},
          glyph.color,
          UiTextureSlotFont);
    }
  }
  if (ui_vertices_.empty()) {
    return;
  }

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, white_texture_.id);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, font_atlas_.id == 0 ? white_texture_.id : font_atlas_.id);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, terrain_atlas_.id == 0 ? white_texture_.id : terrain_atlas_.id);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, item_atlas_.id == 0 ? white_texture_.id : item_atlas_.id);

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
  const GLsizeiptr index_bytes =
      static_cast<GLsizeiptr>(ui_indices_.size() * sizeof(u32));
  ensure_buffer_capacity(
      GL_ELEMENT_ARRAY_BUFFER,
      ui_index_buffer_,
      ui_index_capacity_bytes_,
      index_bytes,
      GL_DYNAMIC_DRAW);
  glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, index_bytes, ui_indices_.data());
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(ui_indices_.size()), GL_UNSIGNED_INT, nullptr);
  ++ui_draw_calls_last_frame_;
  ++draw_calls_last_frame_;
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
  glBindVertexArray(0);
}

void OpenGLRenderer::ensure_white_texture() {
  if (white_texture_.id != 0) {
    return;
  }
  TextureUploadData upload;
  upload.width = 1;
  upload.height = 1;
  upload.rgba_pixels = {255U, 255U, 255U, 255U};
  white_texture_ = create_texture(upload, false);
}

} // namespace arv::render
