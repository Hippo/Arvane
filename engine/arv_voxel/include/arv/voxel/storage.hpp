#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/voxel/light.hpp"

#include <array>
#include <optional>
#include <utility>

namespace arv::voxel {

inline constexpr usize ChunkSide = static_cast<usize>(ChunkSize);
inline constexpr usize ChunkArea = ChunkSide * ChunkSide;
inline constexpr usize ChunkVolume = ChunkArea * ChunkSide;

enum class ChunkLifecycleState {
  Unloaded,
  Requested,
  Loading,
  Generating,
  Populating,
  Loaded,
  Active,
  MarkedForUnload,
  Saving,
};

enum class ChunkDirtyFlags : u32 {
  None = 0,
  NeedsSave = 1U << 0U,
  NeedsMesh = 1U << 1U,
  NeedsRelight = 1U << 2U,
  NeedsFluidStep = 1U << 3U,
};

enum class ChunkMeshState {
  Clean,
  Dirty,
  Queued,
  Building,
  WaitingForUpload,
  Ready,
  Failed,
};

[[nodiscard]] constexpr ChunkDirtyFlags operator|(ChunkDirtyFlags lhs, ChunkDirtyFlags rhs) noexcept {
  return static_cast<ChunkDirtyFlags>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
}

[[nodiscard]] constexpr ChunkDirtyFlags operator&(ChunkDirtyFlags lhs, ChunkDirtyFlags rhs) noexcept {
  return static_cast<ChunkDirtyFlags>(static_cast<u32>(lhs) & static_cast<u32>(rhs));
}

constexpr ChunkDirtyFlags& operator|=(ChunkDirtyFlags& lhs, ChunkDirtyFlags rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

constexpr ChunkDirtyFlags& operator&=(ChunkDirtyFlags& lhs, ChunkDirtyFlags rhs) noexcept {
  lhs = lhs & rhs;
  return lhs;
}

[[nodiscard]] constexpr bool any(ChunkDirtyFlags flags) noexcept {
  return static_cast<u32>(flags) != 0U;
}

[[nodiscard]] constexpr bool has_flag(ChunkDirtyFlags flags, ChunkDirtyFlags flag) noexcept {
  return any(flags & flag);
}

[[nodiscard]] constexpr bool is_valid_local(SectionLocalPos local) noexcept {
  return local.value.x >= 0 && local.value.x < ChunkSize &&
         local.value.y >= 0 && local.value.y < ChunkSize &&
         local.value.z >= 0 && local.value.z < ChunkSize;
}

[[nodiscard]] constexpr usize local_index(SectionLocalPos local) noexcept {
  return static_cast<usize>(local.value.x) +
         ChunkSide * (static_cast<usize>(local.value.y) + ChunkSide * static_cast<usize>(local.value.z));
}

[[nodiscard]] constexpr usize boundary_index_for_face(BlockFace face, SectionLocalPos local) noexcept {
  switch (face) {
  case BlockFace::PositiveX:
  case BlockFace::NegativeX:
    return static_cast<usize>(local.value.y) + ChunkSide * static_cast<usize>(local.value.z);
  case BlockFace::PositiveY:
  case BlockFace::NegativeY:
    return static_cast<usize>(local.value.x) + ChunkSide * static_cast<usize>(local.value.z);
  case BlockFace::PositiveZ:
  case BlockFace::NegativeZ:
    return static_cast<usize>(local.value.x) + ChunkSide * static_cast<usize>(local.value.y);
  }
  return 0;
}

[[nodiscard]] constexpr AxisDirection axis_direction_from_face(BlockFace face) noexcept {
  switch (face) {
  case BlockFace::PositiveX:
    return AxisDirection::PositiveX;
  case BlockFace::NegativeX:
    return AxisDirection::NegativeX;
  case BlockFace::PositiveY:
    return AxisDirection::PositiveY;
  case BlockFace::NegativeY:
    return AxisDirection::NegativeY;
  case BlockFace::PositiveZ:
    return AxisDirection::PositiveZ;
  case BlockFace::NegativeZ:
    return AxisDirection::NegativeZ;
  }
  return AxisDirection::PositiveX;
}

class ChunkSection {
public:
  explicit ChunkSection(ChunkCoord coord = {}) : coord_(coord) {
    blocks_.fill(AirBlockVariantID);
    lights_.fill(NoLight);
    fluids_.fill(empty_fluid_cell());
  }

  [[nodiscard]] ChunkCoord coord() const noexcept {
    return coord_;
  }

  [[nodiscard]] ChunkLifecycleState lifecycle_state() const noexcept {
    return lifecycle_state_;
  }

  void set_lifecycle_state(ChunkLifecycleState state) noexcept {
    lifecycle_state_ = state;
  }

  [[nodiscard]] u64 version() const noexcept {
    return version_;
  }

  [[nodiscard]] ChunkDirtyFlags dirty_flags() const noexcept {
    return dirty_flags_;
  }

  [[nodiscard]] ChunkMeshState mesh_state() const noexcept {
    return mesh_state_;
  }

  [[nodiscard]] bool is_dirty(ChunkDirtyFlags flag) const noexcept {
    return has_flag(dirty_flags_, flag);
  }

  [[nodiscard]] BlockVariantID block(SectionLocalPos local) const noexcept {
    return blocks_[local_index(local)];
  }

  [[nodiscard]] const std::array<BlockVariantID, ChunkVolume>& blocks() const noexcept {
    return blocks_;
  }

  [[nodiscard]] LightWord light(SectionLocalPos local) const noexcept {
    return lights_[local_index(local)];
  }

  [[nodiscard]] const std::array<LightWord, ChunkVolume>& lights() const noexcept {
    return lights_;
  }

  [[nodiscard]] FluidCell fluid(SectionLocalPos local) const noexcept {
    return fluids_[local_index(local)];
  }

  [[nodiscard]] const std::array<FluidCell, ChunkVolume>& fluids() const noexcept {
    return fluids_;
  }

  bool set_block(SectionLocalPos local, BlockVariantID variant) noexcept {
    if (!is_valid_local(local)) {
      return false;
    }

    BlockVariantID& cell = blocks_[local_index(local)];
    if (cell == variant) {
      return false;
    }

    cell = variant;
    ++version_;
    mark_dirty(
        ChunkDirtyFlags::NeedsSave | ChunkDirtyFlags::NeedsMesh |
        ChunkDirtyFlags::NeedsRelight | ChunkDirtyFlags::NeedsFluidStep);
    return true;
  }

  bool set_light(SectionLocalPos local, LightWord light) noexcept {
    if (!is_valid_local(local)) {
      return false;
    }

    LightWord& cell = lights_[local_index(local)];
    if (cell == light) {
      return false;
    }

    cell = light;
    ++version_;
    mark_dirty(ChunkDirtyFlags::NeedsSave | ChunkDirtyFlags::NeedsMesh);
    return true;
  }

  bool set_fluid(SectionLocalPos local, FluidCell fluid, bool affects_lighting = false) noexcept {
    if (!is_valid_local(local)) {
      return false;
    }

    FluidCell& cell = fluids_[local_index(local)];
    if (cell == fluid) {
      return false;
    }

    cell = fluid;
    ++version_;
    ChunkDirtyFlags flags = ChunkDirtyFlags::NeedsSave | ChunkDirtyFlags::NeedsMesh |
                            ChunkDirtyFlags::NeedsFluidStep;
    if (affects_lighting) {
      flags |= ChunkDirtyFlags::NeedsRelight;
    }
    mark_dirty(flags);
    return true;
  }

  void replace_generated_blocks(std::array<BlockVariantID, ChunkVolume> blocks) noexcept {
    blocks_ = std::move(blocks);
    lights_.fill(NoLight);
    fluids_.fill(empty_fluid_cell());
    ++version_;
    mark_dirty(ChunkDirtyFlags::NeedsMesh | ChunkDirtyFlags::NeedsRelight);
  }

  bool replace_lights(std::array<LightWord, ChunkVolume> lights) noexcept {
    if (lights_ == lights) {
      return false;
    }

    lights_ = std::move(lights);
    ++version_;
    mark_dirty(ChunkDirtyFlags::NeedsSave | ChunkDirtyFlags::NeedsMesh);
    return true;
  }

  void mark_dirty(ChunkDirtyFlags flags) noexcept {
    dirty_flags_ |= flags;
    if (has_flag(flags, ChunkDirtyFlags::NeedsMesh)) {
      mesh_state_ = ChunkMeshState::Dirty;
    }
  }

  void mark_mesh_dependency_dirty() noexcept {
    ++version_;
    mark_dirty(ChunkDirtyFlags::NeedsMesh);
  }

  void clear_dirty(ChunkDirtyFlags flags) noexcept {
    dirty_flags_ = static_cast<ChunkDirtyFlags>(static_cast<u32>(dirty_flags_) & ~static_cast<u32>(flags));
    if (!has_flag(dirty_flags_, ChunkDirtyFlags::NeedsMesh) && mesh_state_ == ChunkMeshState::Dirty) {
      mesh_state_ = ChunkMeshState::Clean;
    }
  }

  void mark_mesh_queued() noexcept {
    mesh_state_ = ChunkMeshState::Queued;
  }

  void mark_mesh_building() noexcept {
    mesh_state_ = ChunkMeshState::Building;
  }

  void mark_mesh_waiting_for_upload() noexcept {
    mesh_state_ = ChunkMeshState::WaitingForUpload;
  }

  void mark_mesh_ready(u64 published_version) noexcept {
    published_mesh_version_ = published_version;
    mesh_state_ = ChunkMeshState::Ready;
    clear_dirty(ChunkDirtyFlags::NeedsMesh);
  }

  [[nodiscard]] u64 published_mesh_version() const noexcept {
    return published_mesh_version_;
  }

private:
  ChunkCoord coord_{};
  ChunkLifecycleState lifecycle_state_{ChunkLifecycleState::Loaded};
  std::array<BlockVariantID, ChunkVolume> blocks_{};
  std::array<LightWord, ChunkVolume> lights_{};
  std::array<FluidCell, ChunkVolume> fluids_{};
  u64 version_{1};
  u64 published_mesh_version_{};
  ChunkDirtyFlags dirty_flags_{ChunkDirtyFlags::NeedsMesh};
  ChunkMeshState mesh_state_{ChunkMeshState::Dirty};
};

struct ChunkBoundarySnapshot {
  std::array<BlockVariantID, ChunkArea> blocks{};
  std::array<LightWord, ChunkArea> lights{};
  bool available{};
};

struct ChunkSnapshot {
  ChunkCoord coord{};
  u64 source_version{};
  std::array<BlockVariantID, ChunkVolume> blocks{};
  std::array<LightWord, ChunkVolume> lights{};
  std::array<ChunkBoundarySnapshot, BlockFaceCount> neighbors{};
};

[[nodiscard]] inline ChunkBoundarySnapshot air_boundary_snapshot() {
  ChunkBoundarySnapshot snapshot;
  snapshot.blocks.fill(AirBlockVariantID);
  snapshot.lights.fill(NoLight);
  snapshot.available = true;
  return snapshot;
}

[[nodiscard]] inline ChunkSnapshot make_chunk_snapshot_without_neighbors(const ChunkSection& chunk) {
  ChunkSnapshot snapshot;
  snapshot.coord = chunk.coord();
  snapshot.source_version = chunk.version();
  snapshot.blocks = chunk.blocks();
  snapshot.lights = chunk.lights();
  for (ChunkBoundarySnapshot& boundary : snapshot.neighbors) {
    boundary = air_boundary_snapshot();
  }
  return snapshot;
}

[[nodiscard]] inline BlockVariantID snapshot_block(
    const ChunkSnapshot& snapshot,
    SectionLocalPos local) noexcept {
  if (is_valid_local(local)) {
    return snapshot.blocks[local_index(local)];
  }

  if (local.value.x < 0) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::NegativeX)];
    return boundary.available
               ? boundary.blocks[boundary_index_for_face(BlockFace::NegativeX, {{0, local.value.y, local.value.z}})]
               : AirBlockVariantID;
  }
  if (local.value.x >= ChunkSize) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::PositiveX)];
    return boundary.available
               ? boundary.blocks[boundary_index_for_face(BlockFace::PositiveX, {{0, local.value.y, local.value.z}})]
               : AirBlockVariantID;
  }
  if (local.value.y < 0) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::NegativeY)];
    return boundary.available
               ? boundary.blocks[boundary_index_for_face(BlockFace::NegativeY, {{local.value.x, 0, local.value.z}})]
               : AirBlockVariantID;
  }
  if (local.value.y >= ChunkSize) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::PositiveY)];
    return boundary.available
               ? boundary.blocks[boundary_index_for_face(BlockFace::PositiveY, {{local.value.x, 0, local.value.z}})]
               : AirBlockVariantID;
  }
  if (local.value.z < 0) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::NegativeZ)];
    return boundary.available
               ? boundary.blocks[boundary_index_for_face(BlockFace::NegativeZ, {{local.value.x, local.value.y, 0}})]
               : AirBlockVariantID;
  }

  const auto& boundary = snapshot.neighbors[face_index(BlockFace::PositiveZ)];
  return boundary.available
             ? boundary.blocks[boundary_index_for_face(BlockFace::PositiveZ, {{local.value.x, local.value.y, 0}})]
             : AirBlockVariantID;
}

