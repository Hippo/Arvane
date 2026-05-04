#pragma once

#include "arv/core/types.hpp"
#include "arv/render/entity_model.hpp"
#include "arv/render/render_types.hpp"

#include <glad/glad.h>

#include <array>
#include <deque>
#include <string>
#include <vector>

namespace arv::render {

class OpenGLRenderer final : public IRenderer {
public:
  void set_chunk_shader_sources(std::string vertex_source, std::string fragment_source);
  void set_entity_shader_sources(std::string vertex_source, std::string fragment_source);
  void set_ui_shader_sources(std::string vertex_source, std::string fragment_source);
  void set_chunk_texture_atlas(TextureUploadData upload);
  void set_entity_texture_atlas(TextureUploadData upload);
  void set_ui_font_atlas(TextureUploadData upload);
  void set_ui_item_atlas(TextureUploadData upload);
  void set_entity_model(EntityModelAsset model);
  void clear_entity_models();

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

  [[nodiscard]] u32 mesh_upload_discards_last_frame() const noexcept {
    return mesh_upload_discards_last_frame_;
  }

  [[nodiscard]] u32 draw_calls_last_frame() const noexcept {
    return draw_calls_last_frame_;
  }

  [[nodiscard]] u32 chunk_draw_calls_last_frame() const noexcept {
    return chunk_draw_calls_last_frame_;
  }

  [[nodiscard]] u32 ui_draw_calls_last_frame() const noexcept {
    return ui_draw_calls_last_frame_;
  }

  [[nodiscard]] u64 vertices_submitted_last_frame() const noexcept {
    return vertices_submitted_last_frame_;
  }

  [[nodiscard]] u64 triangles_submitted_last_frame() const noexcept {
    return triangles_submitted_last_frame_;
  }

  [[nodiscard]] u32 gpu_buffer_uploads_last_frame() const noexcept {
    return gpu_buffer_uploads_last_frame_;
  }

  [[nodiscard]] u64 gpu_buffer_upload_bytes_last_frame() const noexcept {
    return gpu_buffer_upload_bytes_last_frame_;
  }

  [[nodiscard]] u32 texture_uploads_last_frame() const noexcept {
    return texture_uploads_last_frame_;
  }

  [[nodiscard]] u64 texture_upload_bytes_last_frame() const noexcept {
    return texture_upload_bytes_last_frame_;
  }

  [[nodiscard]] bool gpu_profile_context_initialized() const noexcept {
    return gpu_profile_context_initialized_;
  }

  [[nodiscard]] std::vector<ChunkMeshUploadReceipt> drain_chunk_mesh_upload_receipts();

  void render() override;

  void begin_shutdown() override;

private:
  struct GpuMesh {
    GLuint vao{};
    GLuint vertex_buffer{};
    GLuint index_buffer{};
    GLsizeiptr vertex_capacity_bytes{};
    GLsizeiptr index_capacity_bytes{};
    GLsizei vertex_count{};
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

  static constexpr usize MaxEntityShaderBones = 128;

  struct EntityGpuModel {
    GLuint vao{};
    GLuint vertex_buffer{};
    GLuint index_buffer{};
    GLuint instance_buffer{};
    GLsizeiptr instance_capacity_bytes{};
    GLsizei index_count{};
  };

  struct EntityInstanceData {
    Vec3f position{};
    Quatf rotation{};
    f32 scale{1.0F};
  };

  void process_chunk_mesh_uploads();

  [[nodiscard]] bool upload_chunk_mesh_now(ChunkMeshUpload upload);

  [[nodiscard]] MeshID allocate_mesh_id() noexcept;

  [[nodiscard]] GpuMesh create_gpu_mesh(const CpuMeshBuffer& mesh);

  void upload_cpu_mesh_to_gpu(GpuMesh& gpu, const CpuMeshBuffer& mesh);

  static void destroy_gpu_mesh(GpuMesh& mesh);

  static void destroy_chunk_gpu_state(ChunkGpuState& state);

  static void destroy_gpu_texture(GpuTexture& texture);

  [[nodiscard]] static EntityGpuModel create_entity_gpu_model(const EntityModelAsset& model);

  static void destroy_entity_gpu_model(EntityGpuModel& model);

  [[nodiscard]] static GpuTexture create_texture(const TextureUploadData& upload, bool mipmaps);
  void note_texture_upload(const TextureUploadData& upload) noexcept;

  [[nodiscard]] static GLuint compile_shader(GLenum type, const char* source);

  [[nodiscard]] bool ensure_chunk_program();
  [[nodiscard]] bool ensure_entity_program();
  [[nodiscard]] bool ensure_ui_program();
  [[nodiscard]] bool ensure_sky_program();

  void draw_sky();
  void draw_chunk_list(const std::vector<ChunkDrawItem>& draw_items, bool transparent);
  void draw_entities();
  void draw_ui();
  void draw_ui_quads();
  void ensure_ui_buffers();
  void ensure_white_texture();

