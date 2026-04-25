#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/id.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"

#include <cstddef>
#include <cstring>
#include <functional>
#include <vector>

namespace arv::render {

struct MeshTag {};
struct TextureTag {};
struct MaterialTag {};
struct ShaderTag {};

using MeshID = ID<MeshTag>;
using TextureID = ID<TextureTag>;
using MaterialID = ID<MaterialTag>;
using ShaderID = ID<ShaderTag>;

struct FogParams {
  Vec3f color{0.55F, 0.68F, 0.82F};
  f32 density{};
  f32 start{};
  f32 end{};
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
  u32 light_face_flags{};
};

static_assert(sizeof(ChunkVertex) == 12);

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

[[nodiscard]] constexpr u32 pack_chunk_light_face_flags(
    u8 block_light_r,
    u8 block_light_g,
    u8 block_light_b,
    u8 sky_light,
    voxel::BlockFace face,
    u16 flags = 0) noexcept {
  return (static_cast<u32>(block_light_r) & 0x0FU) |
         ((static_cast<u32>(block_light_g) & 0x0FU) << 4U) |
         ((static_cast<u32>(block_light_b) & 0x0FU) << 8U) |
         ((static_cast<u32>(sky_light) & 0x0FU) << 12U) |
         ((static_cast<u32>(face) & 0x07U) << 16U) |
         ((static_cast<u32>(flags) & 0x1FFFU) << 19U);
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
    u8 block_light_b = 0) noexcept {
  return {
      pack_chunk_pos_ao(block, model_position_sixteenths, ao),
      pack_chunk_uv(atlas_u, atlas_v),
      pack_chunk_light_face_flags(block_light_r, block_light_g, block_light_b, sky_light, face)};
}

struct CpuMeshBuffer {
  std::vector<u8> vertex_bytes{};
  std::vector<u32> indices{};
};

inline void append_chunk_vertex(CpuMeshBuffer& buffer, ChunkVertex vertex) {
  const usize offset = buffer.vertex_bytes.size();
  buffer.vertex_bytes.resize(offset + sizeof(ChunkVertex));
  std::memcpy(buffer.vertex_bytes.data() + offset, &vertex, sizeof(ChunkVertex));
}

[[nodiscard]] inline usize chunk_vertex_count(const CpuMeshBuffer& buffer) noexcept {
  return buffer.vertex_bytes.size() / sizeof(ChunkVertex);
}

[[nodiscard]] inline ChunkVertex chunk_vertex_at(const CpuMeshBuffer& buffer, usize index) noexcept {
  ChunkVertex vertex;
  std::memcpy(&vertex, buffer.vertex_bytes.data() + index * sizeof(ChunkVertex), sizeof(ChunkVertex));
  return vertex;
}

struct ChunkMeshData {
  WorldID world_id{};
  ChunkCoord coord{};
  u64 source_version{};
  CpuMeshBuffer opaque{};
  CpuMeshBuffer transparent{};
};

struct ChunkMeshUpload {
  ChunkMeshData mesh{};
};

struct ChunkMeshKey {
  WorldID world_id{};
  ChunkCoord coord{};

  constexpr bool operator==(const ChunkMeshKey&) const = default;
};

struct ChunkRenderState {
  MeshID opaque_mesh{};
  MeshID transparent_mesh{};
  u64 uploaded_version{};
  bool resident{};
};

struct ChunkDrawItem {
  ChunkMeshKey key{};
  u64 expected_version{};
};

struct FramePacket {
  CameraSnapshot camera{};
  RenderView world_view{};
  std::vector<ChunkDrawItem> opaque_chunks{};
  std::vector<ChunkDrawItem> transparent_chunks{};
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

[[nodiscard]] inline bool should_accept_chunk_mesh_upload(
    const ChunkRenderState* current,
    const ChunkMeshUpload& upload) noexcept {
  return current == nullptr || upload.mesh.source_version >= current->uploaded_version;
}

class IRenderer {
public:
  virtual ~IRenderer() = default;

  virtual void resize(u32 width, u32 height) = 0;
  virtual void enqueue_chunk_mesh_upload(ChunkMeshUpload upload) = 0;
  virtual void submit_frame(FramePacket packet) = 0;
  virtual void render() = 0;
  virtual void begin_shutdown() = 0;
};

} // namespace arv::render

template<>
struct std::hash<arv::render::ChunkMeshKey> {
  std::size_t operator()(const arv::render::ChunkMeshKey& key) const noexcept {
    const std::size_t x = std::hash<arv::WorldID>{}(key.world_id);
    const std::size_t y = std::hash<arv::i32>{}(key.coord.value.x);
    const std::size_t z = std::hash<arv::i32>{}(key.coord.value.y);
    const std::size_t w = std::hash<arv::i32>{}(key.coord.value.z);
    return (((x * 16777619U) ^ y) * 16777619U ^ z) * 16777619U ^ w;
  }
};
