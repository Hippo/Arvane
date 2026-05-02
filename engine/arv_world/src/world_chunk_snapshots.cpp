#include "world_runtime_internal.hpp"

#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"

#include <cstdio>
#include <utility>

namespace arv::world {
namespace {

using DependencyMask = std::array<bool, voxel::ChunkSnapshotNeighborhoodVolume>;

void add_dependency_offset(DependencyMask& mask, i32 x, i32 y, i32 z) noexcept {
  if (x == 0 && y == 0 && z == 0) {
    return;
  }
  mask[voxel::snapshot_storage_index(x, y, z)] = true;
}

void add_all_neighbor_dependency_offsets(DependencyMask& mask) noexcept {
  for (i32 z = -1; z <= 1; ++z) {
    for (i32 y = -1; y <= 1; ++y) {
      for (i32 x = -1; x <= 1; ++x) {
        add_dependency_offset(mask, x, y, z);
      }
    }
  }
}

void add_block_dependency_offsets(DependencyMask& mask, SectionLocalPos local) noexcept {
  const i32 min_x = local.value.x == 0 ? -1 : 0;
  const i32 max_x = local.value.x == ChunkSize - 1 ? 1 : 0;
  const i32 min_y = local.value.y == 0 ? -1 : 0;
  const i32 max_y = local.value.y == ChunkSize - 1 ? 1 : 0;
  const i32 min_z = local.value.z == 0 ? -1 : 0;
  const i32 max_z = local.value.z == ChunkSize - 1 ? 1 : 0;

  for (i32 z = min_z; z <= max_z; ++z) {
    for (i32 y = min_y; y <= max_y; ++y) {
      for (i32 x = min_x; x <= max_x; ++x) {
        add_dependency_offset(mask, x, y, z);
      }
    }
  }
}

void add_light_dependency_offsets(DependencyMask& mask, SectionLocalPos local) noexcept {
  if (local.value.x == 0) {
    add_dependency_offset(mask, -1, 0, 0);
  }
  if (local.value.x == ChunkSize - 1) {
    add_dependency_offset(mask, 1, 0, 0);
  }
  if (local.value.y == 0) {
    add_dependency_offset(mask, 0, -1, 0);
  }
  if (local.value.y == ChunkSize - 1) {
    add_dependency_offset(mask, 0, 1, 0);
  }
  if (local.value.z == 0) {
    add_dependency_offset(mask, 0, 0, -1);
  }
  if (local.value.z == ChunkSize - 1) {
    add_dependency_offset(mask, 0, 0, 1);
  }
}

void format_chunk_context(
    std::array<char, core::PerfEventContextCapacity>& context,
    ChunkCoord coord) noexcept {
  (void)std::snprintf(
      context.data(),
      context.size(),
      "section=(%d,%d,%d)",
      coord.value.x,
      coord.value.y,
      coord.value.z);
}

} // namespace

std::optional<voxel::ChunkSnapshot> WorldRuntime::get_chunk_snapshot(ChunkCoord coord) const {
  const voxel::ChunkSection* chunk = find_chunk(coord);
  if (chunk == nullptr) {
    return std::nullopt;
  }

  switch (chunk->lifecycle_state()) {
    case voxel::ChunkLifecycleState::Loaded:
    case voxel::ChunkLifecycleState::Active:
      return voxel::make_chunk_snapshot_without_neighbors(*chunk);
    case voxel::ChunkLifecycleState::Unloaded:
    case voxel::ChunkLifecycleState::Requested:
    case voxel::ChunkLifecycleState::Loading:
    case voxel::ChunkLifecycleState::Generating:
    case voxel::ChunkLifecycleState::Populating:
    case voxel::ChunkLifecycleState::MarkedForUnload:
    case voxel::ChunkLifecycleState::Saving:
      return std::nullopt;
  }

  return std::nullopt;
}

void WorldRuntime::apply_chunk_snapshot(
    ChunkCoord coord,
    std::array<voxel::BlockVariantID, voxel::ChunkVolume> blocks,
    std::optional<std::array<voxel::LightWord, voxel::ChunkVolume>> lights,
    std::optional<std::array<voxel::FluidCell, voxel::ChunkVolume>> fluids,
    bool lighting_authoritative) noexcept {
  const voxel::ChunkSection* existing_chunk = find_chunk(coord);
  const bool first_mesh =
      existing_chunk == nullptr || existing_chunk->mesh_built_from_block_version() == 0U;
  const bool needs_first_mesh_dirty =
      first_mesh &&
      (existing_chunk == nullptr ||
       !existing_chunk->is_dirty(voxel::ChunkDirtyFlags::Geometry));

  DependencyMask block_dependency_offsets{};
  DependencyMask light_dependency_offsets{};
  bool blocks_changed = existing_chunk == nullptr;
  bool lights_changed = existing_chunk == nullptr;
  bool fluids_changed = fluids.has_value() && existing_chunk == nullptr;
  const bool has_lighting_payload = lights.has_value();

  std::array<voxel::LightWord, voxel::ChunkVolume> resolved_lights;
  if (has_lighting_payload) {
    resolved_lights = std::move(lights.value());
  } else if (existing_chunk != nullptr) {
    resolved_lights = existing_chunk->lights();
  } else {
    resolved_lights.fill(voxel::NoLight);
  }

  if (existing_chunk != nullptr) {
    blocks_changed = existing_chunk->blocks() != blocks;
    lights_changed = existing_chunk->lights() != resolved_lights;
    fluids_changed = fluids.has_value() && existing_chunk->fluids() != fluids.value();

    if (blocks_changed || fluids_changed) {
      for (usize index = 0; index < voxel::ChunkVolume; ++index) {
        if (existing_chunk->blocks()[index] != blocks[index] ||
            (fluids.has_value() && existing_chunk->fluids()[index] != fluids.value()[index])) {
          add_block_dependency_offsets(block_dependency_offsets, detail::local_from_index(index));
        }
      }
    }
    if (lights_changed) {
      for (usize index = 0; index < voxel::ChunkVolume; ++index) {
        if (existing_chunk->lights()[index] != resolved_lights[index]) {
          add_light_dependency_offsets(light_dependency_offsets, detail::local_from_index(index));
        }
      }
    }
  } else {
    add_all_neighbor_dependency_offsets(block_dependency_offsets);
    add_all_neighbor_dependency_offsets(light_dependency_offsets);
  }

  voxel::ChunkSection& chunk =
      impl_->chunks.ensure_chunk(coord, voxel::ChunkLifecycleState::Loaded, impl_->dirty);
  impl_->chunks.set_lifecycle_state_internal(chunk, voxel::ChunkLifecycleState::Loaded);
  chunk.replace_saved_contents(
      std::move(blocks),
      std::move(resolved_lights),
      std::move(fluids));
  if (has_lighting_payload && lighting_authoritative) {
    chunk.clear_dirty(voxel::ChunkDirtyFlags::Lighting);
  } else {
    chunk.mark_dirty(voxel::ChunkDirtyFlags::Lighting);
  }

  if (needs_first_mesh_dirty || blocks_changed || lights_changed || fluids_changed) {
    impl_->dirty.mark_mesh_content_dirty();
    impl_->dirty.enqueue_dirty_mesh_chunk(coord);
  }
  if (blocks_changed || fluids_changed || needs_first_mesh_dirty) {
    for (i32 z = -1; z <= 1; ++z) {
      for (i32 y = -1; y <= 1; ++y) {
        for (i32 x = -1; x <= 1; ++x) {
          if (!block_dependency_offsets[voxel::snapshot_storage_index(x, y, z)]) {
            continue;
          }
          impl_->dirty.mark_neighbor_mesh_only_dependency_dirty(
              *this,
              {{
                  coord.value.x + x,
                  coord.value.y + y,
                  coord.value.z + z,
              }});
        }
      }
    }
  }
  if (lights_changed) {
    for (const Vec3i& offset : detail::NeighborOffsets) {
      if (!light_dependency_offsets[voxel::snapshot_storage_index(offset.x, offset.y, offset.z)]) {
        continue;
      }
      impl_->dirty.mark_neighbor_light_dependency_dirty(
          *this,
          {{
              coord.value.x + offset.x,
              coord.value.y + offset.y,
              coord.value.z + offset.z,
          }});
    }
  }
}

std::optional<voxel::ChunkSnapshot> WorldRuntime::make_meshing_snapshot(
    ChunkCoord coord,
    MissingNeighborPolicy missing_neighbor_policy) const {
  ARV_PROFILE_ZONE("Snapshot creation");
  std::array<char, core::PerfEventContextCapacity> profile_context{};
  if constexpr (core::perf_stats_enabled()) {
    format_chunk_context(profile_context, coord);
  }
  const core::ScopedThreadTiming snapshot_timing{
      core::PerfMetric::SnapshotCreation,
      "world",
      profile_context.data()};
  const voxel::ChunkSection* chunk = find_chunk(coord);
  if (chunk == nullptr) {
    return std::nullopt;
  }

  core::record_snapshot_created();
  core::record_snapshot_reused();
  voxel::ChunkSnapshot snapshot;
  snapshot.coord = chunk->coord();
  snapshot.source_version = chunk->block_version();
  snapshot.source_content_version = chunk->content_version();
  snapshot.source_light_version = chunk->light_version();
  snapshot.storages[voxel::ChunkSnapshotCenterStorageIndex] = chunk->storage_view();
  // TODO(arvane): Narrow meshing neighbor inputs to compact immutable boundary/edge views
  // if retaining whole neighboring chunk payload handles becomes a measurable cost.
  {
    ARV_PROFILE_ZONE("Chunk lookup / access");
    const core::ScopedThreadTiming lookup_timing{
        core::PerfMetric::ChunkLookupAccess,
        "world",
        profile_context.data()};
    for (i32 oz = -1; oz <= 1; ++oz) {
      for (i32 oy = -1; oy <= 1; ++oy) {
        for (i32 ox = -1; ox <= 1; ++ox) {
          if (ox == 0 && oy == 0 && oz == 0) {
            continue;
          }
          const ChunkCoord neighbor_coord{{
              coord.value.x + ox,
              coord.value.y + oy,
              coord.value.z + oz,
          }};
          const voxel::ChunkSection* neighbor = find_chunk(neighbor_coord);
          if (neighbor == nullptr && missing_neighbor_policy == MissingNeighborPolicy::RequireLoaded) {
            return std::nullopt;
          }
          if (neighbor != nullptr) {
            snapshot.storages[voxel::snapshot_storage_index(ox, oy, oz)] =
                neighbor->storage_view();
          }
        }
      }
    }
  }

  return snapshot;
}

bool WorldRuntime::validate_mesh_result(ChunkCoord coord, u64 source_version) const noexcept {
  const voxel::ChunkSection* chunk = find_chunk(coord);
  return chunk != nullptr && chunk->version() == source_version;
}

bool WorldRuntime::validate_mesh_content_result(
    ChunkCoord coord,
    u64 source_content_version) const noexcept {
  const voxel::ChunkSection* chunk = find_chunk(coord);
  return chunk != nullptr && chunk->content_version() == source_content_version;
}

bool WorldRuntime::publish_mesh_result(
    ChunkCoord coord,
    u64 source_block_version,
    u64 source_light_version,
    u64 source_content_version) noexcept {
  voxel::ChunkSection* chunk = find_chunk(coord);
  if (chunk == nullptr || chunk->block_version() != source_block_version) {
    return false;
  }
  if (source_content_version != 0U && chunk->content_version() != source_content_version) {
    return false;
  }
  chunk->mark_mesh_ready(source_block_version, source_light_version);
  return true;
}

bool WorldRuntime::publish_generated_chunk(GeneratedChunk generated) noexcept {
  if (generated.world_id != impl_->id) {
    return false;
  }

  voxel::ChunkSection& chunk = impl_->chunks.ensure_chunk(
      generated.coord,
      voxel::ChunkLifecycleState::Loaded,
      impl_->dirty);
  impl_->chunks.set_lifecycle_state_internal(chunk, voxel::ChunkLifecycleState::Loaded);
  chunk.replace_generated_blocks(std::move(generated.blocks));
  impl_->dirty.mark_mesh_content_dirty();
  impl_->dirty.enqueue_dirty_mesh_chunk(generated.coord);
  impl_->dirty.mark_lighting_content_dirty();
  impl_->dirty.mark_loaded_chunk_neighbors_dirty(*this, generated.coord);
  return true;
}

void WorldRuntime::restore_saved_chunk(
    ChunkCoord coord,
    std::array<voxel::BlockVariantID, voxel::ChunkVolume> blocks,
    std::array<voxel::LightWord, voxel::ChunkVolume> lights,
    std::optional<std::array<voxel::FluidCell, voxel::ChunkVolume>> fluids) noexcept {
  voxel::ChunkSection& chunk =
      impl_->chunks.ensure_chunk(coord, voxel::ChunkLifecycleState::Loaded, impl_->dirty);
  chunk.replace_saved_contents(
      std::move(blocks),
      std::move(lights),
      std::move(fluids));
  chunk.mark_dirty(voxel::ChunkDirtyFlags::Lighting);
  impl_->dirty.mark_mesh_content_dirty();
  impl_->dirty.enqueue_dirty_mesh_chunk(coord);
  impl_->dirty.mark_lighting_content_dirty();
}

} // namespace arv::world