  u32 width_{};
  u32 height_{};
  bool viewport_set_{};
  u32 next_mesh_id_{1};
  u32 max_chunk_mesh_uploads_per_frame_{8};
  u32 mesh_uploads_last_frame_{};
  u32 mesh_upload_discards_last_frame_{};
  u32 draw_calls_last_frame_{};
  u32 chunk_draw_calls_last_frame_{};
  u32 ui_draw_calls_last_frame_{};
  u64 vertices_submitted_last_frame_{};
  u64 triangles_submitted_last_frame_{};
  u32 gpu_buffer_uploads_last_frame_{};
  u64 gpu_buffer_upload_bytes_last_frame_{};
  u32 texture_uploads_last_frame_{};
  u64 texture_upload_bytes_last_frame_{};
  u32 pending_texture_uploads_{};
  u64 pending_texture_upload_bytes_{};
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
  GLint chunk_animation_time_uniform_{-1};
  GLint chunk_alpha_uniform_{-1};
  GLint chunk_atlas_size_uniform_{-1};
  GLint chunk_terrain_atlas_uniform_{-1};
  GLint chunk_daylight_color_uniform_{-1};
  GLint chunk_daylight_strength_uniform_{-1};
  GLint chunk_ambient_color_uniform_{-1};
  GLuint entity_program_{};
  GLint entity_camera_position_uniform_{-1};
  GLint entity_camera_forward_uniform_{-1};
  GLint entity_camera_right_uniform_{-1};
  GLint entity_camera_up_uniform_{-1};
  GLint entity_camera_aspect_uniform_{-1};
  GLint entity_camera_fov_y_uniform_{-1};
  GLint entity_camera_near_uniform_{-1};
  GLint entity_camera_far_uniform_{-1};
  GLint entity_atlas_size_uniform_{-1};
  GLint entity_atlas_uniform_{-1};
  GLint entity_daylight_color_uniform_{-1};
  GLint entity_daylight_strength_uniform_{-1};
  GLint entity_ambient_color_uniform_{-1};
  GLint entity_bone_count_uniform_{-1};
  GLint entity_bone_pivots_uniform_{-1};
  GLint entity_bone_rotations_uniform_{-1};
  GLuint ui_program_{};
  GLint ui_viewport_uniform_{-1};
  GLint ui_texture_uniform_{-1};
  GLuint sky_program_{};
  GLuint sky_vao_{};
  GLint sky_camera_forward_uniform_{-1};
  GLint sky_camera_right_uniform_{-1};
  GLint sky_camera_up_uniform_{-1};
  GLint sky_camera_aspect_uniform_{-1};
  GLint sky_camera_fov_y_uniform_{-1};
  GLint sky_sun_direction_uniform_{-1};
  GLint sky_moon_direction_uniform_{-1};
  GLint sky_daylight_color_uniform_{-1};
  GLint sky_ambient_color_uniform_{-1};
  GLint sky_fog_color_uniform_{-1};
  GLint sky_daylight_strength_uniform_{-1};
  GLint sky_moonlight_strength_uniform_{-1};
  GLint sky_star_visibility_uniform_{-1};
  GLint sky_day_fraction_uniform_{-1};
  GLint sky_cloud_time_uniform_{-1};
  GLint sky_cirrus_amount_uniform_{-1};
  GLint sky_cumulus_amount_uniform_{-1};
  GLint sky_exposure_uniform_{-1};
  std::string chunk_vertex_shader_source_{};
  std::string chunk_fragment_shader_source_{};
  std::string entity_vertex_shader_source_{};
  std::string entity_fragment_shader_source_{};
  std::string ui_vertex_shader_source_{};
  std::string ui_fragment_shader_source_{};
  FramePacket frame_{};
  HashMap<ChunkMeshKey, ChunkGpuState> chunks_;
  HashMap<EntityModelID, EntityModelAsset> entity_models_;
  HashMap<EntityModelID, EntityGpuModel> entity_gpu_models_;
  std::deque<ChunkMeshUpload> pending_chunk_mesh_uploads_;
  std::vector<ChunkMeshUploadReceipt> chunk_mesh_upload_receipts_;
  GpuTexture terrain_atlas_{};
  GpuTexture entity_atlas_{};
  GpuTexture font_atlas_{};
  GpuTexture item_atlas_{};
  GpuTexture white_texture_{};
  GLuint ui_vao_{};
  GLuint ui_vertex_buffer_{};
  GLuint ui_index_buffer_{};
  GLsizeiptr ui_vertex_capacity_bytes_{};
  GLsizeiptr ui_index_capacity_bytes_{};
  bool gpu_profile_context_initialized_{};
  std::vector<const EntityDrawItem*> entity_draw_order_;
  std::vector<EntityInstanceData> entity_instances_;
  std::array<Vec3f, MaxEntityShaderBones> entity_bone_pivots_{};
  std::array<Vec3f, MaxEntityShaderBones> entity_bone_rotations_{};
  std::vector<UIVertex> ui_vertices_;
  std::vector<u32> ui_indices_;
};

} // namespace arv::render
