#include "world_runtime_internal.hpp"

#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"

#include "chunk_dependency_mask.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace arv::world {
namespace {

using DependencyMask = detail::ChunkDependencyMask;

[[nodiscard]] ChunkSectionCoord offset_chunk(ChunkSectionCoord coord, Vec3i offset) noexcept {
  return {{
      coord.value.x + offset.x,
      coord.value.y + offset.y,
      coord.value.z + offset.z,
  }};
}

[[nodiscard]] voxel::ChunkStorageView make_generated_lighting_snapshot_storage(
    const GenerationSurfaceProfile& surface,
    ChunkSectionCoord coord,
    const voxel::ChunkStorage& source) {
  voxel::ChunkStorageHandle storage =
      std::make_shared<voxel::ChunkStorage>(source);

  for (usize column_index = 0; column_index < voxel::ChunkArea; ++column_index) {
    const ChunkLocalColumn column = voxel::local_column(column_index);
    const i32 surface_y = surface[column_index];
    bool sky_open = true;
    for (i32 y = ChunkSize - 1; y >= 0; --y) {
      const i32 world_y = coord.value.y * ChunkSize + y;
      const usize index = voxel::local_index({{column.x, y, column.z}});
      if (world_y <= surface_y) {
        continue;
      }
      if (storage->blocks[index] != voxel::AirBlockVariantID) {
        sky_open = false;
        continue;
      }
      if (sky_open && voxel::light_sky(storage->lights[index]) < voxel::MaxLightLevel) {
        storage->lights[index] =
            voxel::with_sky_light(storage->lights[index], voxel::MaxLightLevel);
      }
    }
  }
  return storage;
}

} // namespace

std::optional<voxel::ChunkSnapshot> WorldRuntime::get_chunk_snapshot(ChunkSectionCoord coord) const {
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
    ChunkSectionCoord coord,
    std::array<voxel::BlockVariantID, voxel::ChunkVolume> blocks,
    std::optional<std::array<voxel::LightWord, voxel::ChunkVolume>> lights,
    std::optional<std::array<voxel::FluidCell, voxel::ChunkVolume>> fluids,
    bool lighting_authoritative) noexcept {
  assert_owner_thread();
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
          detail::add_boundary_dependency_offsets(
              block_dependency_offsets,
              voxel::local_pos(index));
        }
      }
    }
    if (lights_changed) {
      for (usize index = 0; index < voxel::ChunkVolume; ++index) {
        if (existing_chunk->lights()[index] != resolved_lights[index]) {
          detail::add_boundary_dependency_offsets(
              light_dependency_offsets,
              voxel::local_pos(index));
        }
      }
    }
  } else {
    detail::add_all_neighbor_dependency_offsets(block_dependency_offsets);
    detail::add_all_neighbor_dependency_offsets(light_dependency_offsets);
  }

  voxel::ChunkSection& chunk =
      impl_->chunks.ensure_chunk(coord, voxel::ChunkLifecycleState::Loaded, impl_->dirty);
  impl_->chunks.set_lifecycle_state_internal(chunk, voxel::ChunkLifecycleState::Loaded);
  chunk.replace_saved_contents(
      std::move(blocks),
      std::move(resolved_lights),
      std::move(fluids),
      has_lighting_payload && lighting_authoritative);
  if (has_lighting_payload && lighting_authoritative) {
    chunk.clear_dirty(voxel::ChunkDirtyFlags::Lighting);
    impl_->dirty.forget_lighting_chunk(coord);
  } else {
    impl_->dirty.mark_chunk_lighting_dirty(*this, coord);
  }

  if (needs_first_mesh_dirty || blocks_changed || lights_changed || fluids_changed) {
    impl_->dirty.mark_mesh_content_dirty();
    impl_->dirty.enqueue_dirty_mesh_chunk(coord);
  }
  if (blocks_changed || fluids_changed || needs_first_mesh_dirty) {
    for (usize index = 0; index < voxel::ChunkSnapshotNeighborhoodVolume; ++index) {
      if (!block_dependency_offsets[index]) {
        continue;
      }
      impl_->dirty.mark_neighbor_mesh_only_dependency_dirty(
          *this,
          offset_chunk(coord, voxel::snapshot_storage_offset(index)));
    }
  }
  if (lights_changed) {
    for (usize index = 0; index < voxel::ChunkSnapshotNeighborhoodVolume; ++index) {
      if (!light_dependency_offsets[index]) {
        continue;
      }
      impl_->dirty.mark_neighbor_light_dependency_dirty(
          *this,
          offset_chunk(coord, voxel::snapshot_storage_offset(index)));
    }
  }
}

