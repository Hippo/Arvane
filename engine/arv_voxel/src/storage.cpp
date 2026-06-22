#include "arv/voxel/storage.hpp"

#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"

#include <utility>

namespace arv::voxel {
namespace {

[[nodiscard]] ChunkStorageHandle make_chunk_storage() {
  return std::make_shared<ChunkStorage>();
}

[[nodiscard]] ChunkStorageHandle clone_chunk_storage(const ChunkStorage& storage) {
  return std::make_shared<ChunkStorage>(storage);
}

[[nodiscard]] bool contains_fluid(
    const std::array<FluidCell, ChunkVolume>& fluids) noexcept {
  for (const FluidCell fluid : fluids) {
    if (!fluid.empty()) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] constexpr i32 wrapped_snapshot_local(i32 value) noexcept {
  return value < 0 ? ChunkSize - 1 : (value >= ChunkSize ? 0 : value);
}

struct SnapshotStorageLookup {
  const ChunkStorage* storage{};
  const ChunkSnapshotSkyLightOverlay* sky_light_overlay{};
  SectionLocalPos local{};
};

[[nodiscard]] SnapshotStorageLookup snapshot_storage_for_local(
    const ChunkSnapshotStoragePointers& snapshot,
    SectionLocalPos local) noexcept {
  const i32 offset_x = local.value.x < 0 ? -1 : (local.value.x >= ChunkSize ? 1 : 0);
  const i32 offset_y = local.value.y < 0 ? -1 : (local.value.y >= ChunkSize ? 1 : 0);
  const i32 offset_z = local.value.z < 0 ? -1 : (local.value.z >= ChunkSize ? 1 : 0);

  if ((offset_x != 0 || offset_y != 0 || offset_z != 0) && !is_valid_halo_local(local)) {
    return {};
  }

  const usize storage_index = snapshot_storage_index(offset_x, offset_y, offset_z);
  const ChunkStorage* storage = snapshot.storages[storage_index];
  if (!storage) {
    return {};
  }

  return {
      storage,
      snapshot.sky_light_overlays[storage_index],
      {{
          wrapped_snapshot_local(local.value.x),
          wrapped_snapshot_local(local.value.y),
          wrapped_snapshot_local(local.value.z),
      }}};
}

[[nodiscard]] LightWord apply_snapshot_sky_light_overlay(
    const SnapshotStorageLookup& lookup,
    LightWord light) noexcept {
  if (lookup.sky_light_overlay == nullptr ||
      light_sky(light) >= MaxLightLevel) {
    return light;
  }

  const SectionLocalPos local = lookup.local;
  const usize column_index =
      local_column_index({local.value.x, local.value.z});
  if (local.value.y < lookup.sky_light_overlay->min_open_sky_y[column_index]) {
    return light;
  }

  if (lookup.storage->blocks[local_index(local)] != AirBlockVariantID) {
    return light;
  }
  return with_sky_light(light, MaxLightLevel);
}

} // namespace

ChunkSection::ChunkSection(ChunkSectionCoord coord) : coord_(coord), storage_(make_chunk_storage()) {
}

void ChunkSection::ensure_detached_storage() {
  if (storage_.use_count() <= 1) {
    return;
  }

  core::record_snapshot_copy(sizeof(ChunkStorage));
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
  light_dependencies_stale_ = true;
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
  light_content_version_ = content_version_;
  mark_dirty(ChunkDirtyFlags::NeedsSave | ChunkDirtyFlags::LightMesh);
  return true;
}

bool ChunkSection::set_fluid(SectionLocalPos local, FluidCell fluid, bool affects_lighting) noexcept {
  if (!is_valid_local(local)) {
    return false;
  }

  const usize index = local_index(local);
  const bool had_fluid = !storage_->fluids[index].empty();
  if (storage_->fluids[index] == fluid) {
    return false;
  }

  ensure_detached_storage();
  storage_->fluids[index] = fluid;
  if (!fluid.empty()) {
    has_fluids_ = true;
  } else if (had_fluid) {
    has_fluids_ = contains_fluid(storage_->fluids);
  }
  ++block_version_;
  ++content_version_;
  ChunkDirtyFlags flags = ChunkDirtyFlags::NeedsSave | ChunkDirtyFlags::Geometry |
                          ChunkDirtyFlags::NeedsFluidStep;
  if (affects_lighting) {
    flags |= ChunkDirtyFlags::Lighting;
    light_dependencies_stale_ = true;
  }
  mark_dirty(flags);
  return true;
}

void ChunkSection::replace_generated_blocks(
    std::array<BlockVariantID, ChunkVolume> blocks,
    std::optional<std::array<FluidCell, ChunkVolume>> fluids) noexcept {
  if (storage_.use_count() <= 1) {
    storage_->blocks = std::move(blocks);
    storage_->lights.fill(NoLight);
    if (fluids.has_value()) {
      has_fluids_ = contains_fluid(fluids.value());
      storage_->fluids = std::move(fluids.value());
    } else {
      storage_->fluids.fill(empty_fluid_cell());
      has_fluids_ = false;
    }
  } else {
    ChunkStorageHandle storage = make_chunk_storage();
    storage->blocks = std::move(blocks);
    if (fluids.has_value()) {
      has_fluids_ = contains_fluid(fluids.value());
      storage->fluids = std::move(fluids.value());
    } else {
      has_fluids_ = false;
    }
    storage_ = std::move(storage);
  }
  ++block_version_;
  ++light_version_;
  ++light_data_version_;
  ++content_version_;
  light_dependencies_stale_ = true;
  mark_dirty(ChunkDirtyFlags::Geometry | ChunkDirtyFlags::Lighting);
}

void ChunkSection::replace_saved_contents(
    std::array<BlockVariantID, ChunkVolume> blocks,
    std::array<LightWord, ChunkVolume> lights,
    std::optional<std::array<FluidCell, ChunkVolume>> fluids,
    bool lights_match_content) noexcept {
  const bool blocks_changed = storage_->blocks != blocks;
  const bool lights_changed = storage_->lights != lights;
  const std::array<FluidCell, ChunkVolume>* next_fluids = fluids ? &fluids.value() : nullptr;
  const bool fluids_changed = next_fluids != nullptr && storage_->fluids != *next_fluids;
  const bool first_mesh = mesh_built_from_block_version_ == 0U;
  const bool next_has_fluids = fluids_changed ? contains_fluid(*next_fluids) : has_fluids_;

  if (blocks_changed || lights_changed || fluids_changed) {
    ensure_detached_storage();
    if (blocks_changed) {
      storage_->blocks = std::move(blocks);
      ++block_version_;
      ++content_version_;
    }
    if (fluids_changed) {
      storage_->fluids = std::move(fluids.value());
      has_fluids_ = next_has_fluids;
      ++block_version_;
      ++content_version_;
    } else if (blocks_changed && next_fluids == nullptr) {
      storage_->fluids.fill(empty_fluid_cell());
      has_fluids_ = false;
    }
    if (lights_changed) {
      storage_->lights = std::move(lights);
      ++light_version_;
      ++light_data_version_;
    }
  }
  if (lights_match_content) {
    light_content_version_ = content_version_;
    light_dependencies_stale_ = false;
  } else if (blocks_changed || fluids_changed || lights_changed) {
    light_dependencies_stale_ = true;
  }

  if (!blocks_changed && !lights_changed && !fluids_changed && !first_mesh) {
    return;
  }

  dirty_flags_ = ChunkDirtyFlags::None;
  if (blocks_changed || fluids_changed || first_mesh) {
    dirty_flags_ |= ChunkDirtyFlags::Geometry;
  }
  if (lights_changed && !blocks_changed && !fluids_changed) {
    dirty_flags_ |= ChunkDirtyFlags::LightMesh;
  }
  if (has_flag(dirty_flags_, ChunkDirtyFlags::Geometry) ||
      has_flag(dirty_flags_, ChunkDirtyFlags::LightMesh)) {
    mesh_state_ = ChunkMeshState::Dirty;
  } else if (mesh_state_ == ChunkMeshState::Dirty) {
    mesh_state_ = ChunkMeshState::Clean;
  }
  if (blocks_changed || fluids_changed) {
    mesh_built_from_block_version_ = 0;
    mesh_built_from_light_version_ = 0;
  }
}

void ChunkSection::replace_initial_saved_contents(
    std::array<BlockVariantID, ChunkVolume> blocks,
    std::optional<std::array<LightWord, ChunkVolume>> lights,
    std::optional<std::array<FluidCell, ChunkVolume>> fluids,
    bool lights_match_content) noexcept {
  ensure_detached_storage();
  storage_->blocks = std::move(blocks);
  if (lights.has_value()) {
    storage_->lights = std::move(lights.value());
  }
  if (fluids.has_value()) {
    has_fluids_ = contains_fluid(fluids.value());
    storage_->fluids = std::move(fluids.value());
  } else {
    has_fluids_ = false;
  }

  ++block_version_;
  ++content_version_;
  ++light_version_;
  ++light_data_version_;
  if (lights_match_content && lights.has_value()) {
    light_content_version_ = content_version_;
    light_dependencies_stale_ = false;
  } else {
    light_dependencies_stale_ = true;
  }

  dirty_flags_ = ChunkDirtyFlags::Geometry;
  mesh_state_ = ChunkMeshState::Dirty;
  mesh_built_from_block_version_ = 0;
  mesh_built_from_light_version_ = 0;
}

bool ChunkSection::replace_lights(std::array<LightWord, ChunkVolume> lights) noexcept {
  if (storage_->lights == lights) {
    light_content_version_ = content_version_;
    light_dependencies_stale_ = false;
    return false;
  }

  ensure_detached_storage();
  storage_->lights = std::move(lights);
  ++light_version_;
  ++light_data_version_;
  light_content_version_ = content_version_;
  light_dependencies_stale_ = false;
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

void ChunkSection::mark_lighting_dependency_dirty() noexcept {
  light_dependencies_stale_ = true;
  mark_dirty(ChunkDirtyFlags::Lighting);
}

void ChunkSection::clear_dirty(ChunkDirtyFlags flags) noexcept {
  dirty_flags_ = static_cast<ChunkDirtyFlags>(static_cast<u32>(dirty_flags_) & ~static_cast<u32>(flags));
  if (has_flag(flags, ChunkDirtyFlags::Lighting)) {
    light_dependencies_stale_ = false;
  }
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
  ARV_PROFILE_ZONE("Snapshot creation");
  std::array<char, core::PerfEventContextCapacity> profile_context{};
  if constexpr (core::perf_stats_enabled()) {
    core::format_chunk_section_perf_context(profile_context, chunk.coord());
  }
  const core::ScopedThreadTiming timing{
      core::PerfMetric::SnapshotCreation,
      "world",
      profile_context.data()};
  core::record_snapshot_created();
  core::record_snapshot_reused();
  ChunkSnapshot snapshot;
  snapshot.coord = chunk.coord();
  snapshot.source_version = chunk.block_version();
  snapshot.source_content_version = chunk.content_version();
  snapshot.source_light_version = chunk.light_version();
  snapshot.storages[ChunkSnapshotCenterStorageIndex] = chunk.storage_view();
  return snapshot;
}

ChunkSnapshotStoragePointers make_chunk_snapshot_storage_pointers(
    const ChunkSnapshot& snapshot) noexcept {
  ChunkSnapshotStoragePointers pointers;
  for (usize index = 0; index < pointers.storages.size(); ++index) {
    pointers.storages[index] = snapshot.storages[index].get();
    pointers.sky_light_overlays[index] = snapshot.sky_light_overlays[index].get();
  }
  return pointers;
}

BlockVariantID snapshot_block(const ChunkSnapshot& snapshot, SectionLocalPos local) noexcept {
  return snapshot_block(make_chunk_snapshot_storage_pointers(snapshot), local);
}

BlockVariantID snapshot_block(
    const ChunkSnapshotStoragePointers& snapshot,
    SectionLocalPos local) noexcept {
  const SnapshotStorageLookup lookup = snapshot_storage_for_local(snapshot, local);
  return lookup.storage != nullptr ? lookup.storage->blocks[local_index(lookup.local)] : AirBlockVariantID;
}

LightWord snapshot_light(const ChunkSnapshot& snapshot, SectionLocalPos local) noexcept {
  return snapshot_light(make_chunk_snapshot_storage_pointers(snapshot), local);
}

LightWord snapshot_light(
    const ChunkSnapshotStoragePointers& snapshot,
    SectionLocalPos local) noexcept {
  const SnapshotStorageLookup lookup = snapshot_storage_for_local(snapshot, local);
  if (lookup.storage == nullptr) {
    return NoLight;
  }
  const LightWord light = lookup.storage->lights[local_index(lookup.local)];
  return apply_snapshot_sky_light_overlay(lookup, light);
}

FluidCell snapshot_fluid(const ChunkSnapshot& snapshot, SectionLocalPos local) noexcept {
  return snapshot_fluid(make_chunk_snapshot_storage_pointers(snapshot), local);
}

FluidCell snapshot_fluid(
    const ChunkSnapshotStoragePointers& snapshot,
    SectionLocalPos local) noexcept {
  const SnapshotStorageLookup lookup = snapshot_storage_for_local(snapshot, local);
  return lookup.storage != nullptr ? lookup.storage->fluids[local_index(lookup.local)] : empty_fluid_cell();
}

} // namespace arv::voxel
