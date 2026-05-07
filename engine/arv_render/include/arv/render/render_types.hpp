#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/id.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace arv::render {

struct MeshTag {};
struct TextureTag {};
struct MaterialTag {};
struct ShaderTag {};
struct EntityModelTag {};

using MeshID = ID<MeshTag>;
using TextureID = ID<TextureTag>;
using MaterialID = ID<MaterialTag>;
using ShaderID = ID<ShaderTag>;
using EntityModelID = ID<EntityModelTag>;

struct FogParams {
  Vec3f color{0.55F, 0.68F, 0.82F};
  f32 density{};
  f32 start{};
  f32 end{};
};

struct SkyUniforms {
  bool enabled{};
  Vec3f sun_direction{0.0F, 1.0F, 0.0F};
  f32 daylight_strength{1.0F};
  Vec3f moon_direction{0.0F, -1.0F, 0.0F};
  f32 moonlight_strength{};
  Vec3f daylight_color{1.0F, 0.96F, 0.88F};
  f32 star_visibility{};
  Vec3f ambient_color{0.20F, 0.24F, 0.30F};
  f32 day_fraction{0.5F};
  Vec3f fog_color{0.55F, 0.68F, 0.86F};
  f32 cloud_time_seconds{};
  f32 cirrus_amount{0.35F};
  f32 cumulus_amount{0.60F};
  f32 exposure{1.0F};
};

struct RenderView {
  WorldID world_id{};
  Vec3f position{};
  Vec3f forward{0.0F, 0.0F, 1.0F};
  Vec3f right{1.0F, 0.0F, 0.0F};
  Vec3f up{0.0F, 1.0F, 0.0F};
  Quatf rotation{};
  Mat4f view{};
  Mat4f projection{};
  Mat4f view_projection{};
  f32 near_plane{0.05F};
  f32 far_plane{1000.0F};
  f32 fov_y_degrees{70.0F};
  u32 viewport_width{};
  u32 viewport_height{};
  FogParams fog{};
};

struct CameraSnapshot {
  WorldID world_id{};
  Vec3f position{};
  Vec3f forward{0.0F, 0.0F, 1.0F};
  Vec3f right{1.0F, 0.0F, 0.0F};
  Vec3f up{0.0F, 1.0F, 0.0F};
  Quatf rotation{};
  Mat4f view{};
  Mat4f projection{};
  Mat4f view_projection{};
  f32 near_plane{0.05F};
  f32 far_plane{1000.0F};
  f32 fov_y_degrees{70.0F};
  u32 viewport_width{};
  u32 viewport_height{};
  f32 partial_tick_alpha{};
};

struct ChunkVertex {
  u32 pos_ao{};
  u32 uv{};
  u32 light_face_material_hints{};
};

static_assert(sizeof(ChunkVertex) == 12);

enum class VertexMaterialClass : u8 {
  Opaque = 0,
  Cutout = 1,
  Transparent = 2,
  Water = 3,
  Lava = 4,
  Foliage = 5,

  Reserved6 = 6,
  Reserved7 = 7,
  Reserved8 = 8,
  Reserved9 = 9,
  Reserved10 = 10,
  Reserved11 = 11,
  Reserved12 = 12,
  Reserved13 = 13,
  Reserved14 = 14,

  Custom = 15,
};

enum class VertexAnimationId : u8 {
  None = 0,

  WaterStill = 1,
  WaterFlow = 2,
  WaterSurface = 3,

  LavaStill = 4,
  LavaFlow = 5,

  Reserved6 = 6,
  Reserved7 = 7,
  Reserved8 = 8,
  Reserved9 = 9,
  Reserved10 = 10,
  Reserved11 = 11,
  Reserved12 = 12,
  Reserved13 = 13,
  Reserved14 = 14,

  Custom = 15,
};

enum VertexMaterialFlags : u8 {
  VertexMaterialFlag_None = 0,
  VertexMaterialFlag_TintFromBiome = 1U << 0U,
  VertexMaterialFlag_TintFromFluid = 1U << 1U,
  VertexMaterialFlag_DoubleSided = 1U << 2U,
  VertexMaterialFlag_DisableAo = 1U << 3U,
  VertexMaterialFlag_Reserved4 = 1U << 4U,
};

[[nodiscard]] constexpr VertexMaterialFlags operator|(
    VertexMaterialFlags lhs,
    VertexMaterialFlags rhs) noexcept {
  return static_cast<VertexMaterialFlags>(static_cast<u8>(lhs) | static_cast<u8>(rhs));
}

[[nodiscard]] constexpr VertexMaterialFlags operator&(
    VertexMaterialFlags lhs,
    VertexMaterialFlags rhs) noexcept {
  return static_cast<VertexMaterialFlags>(static_cast<u8>(lhs) & static_cast<u8>(rhs));
}

