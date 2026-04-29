#pragma once

#include "arv/render/render_types.hpp"

#include <glad/glad.h>

#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace arv::render {

class OpenGLRenderer final : public IRenderer {
public:
  void set_chunk_shader_sources(std::string vertex_source, std::string fragment_source);
  void set_ui_shader_sources(std::string vertex_source, std::string fragment_source);
  void set_chunk_texture_atlas(TextureUploadData upload);
  void set_ui_font_atlas(TextureUploadData upload);
  void set_ui_item_atlas(TextureUploadData upload);

  void resize(u32 width, u32 height) override;

  void submit_frame(FramePacket packet) override;

  void set_chunk_mesh_upload_budget(u32 max_uploads_per_frame) noexcept;

  void enqueue_chunk_mesh_upload(ChunkMeshUpload upload) override;
  void remove_chunk_mesh(ChunkMeshKey key) override;

  [[nodiscard]] u32 mesh_uploads_last_frame() const noexcept {
    return mesh_uploads_last_frame_;
  }

  [[nodiscard]] usize pending_mesh_upload_count() const noexcept {
    return pending_chunk_mesh_uploads_.size();
  }

  [[nodiscard]] double mesh_upload_time_ms_last_frame() const noexcept {
    return mesh_upload_time_ms_last_frame_;
  }

  void render() override;

  void begin_shutdown() override;

private:
  struct GpuMesh {
    GLuint vao{};
    GLuint vertex_buffer{};
    GLuint index_buffer{};
    GLsizei index_count{};
  };

  struct ChunkGpuState {
    ChunkRenderState state{};
    GpuMesh opaque{};
    GpuMesh transparent{};
  };

  struct GpuTexture {
    GLuint id{};
    u32 width{};
    u32 height{};
  };

  void process_chunk_mesh_uploads();

  [[nodiscard]] bool upload_chunk_mesh_now(ChunkMeshUpload upload);

  [[nodiscard]] MeshID allocate_mesh_id() noexcept;

  [[nodiscard]] static GpuMesh create_gpu_mesh(const CpuMeshBuffer& mesh);

  static void upload_cpu_mesh_to_gpu(GpuMesh& gpu, const CpuMeshBuffer& mesh);

  static void destroy_gpu_mesh(GpuMesh& mesh);

  static void destroy_chunk_gpu_state(ChunkGpuState& state);

  static void destroy_gpu_texture(GpuTexture& texture);

  [[nodiscard]] static GpuTexture create_texture(const TextureUploadData& upload, bool mipmaps);

  [[nodiscard]] static GLuint compile_shader(GLenum type, const char* source);

  [[nodiscard]] bool ensure_chunk_program();
  [[nodiscard]] bool ensure_ui_program();

  void draw_chunk_list(const std::vector<ChunkDrawItem>& draw_items, bool transparent);
  void draw_ui();
  void draw_ui_rects();
  void draw_ui_textured_quads();
  void draw_ui_glyphs();
  void ensure_ui_buffers();
  void ensure_white_texture();
  [[nodiscard]] GLuint texture_for_ui_source(UiTextureSource source) const noexcept;

  u32 width_{};
  u32 height_{};
  bool viewport_set_{};
  u32 next_mesh_id_{1};
  u32 max_chunk_mesh_uploads_per_frame_{8};
  u32 mesh_uploads_last_frame_{};
  double mesh_upload_time_ms_last_frame_{};
  GLuint chunk_program_{};
  GLint chunk_origin_uniform_{-1};
  GLint chunk_camera_position_uniform_{-1};
  GLint chunk_camera_forward_uniform_{-1};
  GLint chunk_camera_right_uniform_{-1};
  GLint chunk_camera_up_uniform_{-1};
  GLint chunk_camera_aspect_uniform_{-1};
  GLint chunk_camera_fov_y_uniform_{-1};
  GLint chunk_camera_near_uniform_{-1};
  GLint chunk_camera_far_uniform_{-1};
  GLint chunk_alpha_uniform_{-1};
  GLint chunk_atlas_size_uniform_{-1};
  GLint chunk_terrain_atlas_uniform_{-1};
  GLuint ui_program_{};
  GLint ui_viewport_uniform_{-1};
  GLint ui_texture_uniform_{-1};
  std::string chunk_vertex_shader_source_{};
  std::string chunk_fragment_shader_source_{};
  std::string ui_vertex_shader_source_{};
  std::string ui_fragment_shader_source_{};
  FramePacket frame_{};
  std::unordered_map<ChunkMeshKey, ChunkGpuState> chunks_;
  std::deque<ChunkMeshUpload> pending_chunk_mesh_uploads_;
  GpuTexture terrain_atlas_{};
  GpuTexture font_atlas_{};
  GpuTexture item_atlas_{};
  GpuTexture white_texture_{};
  GLuint ui_vao_{};
  GLuint ui_vertex_buffer_{};
  GLuint ui_index_buffer_{};
  std::vector<UIVertex> ui_vertices_;
  std::vector<u32> ui_indices_;
};

} // namespace arv::render
