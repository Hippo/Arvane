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

  void resize(u32 width, u32 height) override;

  void submit_frame(FramePacket packet) override;

  void set_chunk_mesh_upload_budget(u32 max_uploads_per_frame) noexcept;

  void enqueue_chunk_mesh_upload(ChunkMeshUpload upload) override;

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

  void process_chunk_mesh_uploads();

  [[nodiscard]] bool upload_chunk_mesh_now(ChunkMeshUpload upload);

  [[nodiscard]] MeshID allocate_mesh_id() noexcept;

  [[nodiscard]] static GpuMesh create_gpu_mesh(const CpuMeshBuffer& mesh);

  static void destroy_gpu_mesh(GpuMesh& mesh);

  static void destroy_chunk_gpu_state(ChunkGpuState& state);

  [[nodiscard]] static GLuint compile_shader(GLenum type, const char* source);

  [[nodiscard]] bool ensure_chunk_program();

  void draw_chunk_list(const std::vector<ChunkDrawItem>& draw_items, bool transparent);

  u32 width_{};
  u32 height_{};
  u32 next_mesh_id_{1};
  u32 max_chunk_mesh_uploads_per_frame_{2};
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
  std::string chunk_vertex_shader_source_{};
  std::string chunk_fragment_shader_source_{};
  FramePacket frame_{};
  std::unordered_map<ChunkMeshKey, ChunkGpuState> chunks_;
  std::deque<ChunkMeshUpload> pending_chunk_mesh_uploads_;
};

} // namespace arv::render