constexpr VertexMaterialFlags& operator|=(
    VertexMaterialFlags& lhs,
    VertexMaterialFlags rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

[[nodiscard]] constexpr bool any(VertexMaterialFlags flags) noexcept {
  return static_cast<u8>(flags) != 0U;
}

[[nodiscard]] constexpr bool has_vertex_material_flag(
    VertexMaterialFlags flags,
    VertexMaterialFlags flag) noexcept {
  return any(flags & flag);
}

struct VertexMaterialHints {
  VertexMaterialClass material_class{VertexMaterialClass::Opaque};
  VertexAnimationId animation_id{VertexAnimationId::None};
  VertexMaterialFlags material_flags{VertexMaterialFlag_None};
};

inline constexpr u32 ChunkBlockLightRShift = 0U;
inline constexpr u32 ChunkBlockLightGShift = 4U;
inline constexpr u32 ChunkBlockLightBShift = 8U;
inline constexpr u32 ChunkSkyLightShift = 12U;
inline constexpr u32 ChunkFaceShift = 16U;
inline constexpr u32 ChunkMaterialClassShift = 19U;
inline constexpr u32 ChunkAnimationIdShift = 23U;
inline constexpr u32 ChunkMaterialFlagsShift = 27U;

inline constexpr u32 ChunkLightMask = 0x0FU;
inline constexpr u32 ChunkFaceMask = 0x07U;
inline constexpr u32 ChunkMaterialClassMask = 0x0FU;
inline constexpr u32 ChunkAnimationIdMask = 0x0FU;
inline constexpr u32 ChunkMaterialFlagsMask = 0x1FU;

[[nodiscard]] constexpr u32 pack_chunk_pos_ao(
    SectionLocalPos block,
    Vec3i model_position_sixteenths,
    u8 ao) noexcept {
  return (static_cast<u32>(block.value.x) & 0x1FU) |
         ((static_cast<u32>(block.value.y) & 0x1FU) << 5U) |
         ((static_cast<u32>(block.value.z) & 0x1FU) << 10U) |
         ((static_cast<u32>(model_position_sixteenths.x) & 0x1FU) << 15U) |
         ((static_cast<u32>(model_position_sixteenths.y) & 0x1FU) << 20U) |
         ((static_cast<u32>(model_position_sixteenths.z) & 0x1FU) << 25U) |
         ((static_cast<u32>(ao) & 0x03U) << 30U);
}

[[nodiscard]] constexpr u32 pack_chunk_uv(u16 atlas_u, u16 atlas_v) noexcept {
  return static_cast<u32>(atlas_u) | (static_cast<u32>(atlas_v) << 16U);
}

[[nodiscard]] constexpr u32 pack_chunk_light_face_material_hints(
    u8 block_light_r,
    u8 block_light_g,
    u8 block_light_b,
    u8 sky_light,
    voxel::BlockFace face,
    VertexMaterialHints material_hints = {}) noexcept {
  return ((static_cast<u32>(block_light_r) & ChunkLightMask) << ChunkBlockLightRShift) |
         ((static_cast<u32>(block_light_g) & ChunkLightMask) << ChunkBlockLightGShift) |
         ((static_cast<u32>(block_light_b) & ChunkLightMask) << ChunkBlockLightBShift) |
         ((static_cast<u32>(sky_light) & ChunkLightMask) << ChunkSkyLightShift) |
         ((static_cast<u32>(face) & ChunkFaceMask) << ChunkFaceShift) |
         ((static_cast<u32>(material_hints.material_class) & ChunkMaterialClassMask)
          << ChunkMaterialClassShift) |
         ((static_cast<u32>(material_hints.animation_id) & ChunkAnimationIdMask)
          << ChunkAnimationIdShift) |
         ((static_cast<u32>(material_hints.material_flags) & ChunkMaterialFlagsMask)
          << ChunkMaterialFlagsShift);
}

[[nodiscard]] constexpr u8 unpack_chunk_block_light_r(u32 packed) noexcept {
  return static_cast<u8>((packed >> ChunkBlockLightRShift) & ChunkLightMask);
}

[[nodiscard]] constexpr u8 unpack_chunk_block_light_g(u32 packed) noexcept {
  return static_cast<u8>((packed >> ChunkBlockLightGShift) & ChunkLightMask);
}

[[nodiscard]] constexpr u8 unpack_chunk_block_light_b(u32 packed) noexcept {
  return static_cast<u8>((packed >> ChunkBlockLightBShift) & ChunkLightMask);
}

[[nodiscard]] constexpr u8 unpack_chunk_sky_light(u32 packed) noexcept {
  return static_cast<u8>((packed >> ChunkSkyLightShift) & ChunkLightMask);
}

[[nodiscard]] constexpr voxel::BlockFace unpack_chunk_face(u32 packed) noexcept {
  return static_cast<voxel::BlockFace>((packed >> ChunkFaceShift) & ChunkFaceMask);
}

[[nodiscard]] constexpr VertexMaterialClass unpack_chunk_material_class(u32 packed) noexcept {
  return static_cast<VertexMaterialClass>(
      (packed >> ChunkMaterialClassShift) & ChunkMaterialClassMask);
}

[[nodiscard]] constexpr VertexAnimationId unpack_chunk_animation_id(u32 packed) noexcept {
  return static_cast<VertexAnimationId>((packed >> ChunkAnimationIdShift) & ChunkAnimationIdMask);
}

[[nodiscard]] constexpr VertexMaterialFlags unpack_chunk_material_flags(u32 packed) noexcept {
  return static_cast<VertexMaterialFlags>(
      (packed >> ChunkMaterialFlagsShift) & ChunkMaterialFlagsMask);
}

[[nodiscard]] constexpr ChunkVertex make_chunk_vertex(
    SectionLocalPos block,
    Vec3i model_position_sixteenths,
    u16 atlas_u,
    u16 atlas_v,
    voxel::BlockFace face,
    u8 ao = 0,
    u8 sky_light = 15,
    u8 block_light_r = 0,
    u8 block_light_g = 0,
    u8 block_light_b = 0,
    VertexMaterialHints material_hints = {}) noexcept {
  return {
      pack_chunk_pos_ao(block, model_position_sixteenths, ao),
      pack_chunk_uv(atlas_u, atlas_v),
      pack_chunk_light_face_material_hints(
          block_light_r,
          block_light_g,
          block_light_b,
          sky_light,
          face,
          material_hints)};
}

struct CpuMeshBuffer {
  std::vector<u8> vertex_bytes{};
  std::vector<u32> indices{};
};

struct UIVertex {
  Vec2f position{};
  Vec2f uv{};
  Vec4f color{1.0F, 1.0F, 1.0F, 1.0F};
  f32 texture_slot{};
};

struct TextureUploadData {
  u32 width{};
  u32 height{};
  std::vector<u8> rgba_pixels{};
  bool limit_mipmap_levels{};
  u32 max_mipmap_level{};

  [[nodiscard]] bool valid() const noexcept {
    return width > 0 && height > 0 &&
           rgba_pixels.size() == static_cast<usize>(width) * static_cast<usize>(height) * 4U;
  }
};

struct UIRectDrawItem {
  Vec2f min{};
  Vec2f max{};
  Vec4f color{1.0F, 1.0F, 1.0F, 1.0F};
};

struct UIGlyphDrawItem {
  Vec2f min{};
  Vec2f max{};
  Vec2f uv_min{};
  Vec2f uv_max{};
  Vec4f color{1.0F, 1.0F, 1.0F, 1.0F};
  u32 page{};
};

enum class UiTextureSource : u8 {
  White,
  FontAtlas,
  TerrainAtlas,
  ItemAtlas,
};

struct UITexturedQuadDrawItem {
  std::array<Vec2f, 4> positions{};
  std::array<Vec2f, 4> uvs{};
  Vec4f color{1.0F, 1.0F, 1.0F, 1.0F};
  UiTextureSource source{UiTextureSource::White};
  u32 page{};
};

void append_chunk_vertex(CpuMeshBuffer& buffer, ChunkVertex vertex);

[[nodiscard]] inline usize chunk_vertex_count(const CpuMeshBuffer& buffer) noexcept {
  return buffer.vertex_bytes.size() / sizeof(ChunkVertex);
}

[[nodiscard]] ChunkVertex chunk_vertex_at(const CpuMeshBuffer& buffer, usize index) noexcept;

struct ChunkMeshData {
  WorldID world_id{};
  ChunkSectionCoord coord{};
  u64 source_version{};
  u64 source_content_version{};
  u64 source_light_version{};
  CpuMeshBuffer opaque{};
  CpuMeshBuffer transparent{};
};

struct ChunkMeshUpload {
  ChunkMeshData mesh{};
};

struct ChunkMeshKey {
  WorldID world_id{};
  ChunkSectionCoord coord{};

  constexpr bool operator==(const ChunkMeshKey&) const = default;
};

struct ChunkMeshUploadReceipt {
  ChunkMeshKey key{};
  u64 source_version{};
  u64 source_content_version{};
  u64 source_light_version{};
  bool accepted{};
};

struct ChunkRenderState {
  MeshID opaque_mesh{};
  MeshID transparent_mesh{};
  u64 uploaded_version{};
  u64 uploaded_light_version{};
  bool resident{};
};

struct ChunkDrawItem {
  ChunkMeshKey key{};
  u64 expected_version{};
  u64 expected_light_version{};
};

struct EntityBoneRotationOverride {
  std::string bone_name{};
  Vec3f rotation_degrees{};

  bool operator==(const EntityBoneRotationOverride&) const = default;
};

struct EntityDrawItem {
  EntityModelID model_id{};
  Vec3f position{};
  Quatf rotation{};
  f32 scale{1.0F};
  f32 animation_time_seconds{};
  std::string animation_name{};
  std::vector<EntityBoneRotationOverride> bone_rotation_overrides{};
};

struct EntityVertex {
  Vec3f position{};
  Vec2f uv{};
  Vec3f color{1.0F, 1.0F, 1.0F};
};

struct FramePacket {
  CameraSnapshot camera{};
  RenderView world_view{};
  SkyUniforms sky{};
  std::vector<ChunkDrawItem> opaque_chunks{};
  std::vector<ChunkDrawItem> transparent_chunks{};
  std::vector<EntityDrawItem> entities{};
  std::vector<UIRectDrawItem> ui_rects{};
  std::vector<UITexturedQuadDrawItem> ui_textured_quads{};
  std::vector<UIGlyphDrawItem> ui_glyphs{};
};

[[nodiscard]] inline RenderView render_view_from_camera(const CameraSnapshot& camera) noexcept {
  RenderView view;
  view.world_id = camera.world_id;
  view.position = camera.position;
  view.forward = camera.forward;
  view.right = camera.right;
  view.up = camera.up;
  view.rotation = camera.rotation;
  view.view = camera.view;
  view.projection = camera.projection;
  view.view_projection = camera.view_projection;
  view.near_plane = camera.near_plane;
  view.far_plane = camera.far_plane;
  view.fov_y_degrees = camera.fov_y_degrees;
  view.viewport_width = camera.viewport_width;
  view.viewport_height = camera.viewport_height;
  return view;
}

[[nodiscard]] inline bool chunk_mesh_matches_live_version(
    const ChunkMeshData& mesh,
    u64 live_chunk_version) noexcept {
  return mesh.source_version == live_chunk_version;
}

[[nodiscard]] inline bool chunk_mesh_matches_live_versions(
    const ChunkMeshData& mesh,
    u64 live_block_version,
    u64 live_light_version) noexcept {
  return mesh.source_version == live_block_version &&
         mesh.source_light_version == live_light_version;
}

[[nodiscard]] inline bool chunk_mesh_upload_at_least_as_new(
    u64 lhs_block_version,
    u64 lhs_light_version,
    u64 rhs_block_version,
    u64 rhs_light_version) noexcept {
  return lhs_block_version > rhs_block_version ||
         (lhs_block_version == rhs_block_version && lhs_light_version >= rhs_light_version);
}

[[nodiscard]] inline bool should_accept_chunk_mesh_upload(
    const ChunkRenderState* current,
    const ChunkMeshUpload& upload) noexcept {
  return current == nullptr ||
         chunk_mesh_upload_at_least_as_new(
             upload.mesh.source_version,
             upload.mesh.source_light_version,
             current->uploaded_version,
             current->uploaded_light_version);
}

[[nodiscard]] inline bool should_draw_chunk_mesh(
    const ChunkRenderState* current,
    const ChunkDrawItem& item) noexcept {
  if (current == nullptr || !current->resident || current->uploaded_version == 0) {
    return false;
  }

  if (item.expected_version == 0) {
    return true;
  }

  if (current->uploaded_version == item.expected_version) {
    return true;
  }

  // Keep the last resident mesh visible while a fresher validated upload is
  // still queued for the GPU so block edits do not momentarily disappear.
  return true;
}

class IRenderer {
public:
  virtual ~IRenderer() = default;

  virtual void resize(u32 width, u32 height) = 0;
  virtual void enqueue_chunk_mesh_upload(ChunkMeshUpload upload) = 0;
  virtual void remove_chunk_mesh(ChunkMeshKey key) = 0;
  virtual void submit_frame(FramePacket packet) = 0;
  virtual void render() = 0;
  virtual void begin_shutdown() = 0;
};

} // namespace arv::render

template<>
struct std::hash<arv::render::ChunkMeshKey> {
  arv::usize operator()(const arv::render::ChunkMeshKey& key) const noexcept {
    const arv::usize x = std::hash<arv::WorldID>{}(key.world_id);
    const arv::usize y = std::hash<arv::i32>{}(key.coord.value.x);
    const arv::usize z = std::hash<arv::i32>{}(key.coord.value.y);
    const arv::usize w = std::hash<arv::i32>{}(key.coord.value.z);
    return (((x * 16777619U) ^ y) * 16777619U ^ z) * 16777619U ^ w;
  }
};
