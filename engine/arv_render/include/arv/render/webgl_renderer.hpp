#pragma once

#include "arv/core/types.hpp"
#include "arv/render/entity_model.hpp"
#include "arv/render/render_types.hpp"

#include <span>
#include <optional>
#include <string>
#include <string_view>

namespace arv::render {

struct WebGLIndexedMesh {
  u32 vao{};
  u32 vbo{};
  u32 ebo{};
  i32 index_count{};
};

struct WebGLArrayMesh {
  u32 vao{};
  u32 vbo{};
  i32 vertex_count{};
};

struct WebGLColorVertex {
  Vec3f position{};
  Vec3f color{};
};

struct WebGLTextureArrayView {
  u32 texture{};
  u32 width{};
  u32 height{};
  u32 layers{};
};

struct WebGLUiRenderer {
  u32 program{};
  i32 viewport_location{-1};
  i32 atlas_location{-1};
  i32 mode_location{-1};
  u32 vao{};
  u32 vbo{};
};

struct WebGLSkyRenderer {
  u32 program{};
  u32 vao{};
  i32 camera_forward_location{-1};
  i32 camera_right_location{-1};
  i32 camera_up_location{-1};
  i32 camera_aspect_location{-1};
  i32 camera_fov_y_location{-1};
  i32 sun_direction_location{-1};
  i32 moon_direction_location{-1};
  i32 daylight_color_location{-1};
  i32 ambient_color_location{-1};
  i32 fog_color_location{-1};
  i32 daylight_strength_location{-1};
  i32 moonlight_strength_location{-1};
  i32 star_visibility_location{-1};
  i32 day_fraction_location{-1};
  i32 cloud_time_location{-1};
  i32 cirrus_amount_location{-1};
  i32 cumulus_amount_location{-1};
  i32 cloud_intensity_location{-1};
  i32 exposure_location{-1};
};

struct WebGLChunkRenderer {
  u32 program{};
  i32 mvp_location{-1};
  i32 origin_location{-1};
  i32 atlas_location{-1};
  i32 atlas_inv_size_location{-1};
  i32 alpha_location{-1};
  i32 animation_time_location{-1};
};

struct WebGLColorRenderer {
  u32 program{};
  i32 mvp_location{-1};
};

struct WebGLEntityRenderer {
  u32 program{};
  i32 mvp_location{-1};
  i32 position_location{-1};
  i32 rotation_location{-1};
  i32 scale_location{-1};
  i32 atlas_location{-1};
  i32 atlas_size_location{-1};
  i32 player_skin_atlas_location{-1};
  i32 player_skin_atlas_size_location{-1};
  i32 player_skin_region_location{-1};
  i32 player_skin_page_location{-1};
  i32 bone_count_location{-1};
  i32 bone_pivots_location{-1};
  i32 bone_rotations_location{-1};
  i32 bone_positions_location{-1};
};

struct WebGLParticleRenderer {
  u32 program{};
  u32 vao{};
  u32 vbo{};
  i32 camera_position_location{-1};
  i32 camera_forward_location{-1};
  i32 camera_right_location{-1};
  i32 camera_up_location{-1};
  i32 camera_aspect_location{-1};
  i32 camera_fov_y_location{-1};
  i32 camera_near_location{-1};
  i32 camera_far_location{-1};
  i32 atlas_size_location{-1};
  i32 atlas_location{-1};
};

struct WebGLAtlasRegion {
  u32 x{};
  u32 y{};
  u32 width{};
  u32 height{};
  u32 page{};
};

struct WebGLUiTextures {
  u32 terrain_texture{};
  u32 item_texture{};
  u32 font_texture{};
};

struct WebGLChunkSectionMesh {
  ChunkSectionCoord coord{};
  u64 content_version{};
  u64 light_version{};
  u64 dependency_signature{};
  WebGLIndexedMesh opaque{};
  WebGLIndexedMesh transparent{};
};

enum class WebGLBufferUsage : u8 {
  Static,
  Dynamic,
};

enum class WebGLChunkMeshLayer : u8 {
  Opaque,
  Transparent,
};

enum class WebGLTextureArraySampling : u8 {
  Nearest,
  NearestMipmap,
  Linear,
};

struct WebGLTextureArrayLimits {
  u32 max_texture_size{};
  u32 max_array_layers{};
};

struct WebGLTextureArrayUploadResult {
  u32 texture{};
  u32 width{};
  u32 height{};
  u32 layers{};
  std::string error{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return texture != 0U;
  }
};

struct WebGLTextureArrayRegionUploadResult {
  std::string error{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error.empty();
  }
};

void configure_default_webgl_state();
void clear_webgl_frame(f32 red, f32 green, f32 blue, f32 alpha);
void resize_webgl_viewport(i32 width, i32 height);
void delete_webgl_texture(u32 texture);
void destroy_webgl_indexed_mesh(WebGLIndexedMesh& mesh);
void destroy_webgl_array_mesh(WebGLArrayMesh& mesh);
void destroy_webgl_ui_renderer(WebGLUiRenderer& renderer);
void destroy_webgl_sky_renderer(WebGLSkyRenderer& renderer);
void destroy_webgl_particle_renderer(WebGLParticleRenderer& renderer);

[[nodiscard]] WebGLUiRenderer create_webgl_ui_renderer(u32 program);
[[nodiscard]] WebGLSkyRenderer create_webgl_sky_renderer(u32 program);
[[nodiscard]] WebGLChunkRenderer create_webgl_chunk_renderer(u32 program);
[[nodiscard]] WebGLColorRenderer create_webgl_color_renderer(u32 program);
[[nodiscard]] WebGLEntityRenderer create_webgl_entity_renderer(u32 program);
[[nodiscard]] WebGLParticleRenderer create_webgl_particle_renderer(u32 program);

void reset_webgl_draw_bindings();
void begin_webgl_translucent_pass();
void end_webgl_translucent_pass();

void draw_webgl_ui_frame(
    const FramePacket& frame,
    const WebGLUiRenderer& renderer,
    i32 viewport_width,
    i32 viewport_height,
    WebGLUiTextures textures);

void draw_webgl_sky(
    const WebGLSkyRenderer& renderer,
    const SkyUniforms& sky,
    Vec3f camera_forward,
    Vec3f camera_right,
    Vec3f camera_up,
    f32 camera_aspect,
    f32 camera_fov_y_degrees);

void draw_webgl_chunk_meshes(
    const WebGLChunkRenderer& renderer,
    std::span<const WebGLChunkSectionMesh> chunks,
    WebGLTextureArrayView atlas,
    const Mat4f& mvp,
    f32 animation_time_seconds,
    f32 alpha,
    WebGLChunkMeshLayer layer);

void draw_webgl_color_mesh(
    const WebGLColorRenderer& renderer,
    const WebGLArrayMesh& mesh,
    const Mat4f& mvp);

void draw_webgl_entity_models(
    const WebGLEntityRenderer& renderer,
    std::span<const EntityDrawItem> draws,
    const HashMap<EntityModelID, WebGLIndexedMesh>& gpu_models,
    std::span<const EntityModelAsset> models,
    std::span<const EntityAnimationAsset> animations,
    WebGLTextureArrayView entity_atlas,
    WebGLTextureArrayView player_skin_atlas,
    const Mat4f& mvp,
    Vec3f eye,
    bool translucent,
    FunctionRef<std::optional<WebGLAtlasRegion>(TextureRegionHandle)>
        resolve_player_skin_region);

[[nodiscard]] usize draw_webgl_particles(
    const WebGLParticleRenderer& renderer,
    std::span<const ParticleDrawItem> draws,
    WebGLTextureArrayView atlas,
    const RenderView& view,
    u32 viewport_width,
    u32 viewport_height,
    ParticleRenderBlendMode blend_mode);

[[nodiscard]] WebGLIndexedMesh upload_webgl_chunk_mesh(
    const CpuMeshBuffer& mesh);

[[nodiscard]] WebGLIndexedMesh upload_webgl_entity_model_mesh(
    const EntityModelAsset& model);

[[nodiscard]] WebGLArrayMesh upload_webgl_color_mesh(
    std::span<const WebGLColorVertex> vertices,
    WebGLBufferUsage usage);

[[nodiscard]] WebGLTextureArrayLimits query_webgl_texture_array_limits(
    u32 fallback_texture_size,
    u32 fallback_array_layers);

[[nodiscard]] WebGLTextureArrayUploadResult allocate_empty_webgl_texture_array(
    std::string_view label,
    u32 width,
    u32 height,
    u32 layers,
    WebGLTextureArraySampling sampling);

[[nodiscard]] WebGLTextureArrayUploadResult upload_webgl_texture_array(
    std::string_view label,
    const TextureArrayUploadData& upload,
    WebGLTextureArraySampling sampling);

[[nodiscard]] WebGLTextureArrayRegionUploadResult upload_webgl_texture_array_region(
    std::string_view label,
    u32 texture,
    u32 x,
    u32 y,
    u32 layer,
    u32 width,
    u32 height,
    std::span<const u8> rgba_pixels);

} // namespace arv::render