[[nodiscard]] inline LightWord snapshot_light(
    const ChunkSnapshot& snapshot,
    SectionLocalPos local) noexcept {
  if (is_valid_local(local)) {
    return snapshot.lights[local_index(local)];
  }

  if (local.value.x < 0) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::NegativeX)];
    return boundary.available
               ? boundary.lights[boundary_index_for_face(BlockFace::NegativeX, {{0, local.value.y, local.value.z}})]
               : NoLight;
  }
  if (local.value.x >= ChunkSize) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::PositiveX)];
    return boundary.available
               ? boundary.lights[boundary_index_for_face(BlockFace::PositiveX, {{0, local.value.y, local.value.z}})]
               : NoLight;
  }
  if (local.value.y < 0) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::NegativeY)];
    return boundary.available
               ? boundary.lights[boundary_index_for_face(BlockFace::NegativeY, {{local.value.x, 0, local.value.z}})]
               : NoLight;
  }
  if (local.value.y >= ChunkSize) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::PositiveY)];
    return boundary.available
               ? boundary.lights[boundary_index_for_face(BlockFace::PositiveY, {{local.value.x, 0, local.value.z}})]
               : NoLight;
  }
  if (local.value.z < 0) {
    const auto& boundary = snapshot.neighbors[face_index(BlockFace::NegativeZ)];
    return boundary.available
               ? boundary.lights[boundary_index_for_face(BlockFace::NegativeZ, {{local.value.x, local.value.y, 0}})]
               : NoLight;
  }

  const auto& boundary = snapshot.neighbors[face_index(BlockFace::PositiveZ)];
  return boundary.available
             ? boundary.lights[boundary_index_for_face(BlockFace::PositiveZ, {{local.value.x, local.value.y, 0}})]
             : NoLight;
}

} // namespace arv::voxel
