#include "arv/voxel/storage.hpp"

#include <utility>

namespace arv::voxel {
namespace {

[[nodiscard]] ChunkStorageHandle make_chunk_storage() {
  ChunkStorageHandle storage = std::make_shared<ChunkStorage>();
  storage->blocks.fill(AirBlockVariantID);
  storage->lights.fill(NoLight);
  storage->fluids.fill(empty_fluid_cell());
  return storage;
}

[[nodiscard]] ChunkStorageHandle clone_chunk_storage(const ChunkStorage& storage) {
  return std::make_shared<ChunkStorage>(storage);
}

[[nodiscard]] constexpr i32 wrapped_snapshot_local(i32 value) noexcept {
  return value < 0 ? ChunkSize - 1 : (value >= ChunkSize ? 0 : value);
}

[[nodiscard]] const ChunkStorage* snapshot_storage_for_local(
    const ChunkSnapshot& snapshot,
    SectionLocalPos local,
    SectionLocalPos& storage_local) noexcept {
  const i32 offset_x = local.value.x < 0 ? -1 : (local.value.x >= ChunkSize ? 1 : 0);
  const i32 offset_y = local.value.y < 0 ? -1 : (local.value.y >= ChunkSize ? 1 : 0);
  const i32 offset_z = local.value.z < 0 ? -1 : (local.value.z >= ChunkSize ? 1 : 0);

  if ((offset_x != 0 || offset_y != 0 || offset_z != 0) && !is_valid_halo_local(local)) {
    return nullptr;
  }

  const ChunkStorageView& storage =
      snapshot.storages[snapshot_storage_index(offset_x, offset_y, offset_z)];
  if (!storage) {
    return nullptr;
  }

  storage_local = {{
      wrapped_snapshot_local(local.value.x),
      wrapped_snapshot_local(local.value.y),
      wrapped_snapshot_local(local.value.z),
  }};
  return storage.get();
}

} // namespace

ChunkSection::ChunkSection(ChunkCoord coord) : coord_(coord), storage_(make_chunk_storage()) {
}

void ChunkSection::ensure_detached_storage() {
  if (storage_.use_count() <= 1) {
    return;
  }

  storage_ = clone_chunk_storage(*storage_);
}

bool ChunkSection::set_block(SectionLocalPos local, BlockVariantID variant) noexcept {
  if (!is_valid_local(local)) {
    return false;
  }

  const usize index = local_index(local);
  if (storage_->blocks[index] == variant) {
    return false;
  }

  ensure_detached_storage();
  storage_->blocks[index] = variant;
  ++block_version_;
  ++content_version_;
  mark_dirty(
      ChunkDirtyFlags::NeedsSave | ChunkDirtyFlags::Geometry |
      ChunkDirtyFlags::Lighting | ChunkDirtyFlags::NeedsFluidStep);
  return true;
}

bool ChunkSection::set_light(SectionLocalPos local, LightWord light) noexcept {
  if (!is_valid_local(local)) {
    return false;
  }

  const usize index = local_index(local);
  if (storage_->lights[index] == light) {
    return false;
  }

  ensure_detached_storage();
  storage_->lights[index] = light;
  ++light_version_;
  ++light_data_version_;
  mark_dirty(ChunkDirtyFlags::NeedsSave | ChunkDirtyFlags::LightMesh);
  return true;
}

bool ChunkSection::set_fluid(SectionLocalPos local, FluidCell fluid, bool affects_lighting) noexcept {
  if (!is_valid_local(local)) {
    return false;
  }

  const usize index = local_index(local);
  if (storage_->fluids[index] == fluid) {
    return false;
  }

  ensure_detached_storage();
  storage_->fluids[index] = fluid;
  ++block_version_;
  ++content_version_;
  ChunkDirtyFlags flags = ChunkDirtyFlags::NeedsSave | ChunkDirtyFlags::Geometry |
                          ChunkDirtyFlags::NeedsFluidStep;
  if (affects_lighting) {
    flags |= ChunkDirtyFlags::Lighting;
  }
  mark_dirty(flags);
  return true;
}

void ChunkSection::replace_generated_blocks(std::array<BlockVariantID, ChunkVolume> blocks) noexcept {
  if (storage_.use_count() <= 1) {
    storage_->blocks = std::move(blocks);
    storage_->lights.fill(NoLight);
    storage_->fluids.fill(empty_fluid_cell());
  } else {
    ChunkStorageHandle storage = make_chunk_storage();
    storage->blocks = std::move(blocks);
    storage_ = std::move(storage);
  }
  ++block_version_;
  ++light_version_;
  ++light_data_version_;
  ++content_version_;
  mark_dirty(ChunkDirtyFlags::Geometry | ChunkDirtyFlags::Lighting);
}

void ChunkSection::replace_saved_contents(
    std::array<BlockVariantID, ChunkVolume> blocks,
    std::array<LightWord, ChunkVolume> lights) noexcept {
  const bool blocks_changed = storage_->blocks != blocks;
  const bool lights_changed = storage_->lights != lights;
  const bool first_mesh = mesh_built_from_block_version_ == 0U;

  if (blocks_changed || lights_changed) {
    ensure_detached_storage();
    if (blocks_changed) {
      storage_->blocks = std::move(blocks);
      storage_->fluids.fill(empty_fluid_cell());
      ++block_version_;
      ++content_version_;
    }
    if (lights_changed) {
      storage_->lights = std::move(lights);
      ++light_version_;
      ++light_data_version_;
    }
  }

  if (!blocks_changed && !lights_changed && !first_mesh) {
    return;
  }

  dirty_flags_ = ChunkDirtyFlags::None;
  if (blocks_changed || first_mesh) {
    dirty_flags_ |= ChunkDirtyFlags::Geometry;
  }
  if (lights_changed && !blocks_changed) {
    dirty_flags_ |= ChunkDirtyFlags::LightMesh;
  }
  if (has_flag(dirty_flags_, ChunkDirtyFlags::Geometry) ||
      has_flag(dirty_flags_, ChunkDirtyFlags::LightMesh)) {
    mesh_state_ = ChunkMeshState::Dirty;
  } else if (mesh_state_ == ChunkMeshState::Dirty) {
    mesh_state_ = ChunkMeshState::Clean;
  }
  if (blocks_changed) {
    mesh_built_from_block_version_ = 0;
    mesh_built_from_light_version_ = 0;
  }
}

bool ChunkSection::replace_lights(std::array<LightWord, ChunkVolume> lights) noexcept {
  if (storage_->lights == lights) {
    return false;
  }

  ensure_detached_storage();
  storage_->lights = std::move(lights);
  ++light_version_;
  ++light_data_version_;
  mark_dirty(ChunkDirtyFlags::NeedsSave | ChunkDirtyFlags::LightMesh);
  return true;
}

void ChunkSection::mark_dirty(ChunkDirtyFlags flags) noexcept {
  dirty_flags_ |= flags;
  if (has_flag(flags, ChunkDirtyFlags::Geometry) ||
      has_flag(flags, ChunkDirtyFlags::LightMesh)) {
    mesh_state_ = ChunkMeshState::Dirty;
  }
}

void ChunkSection::mark_mesh_dependency_dirty() noexcept {
  ++block_version_;
  ++content_version_;
  mark_dirty(ChunkDirtyFlags::Geometry);
}

void ChunkSection::mark_light_dependency_dirty() noexcept {
  ++light_version_;
  mark_dirty(ChunkDirtyFlags::LightMesh);
}

void ChunkSection::clear_dirty(ChunkDirtyFlags flags) noexcept {
  dirty_flags_ = static_cast<ChunkDirtyFlags>(static_cast<u32>(dirty_flags_) & ~static_cast<u32>(flags));
  if (!has_flag(dirty_flags_, ChunkDirtyFlags::Geometry) &&
      !has_flag(dirty_flags_, ChunkDirtyFlags::LightMesh) &&
      mesh_state_ == ChunkMeshState::Dirty) {
    mesh_state_ = ChunkMeshState::Clean;
  }
}

void ChunkSection::mark_mesh_ready(u64 source_block_version, u64 source_light_version) noexcept {
  mesh_built_from_block_version_ = source_block_version;
  mesh_built_from_light_version_ = source_light_version;
  if (source_block_version == block_version_) {
    clear_dirty(ChunkDirtyFlags::Geometry);
  }
  if (source_light_version == light_version_) {
    clear_dirty(ChunkDirtyFlags::LightMesh);
  }
  mesh_state_ =
      (has_flag(dirty_flags_, ChunkDirtyFlags::Geometry) ||
       has_flag(dirty_flags_, ChunkDirtyFlags::LightMesh))
          ? ChunkMeshState::Dirty
          : ChunkMeshState::Ready;
}

ChunkSnapshot make_chunk_snapshot_without_neighbors(const ChunkSection& chunk) {
  ChunkSnapshot snapshot;
  snapshot.coord = chunk.coord();
  snapshot.source_version = chunk.block_version();
  snapshot.source_content_version = chunk.content_version();
  snapshot.source_light_version = chunk.light_version();
  snapshot.storages[ChunkSnapshotCenterStorageIndex] = chunk.storage_view();
  return snapshot;
}

BlockVariantID snapshot_block(const ChunkSnapshot& snapshot, SectionLocalPos local) noexcept {
  SectionLocalPos storage_local{};
  const ChunkStorage* storage = snapshot_storage_for_local(snapshot, local, storage_local);
  return storage != nullptr ? storage->blocks[local_index(storage_local)] : AirBlockVariantID;
}

LightWord snapshot_light(const ChunkSnapshot& snapshot, SectionLocalPos local) noexcept {
  SectionLocalPos storage_local{};
  const ChunkStorage* storage = snapshot_storage_for_local(snapshot, local, storage_local);
  return storage != nullptr ? storage->lights[local_index(storage_local)] : NoLight;
}

} // namespace arv::voxel
