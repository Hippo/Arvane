#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/voxel/light.hpp"

#include <array>
#include <memory>
#include <optional>

namespace arv::voxel {

inline constexpr usize ChunkSide = static_cast<usize>(ChunkSize);
inline constexpr usize ChunkArea = ChunkSide * ChunkSide;
inline constexpr usize ChunkVolume = ChunkArea * ChunkSide;
inline constexpr usize ChunkHaloSide = ChunkSide + 2U;
inline constexpr usize ChunkHaloVolume = ChunkHaloSide * ChunkHaloSide * ChunkHaloSide;
inline constexpr usize ChunkSnapshotNeighborhoodSide = 3U;
inline constexpr usize ChunkSnapshotNeighborhoodVolume =
    ChunkSnapshotNeighborhoodSide * ChunkSnapshotNeighborhoodSide * ChunkSnapshotNeighborhoodSide;

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

[[nodiscard]] constexpr usize snapshot_storage_index(i32 offset_x, i32 offset_y, i32 offset_z) noexcept {
  return static_cast<usize>(offset_x + 1) +
         ChunkSnapshotNeighborhoodSide * (static_cast<usize>(offset_y + 1) +
                                          ChunkSnapshotNeighborhoodSide * static_cast<usize>(offset_z + 1));
}

inline constexpr usize ChunkSnapshotCenterStorageIndex = snapshot_storage_index(0, 0, 0);

struct ChunkStorage {
  std::array<BlockVariantID, ChunkVolume> blocks{};
  std::array<LightWord, ChunkVolume> lights{};
  std::array<FluidCell, ChunkVolume> fluids{};
};

using ChunkStorageHandle = std::shared_ptr<ChunkStorage>;
using ChunkStorageView = std::shared_ptr<const ChunkStorage>;

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
    return storage_->blocks[local_index(local)];
  }

  [[nodiscard]] const std::array<BlockVariantID, ChunkVolume>& blocks() const noexcept {
    return storage_->blocks;
  }

  [[nodiscard]] LightWord light(SectionLocalPos local) const noexcept {
    return storage_->lights[local_index(local)];
  }

  [[nodiscard]] const std::array<LightWord, ChunkVolume>& lights() const noexcept {
    return storage_->lights;
  }

  [[nodiscard]] FluidCell fluid(SectionLocalPos local) const noexcept {
    return storage_->fluids[local_index(local)];
  }

  [[nodiscard]] const std::array<FluidCell, ChunkVolume>& fluids() const noexcept {
    return storage_->fluids;
  }

  [[nodiscard]] ChunkStorageView storage_view() const noexcept {
    return storage_;
  }

  [[nodiscard]] const ChunkStorage* storage_identity() const noexcept {
    return storage_.get();
  }

  bool set_block(SectionLocalPos local, BlockVariantID variant) noexcept;

  bool set_light(SectionLocalPos local, LightWord light) noexcept;

  bool set_fluid(SectionLocalPos local, FluidCell fluid, bool affects_lighting = false) noexcept;

  void replace_generated_blocks(std::array<BlockVariantID, ChunkVolume> blocks) noexcept;

  void replace_saved_contents(
      std::array<BlockVariantID, ChunkVolume> blocks,
      std::array<LightWord, ChunkVolume> lights) noexcept;

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
  void ensure_detached_storage();

  ChunkCoord coord_{};
  ChunkLifecycleState lifecycle_state_{ChunkLifecycleState::Loaded};
  ChunkStorageHandle storage_{};
  u64 version_{1};
  u64 published_mesh_version_{};
  ChunkDirtyFlags dirty_flags_{ChunkDirtyFlags::NeedsMesh};
  ChunkMeshState mesh_state_{ChunkMeshState::Dirty};
};

struct ChunkSnapshot {
  ChunkCoord coord{};
  u64 source_version{};

  std::array<ChunkStorageView, ChunkSnapshotNeighborhoodVolume> storages{};

  [[nodiscard]] const ChunkStorageView& center_storage() const noexcept {
    return storages[ChunkSnapshotCenterStorageIndex];
  }

  [[nodiscard]] const std::array<BlockVariantID, ChunkVolume>& blocks() const noexcept {
    return center_storage()->blocks;
  }

  [[nodiscard]] const std::array<LightWord, ChunkVolume>& lights() const noexcept {
    return center_storage()->lights;
  }

  [[nodiscard]] const ChunkStorage* center_storage_identity() const noexcept {
    return center_storage().get();
  }
};

[[nodiscard]] ChunkSnapshot make_chunk_snapshot_without_neighbors(const ChunkSection& chunk);

[[nodiscard]] BlockVariantID snapshot_block(
    const ChunkSnapshot& snapshot,
    SectionLocalPos local) noexcept;

[[nodiscard]] LightWord snapshot_light(
    const ChunkSnapshot& snapshot,
    SectionLocalPos local) noexcept;

} // namespace arv::voxel