std::optional<voxel::ChunkSnapshot> WorldRuntime::make_meshing_snapshot(
    ChunkSectionCoord coord,
    MissingNeighborPolicy missing_neighbor_policy) const {
  ARV_PROFILE_ZONE("Snapshot creation");
  std::array<char, core::PerfEventContextCapacity> profile_context{};
  if constexpr (core::perf_stats_enabled()) {
    core::format_chunk_section_perf_context(profile_context, coord);
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
  // TODO(arvane): Narrow meshing neighbor inputs to compact immutable boundary/edge views
  // if retaining whole neighboring chunk payload handles becomes a measurable cost.
  {
    ARV_PROFILE_ZONE("Chunk lookup / access");
    const core::ScopedThreadTiming lookup_timing{
        core::PerfMetric::ChunkLookupAccess,
        "world",
        profile_context.data()};
    std::vector<std::pair<ChunkCoord, GenerationSurfaceProfile>> generated_surface_cache;
    auto generated_surface_profile_for =
        [this, &generated_surface_cache](ChunkCoord column_coord)
        -> const GenerationSurfaceProfile& {
      if (const Chunk* column = impl_->chunks.find_column(column_coord);
          column != nullptr) {
        if (const GenerationSurfaceProfile* cached = column->generation_surface_profile();
            cached != nullptr) {
          return *cached;
        }
      }
      const auto cached_iter = std::find_if(
          generated_surface_cache.begin(),
          generated_surface_cache.end(),
          [column_coord](const auto& entry) {
            return entry.first == column_coord;
          });
      if (cached_iter != generated_surface_cache.end()) {
        return cached_iter->second;
      }

      generated_surface_cache.emplace_back(
          column_coord,
          terrain_height_profile_at(
              impl_->generation_config.value(),
              impl_->generation_seed,
              column_coord));
      return generated_surface_cache.back().second;
    };
    auto storage_view_for_chunk =
        [&generated_surface_profile_for, this](const voxel::ChunkSection& source) {
          if (!impl_->generation_config.has_value() ||
              !source.is_dirty(voxel::ChunkDirtyFlags::Lighting)) {
            return source.storage_view();
          }

          const voxel::ChunkStorageView source_storage = source.storage_view();
          return make_generated_lighting_snapshot_storage(
              generated_surface_profile_for(chunk_to_column(source.coord())),
              source.coord(),
              *source_storage);
        };
    snapshot.storages[voxel::ChunkSnapshotCenterStorageIndex] =
        storage_view_for_chunk(*chunk);
    for (usize storage_index = 0; storage_index < voxel::ChunkSnapshotNeighborhoodVolume; ++storage_index) {
      if (storage_index == voxel::ChunkSnapshotCenterStorageIndex) {
        continue;
      }
      const ChunkSectionCoord neighbor_coord =
          offset_chunk(coord, voxel::snapshot_storage_offset(storage_index));
      const voxel::ChunkSection* neighbor = find_chunk(neighbor_coord);
      if (neighbor == nullptr && missing_neighbor_policy == MissingNeighborPolicy::RequireLoaded) {
        return std::nullopt;
      }
      if (neighbor != nullptr) {
        snapshot.storages[storage_index] = storage_view_for_chunk(*neighbor);
      } else {
        snapshot.storages[storage_index].reset();
      }
    }
  }

  return snapshot;
}

bool WorldRuntime::validate_mesh_result(ChunkSectionCoord coord, u64 source_version) const noexcept {
  const voxel::ChunkSection* chunk = find_chunk(coord);
  return chunk != nullptr && chunk->version() == source_version;
}

bool WorldRuntime::validate_mesh_content_result(
    ChunkSectionCoord coord,
    u64 source_content_version) const noexcept {
  const voxel::ChunkSection* chunk = find_chunk(coord);
  return chunk != nullptr && chunk->content_version() == source_content_version;
}

bool WorldRuntime::publish_mesh_result(
    ChunkSectionCoord coord,
    u64 source_block_version,
    u64 source_light_version,
    u64 source_content_version) noexcept {
  assert_owner_thread();
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
  assert_owner_thread();
  if (generated.world_id != impl_->id) {
    return false;
  }

  bool published_any = false;
  for (GeneratedChunkSection& generated_section : generated.sections) {
    voxel::ChunkSection* existing = impl_->chunks.find_chunk(generated_section.coord);
    if (existing != nullptr &&
        existing->lifecycle_state() != voxel::ChunkLifecycleState::Requested &&
        existing->lifecycle_state() != voxel::ChunkLifecycleState::Generating) {
      continue;
    }

    voxel::ChunkSection& chunk = impl_->chunks.ensure_chunk(
        generated_section.coord,
        voxel::ChunkLifecycleState::Loaded,
        impl_->dirty);
    impl_->chunks.set_lifecycle_state_internal(chunk, voxel::ChunkLifecycleState::Loaded);
    std::optional<std::array<voxel::FluidCell, voxel::ChunkVolume>> generated_fluids{
        std::move(generated_section.fluids)};
    chunk.replace_generated_blocks(
        std::move(generated_section.blocks),
        std::move(generated_fluids));
    if (Chunk* column = impl_->chunks.find_column(generated.coord); column != nullptr) {
      column->set_generation_surface_profile(generated.surface_y_by_column);
    }
    impl_->dirty.mark_mesh_content_dirty();
    impl_->dirty.enqueue_dirty_mesh_chunk(generated_section.coord);
    impl_->dirty.mark_chunk_lighting_dirty(*this, generated_section.coord);
    impl_->dirty.mark_lighting_content_dirty();
    impl_->dirty.mark_loaded_chunk_neighbors_dirty(*this, generated_section.coord);
    published_any = true;
  }
  return published_any;
}

void WorldRuntime::restore_saved_chunk(
    ChunkSectionCoord coord,
    std::array<voxel::BlockVariantID, voxel::ChunkVolume> blocks,
    std::array<voxel::LightWord, voxel::ChunkVolume> lights,
    std::optional<std::array<voxel::FluidCell, voxel::ChunkVolume>> fluids,
    bool lighting_authoritative) noexcept {
  assert_owner_thread();
  voxel::ChunkSection& chunk =
      impl_->chunks.ensure_chunk(coord, voxel::ChunkLifecycleState::Loaded, impl_->dirty);
  chunk.replace_saved_contents(
      std::move(blocks),
      std::move(lights),
      std::move(fluids),
      lighting_authoritative);
  if (lighting_authoritative) {
    chunk.clear_dirty(voxel::ChunkDirtyFlags::Lighting);
    impl_->dirty.forget_lighting_chunk(coord);
  } else {
    impl_->dirty.mark_chunk_lighting_dirty(*this, coord);
    impl_->dirty.mark_lighting_content_dirty();
  }
  impl_->dirty.mark_mesh_content_dirty();
  impl_->dirty.enqueue_dirty_mesh_chunk(coord);
}

} // namespace arv::world
