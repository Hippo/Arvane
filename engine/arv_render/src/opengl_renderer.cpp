#include "arv/render/opengl_renderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <utility>

namespace arv::render {

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

void OpenGLRenderer::resize(u32 width, u32 height) {
  width_ = width;
  height_ = height;
  glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
}

void OpenGLRenderer::submit_frame(FramePacket packet) {
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

  pending_chunk_mesh_uploads_.push_back(std::move(upload));
}

void OpenGLRenderer::render() {
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
  frame_ = {};
}

void OpenGLRenderer::process_chunk_mesh_uploads() {
  const auto start = std::chrono::steady_clock::now();
  mesh_uploads_last_frame_ = 0;
  u32 upload_attempts = 0;

  while (upload_attempts < max_chunk_mesh_uploads_per_frame_ &&
         !pending_chunk_mesh_uploads_.empty()) {
    ChunkMeshUpload upload = std::move(pending_chunk_mesh_uploads_.front());
    pending_chunk_mesh_uploads_.pop_front();
    ++upload_attempts;
    if (upload_chunk_mesh_now(std::move(upload))) {
      ++mesh_uploads_last_frame_;
    }
  }

  const auto end = std::chrono::steady_clock::now();
  mesh_upload_time_ms_last_frame_ =
      std::chrono::duration<double, std::milli>(end - start).count();
}

bool OpenGLRenderer::upload_chunk_mesh_now(ChunkMeshUpload upload) {
  const ChunkMeshKey key{upload.mesh.world_id, upload.mesh.coord};
  const auto found = chunks_.find(key);
  if (found != chunks_.end() && !should_accept_chunk_mesh_upload(&found->second.state, upload)) {
    return false;
  }

  if (found != chunks_.end()) {
    destroy_chunk_gpu_state(found->second);
  }

  ChunkGpuState gpu_state;
  gpu_state.opaque = create_gpu_mesh(upload.mesh.opaque);
  gpu_state.transparent = create_gpu_mesh(upload.mesh.transparent);
  gpu_state.state.opaque_mesh = gpu_state.opaque.index_count == 0 ? MeshID{} : allocate_mesh_id();
  gpu_state.state.transparent_mesh = gpu_state.transparent.index_count == 0 ? MeshID{} : allocate_mesh_id();
  gpu_state.state.uploaded_version = upload.mesh.source_version;
  gpu_state.state.resident = true;
  chunks_[key] = gpu_state;
  return true;
}

MeshID OpenGLRenderer::allocate_mesh_id() noexcept {
  return MeshID{next_mesh_id_++};
}

OpenGLRenderer::GpuMesh OpenGLRenderer::create_gpu_mesh(const CpuMeshBuffer& mesh) {
  GpuMesh gpu;
  if (mesh.vertex_bytes.empty() || mesh.indices.empty()) {
    return gpu;
  }

  glGenVertexArrays(1, &gpu.vao);
  glBindVertexArray(gpu.vao);

  glGenBuffers(1, &gpu.vertex_buffer);
  glBindBuffer(GL_ARRAY_BUFFER, gpu.vertex_buffer);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(mesh.vertex_bytes.size()),
      mesh.vertex_bytes.data(),
      GL_STATIC_DRAW);

  glGenBuffers(1, &gpu.index_buffer);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.index_buffer);
  glBufferData(
      GL_ELEMENT_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(u32)),
      mesh.indices.data(),
      GL_STATIC_DRAW);

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
      reinterpret_cast<const void*>(offsetof(ChunkVertex, light_face_flags)));

  gpu.index_count = static_cast<GLsizei>(mesh.indices.size());
  glBindVertexArray(0);
  return gpu;
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
  ui_texture_uniform_ = glGetUniformLocation(ui_program_, "u_texture");
  return true;
}

void OpenGLRenderer::draw_chunk_list(const std::vector<ChunkDrawItem>& draw_items, bool transparent) {
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
  }
}

void OpenGLRenderer::draw_ui() {
  if (frame_.ui_rects.empty() && frame_.ui_glyphs.empty()) {
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
  glUniform1i(ui_texture_uniform_, 0);

  draw_ui_rects();
  draw_ui_glyphs();

  glBindVertexArray(0);
  glUseProgram(0);
  glDisable(GL_BLEND);
}

void OpenGLRenderer::draw_ui_rects() {
  if (frame_.ui_rects.empty()) {
    return;
  }

  std::vector<UIVertex> vertices;
  std::vector<u32> indices;
  vertices.reserve(frame_.ui_rects.size() * 4U);
  indices.reserve(frame_.ui_rects.size() * 6U);
  for (const UIRectDrawItem& rect : frame_.ui_rects) {
    const u32 base = static_cast<u32>(vertices.size());
    vertices.push_back({{rect.min.x, rect.min.y}, {0.0F, 0.0F}, rect.color});
    vertices.push_back({{rect.max.x, rect.min.y}, {1.0F, 0.0F}, rect.color});
    vertices.push_back({{rect.max.x, rect.max.y}, {1.0F, 1.0F}, rect.color});
    vertices.push_back({{rect.min.x, rect.max.y}, {0.0F, 1.0F}, rect.color});
    indices.insert(indices.end(), {base, base + 1U, base + 2U, base + 2U, base + 3U, base});
  }

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, white_texture_.id);
  glBindVertexArray(ui_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, ui_vertex_buffer_);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(vertices.size() * sizeof(UIVertex)),
      vertices.data(),
      GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui_index_buffer_);
  glBufferData(
      GL_ELEMENT_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(indices.size() * sizeof(u32)),
      indices.data(),
      GL_DYNAMIC_DRAW);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
}

void OpenGLRenderer::draw_ui_glyphs() {
  if (frame_.ui_glyphs.empty() || font_atlas_.id == 0) {
    return;
  }

  std::vector<UIVertex> vertices;
  std::vector<u32> indices;
  vertices.reserve(frame_.ui_glyphs.size() * 4U);
  indices.reserve(frame_.ui_glyphs.size() * 6U);
  for (const UIGlyphDrawItem& glyph : frame_.ui_glyphs) {
    if (glyph.page != 0) {
      continue;
    }
    const u32 base = static_cast<u32>(vertices.size());
    vertices.push_back({{glyph.min.x, glyph.min.y}, {glyph.uv_min.x, glyph.uv_min.y}, glyph.color});
    vertices.push_back({{glyph.max.x, glyph.min.y}, {glyph.uv_max.x, glyph.uv_min.y}, glyph.color});
    vertices.push_back({{glyph.max.x, glyph.max.y}, {glyph.uv_max.x, glyph.uv_max.y}, glyph.color});
    vertices.push_back({{glyph.min.x, glyph.max.y}, {glyph.uv_min.x, glyph.uv_max.y}, glyph.color});
    indices.insert(indices.end(), {base, base + 1U, base + 2U, base + 2U, base + 3U, base});
  }
  if (vertices.empty()) {
    return;
  }

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, font_atlas_.id);
  glBindVertexArray(ui_vao_);
  glBindBuffer(GL_ARRAY_BUFFER, ui_vertex_buffer_);
  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(vertices.size() * sizeof(UIVertex)),
      vertices.data(),
      GL_DYNAMIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui_index_buffer_);
  glBufferData(
      GL_ELEMENT_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(indices.size() * sizeof(u32)),
      indices.data(),
      GL_DYNAMIC_DRAW);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);
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
