#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/voxel/light.hpp"

#include <array>
#include <optional>

namespace arv::voxel {

inline constexpr usize ChunkSide = static_cast<usize>(ChunkSize);
inline constexpr usize ChunkArea = ChunkSide * ChunkSide;
inline constexpr usize ChunkVolume = ChunkArea * ChunkSide;
inline constexpr usize ChunkHaloSide = ChunkSide + 2U;
inline constexpr usize ChunkHaloVolume = ChunkHaloSide * ChunkHaloSide * ChunkHaloSide;

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

[[nodiscard]] constexpr bool is_valid_halo_local(SectionLocalPos local) noexcept {
  return local.value.x >= -1 && local.value.x <= ChunkSize &&
         local.value.y >= -1 && local.value.y <= ChunkSize &&
         local.value.z >= -1 && local.value.z <= ChunkSize;
}

[[nodiscard]] constexpr usize halo_index(SectionLocalPos local) noexcept {
  return static_cast<usize>(local.value.x + 1) +
         ChunkHaloSide * (static_cast<usize>(local.value.y + 1) +
                          ChunkHaloSide * static_cast<usize>(local.value.z + 1));
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
  explicit ChunkSection(ChunkCoord coord = {});

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

  bool set_block(SectionLocalPos local, BlockVariantID variant) noexcept;

  bool set_light(SectionLocalPos local, LightWord light) noexcept;

  bool set_fluid(SectionLocalPos local, FluidCell fluid, bool affects_lighting = false) noexcept;

  void replace_generated_blocks(std::array<BlockVariantID, ChunkVolume> blocks) noexcept;

  bool replace_lights(std::array<LightWord, ChunkVolume> lights) noexcept;

  void mark_dirty(ChunkDirtyFlags flags) noexcept;

  void mark_mesh_dependency_dirty() noexcept;

  void clear_dirty(ChunkDirtyFlags flags) noexcept;

  void mark_mesh_queued() noexcept {
    mesh_state_ = ChunkMeshState::Queued;
  }

  void mark_mesh_building() noexcept {
    mesh_state_ = ChunkMeshState::Building;
  }

  void mark_mesh_waiting_for_upload() noexcept {
    mesh_state_ = ChunkMeshState::WaitingForUpload;
  }

  void mark_mesh_ready(u64 published_version) noexcept;

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
  std::array<BlockVariantID, ChunkHaloVolume> halo_blocks{};
  std::array<LightWord, ChunkHaloVolume> halo_lights{};
};

[[nodiscard]] ChunkBoundarySnapshot air_boundary_snapshot();

[[nodiscard]] ChunkSnapshot make_chunk_snapshot_without_neighbors(const ChunkSection& chunk);

[[nodiscard]] BlockVariantID snapshot_block(
    const ChunkSnapshot& snapshot,
    SectionLocalPos local) noexcept;

[[nodiscard]] LightWord snapshot_light(
    const ChunkSnapshot& snapshot,
    SectionLocalPos local) noexcept;

} // namespace arv::voxel
