#include "arv/world/world_runtime.hpp"

#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"
#include "arv/world/world_queries.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace arv::world {
namespace {

using DependencyMask = std::array<bool, voxel::ChunkSnapshotNeighborhoodVolume>;

[[nodiscard]] u8 fluid_update_priority(FluidUpdateReason reason) noexcept {
  switch (reason) {
    case FluidUpdateReason::FluidRemoved:
    case FluidUpdateReason::BlockChanged:
      return 2U;
    case FluidUpdateReason::FluidAdded:
    case FluidUpdateReason::NeighborChanged:
    case FluidUpdateReason::ChunkActivated:
      return 1U;
  }
  return 0U;
}

[[nodiscard]] bool should_replace_pending_fluid_update(
    const FluidUpdate& current,
    const FluidUpdate& candidate) noexcept {
  if (candidate.execute_at != current.execute_at) {
    return candidate.execute_at < current.execute_at;
  }
  return fluid_update_priority(candidate.reason) > fluid_update_priority(current.reason);
}

[[nodiscard]] bool matches_pending_fluid_update(
    const FluidUpdate& pending,
    const FluidUpdate& queued) noexcept {
  return pending.execute_at == queued.execute_at &&
         fluid_update_priority(pending.reason) == fluid_update_priority(queued.reason);
}

[[nodiscard]] SectionLocalPos local_from_index(usize index) noexcept {
  const i32 x = static_cast<i32>(index % voxel::ChunkSide);
  index /= voxel::ChunkSide;
  const i32 y = static_cast<i32>(index % voxel::ChunkSide);
  const i32 z = static_cast<i32>(index / voxel::ChunkSide);
  return {{x, y, z}};
}

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

[[nodiscard]] bool is_idle_goal(std::string_view type) noexcept {
  return type == "arvane:idle";
}

[[nodiscard]] bool is_wander_goal(std::string_view type) noexcept {
  return type == "arvane:wander";
}

[[nodiscard]] bool is_float_in_fluid_goal(std::string_view type) noexcept {
  return type == "arvane:float_in_fluid";
}

[[nodiscard]] f32 positive_fraction(f32 value) noexcept {
  const f32 whole = std::floor(value);
  return value - whole;
}

[[nodiscard]] Vec3f deterministic_wander_direction(
    EntityID entity_id,
    TickIndex tick,
    usize goal_index) noexcept {
  const f32 seed =
      static_cast<f32>(entity_id.value * 37U + static_cast<u32>(goal_index) * 101U) +
      static_cast<f32>(tick % 100000U) * 0.173F;
  const f32 angle = positive_fraction(std::sin(seed) * 43758.5453F) * 6.28318530718F;
  return {std::cos(angle), 0.0F, std::sin(angle)};
}

[[nodiscard]] Aabb3f entity_body_bounds(Vec3f position, Vec3f box) noexcept {
  const Vec3f size{
      std::max(0.0F, box.x),
      std::max(0.0F, box.y),
      std::max(0.0F, box.z)};
  const f32 half_x = size.x * 0.5F;
  const f32 half_z = size.z * 0.5F;
  return {
      {position.x - half_x, position.y, position.z - half_z},
      {position.x + half_x, position.y + size.y, position.z + half_z}};
}

[[nodiscard]] Quatf yaw_rotation(f32 radians) noexcept {
  const f32 half = radians * 0.5F;
  return {0.0F, std::sin(half), 0.0F, std::cos(half)};
}

[[nodiscard]] bool has_horizontal_motion(Vec3f value) noexcept {
  return value.x * value.x + value.z * value.z > 0.000001F;
}

[[nodiscard]] f32 yaw_from_horizontal_direction(Vec3f direction) noexcept {
  constexpr f32 ModelForwardYawOffset = 3.14159265358979323846F;
  return std::atan2(direction.x, direction.z) + ModelForwardYawOffset;
}

} // namespace

WorldRuntime::WorldRuntime(WorldID id) : id_(id) {
}

WorldID WorldRuntime::id() const noexcept {
  return id_;
}

voxel::ChunkSection& WorldRuntime::ensure_chunk(
    ChunkCoord coord,
    voxel::ChunkLifecycleState state) {
  auto [iter, inserted] = chunks_.try_emplace(coord, coord);
  if (inserted) {
    iter->second.set_lifecycle_state(state);
    ++lifecycle_counts_[lifecycle_index(state)];
    if (state == voxel::ChunkLifecycleState::Active) {
      ++active_chunk_count_;
    }
    enqueue_dirty_mesh_chunk(coord);
  }
  return iter->second;
}

void WorldRuntime::request_chunk_load(ChunkCoord coord) {
  if (find_chunk(coord) != nullptr) {
    return;
  }
  (void)ensure_chunk(coord, voxel::ChunkLifecycleState::Requested);
}

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
          add_block_dependency_offsets(block_dependency_offsets, local_from_index(index));
        }
      }
    }
    if (lights_changed) {
      for (usize index = 0; index < voxel::ChunkVolume; ++index) {
        if (existing_chunk->lights()[index] != resolved_lights[index]) {
          add_light_dependency_offsets(light_dependency_offsets, local_from_index(index));
        }
      }
    }
  } else {
    add_all_neighbor_dependency_offsets(block_dependency_offsets);
    add_all_neighbor_dependency_offsets(light_dependency_offsets);
  }

  voxel::ChunkSection& chunk = ensure_chunk(coord, voxel::ChunkLifecycleState::Loaded);
  set_chunk_lifecycle_state_internal(chunk, voxel::ChunkLifecycleState::Loaded);
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
    mark_mesh_content_dirty();
    enqueue_dirty_mesh_chunk(coord);
  }
  if (blocks_changed || fluids_changed || needs_first_mesh_dirty) {
    for (i32 z = -1; z <= 1; ++z) {
      for (i32 y = -1; y <= 1; ++y) {
        for (i32 x = -1; x <= 1; ++x) {
          if (!block_dependency_offsets[voxel::snapshot_storage_index(x, y, z)]) {
            continue;
          }
          mark_neighbor_mesh_only_dependency_dirty({{
              coord.value.x + x,
              coord.value.y + y,
              coord.value.z + z,
          }});
        }
      }
    }
  }
  if (lights_changed) {
    for (const Vec3i& offset : NeighborOffsets) {
      if (!light_dependency_offsets[voxel::snapshot_storage_index(offset.x, offset.y, offset.z)]) {
        continue;
      }
      mark_neighbor_light_dependency_dirty({{
          coord.value.x + offset.x,
          coord.value.y + offset.y,
          coord.value.z + offset.z,
      }});
    }
  }
}

bool WorldRuntime::unload_chunk(ChunkCoord coord) noexcept {
  const auto found = chunks_.find(coord);
  if (found == chunks_.end()) {
    return false;
  }
  const voxel::ChunkLifecycleState state = found->second.lifecycle_state();
  usize& state_count = lifecycle_counts_[lifecycle_index(state)];
  if (state_count > 0U) {
    --state_count;
  }
  if (state == voxel::ChunkLifecycleState::Active && active_chunk_count_ > 0U) {
    --active_chunk_count_;
  }
  chunks_.erase(found);

  mark_mesh_content_dirty();
  mark_loaded_chunk_neighbors_dirty(coord);
  recompute_lighting_work_pending();
  return true;
}

voxel::ChunkSection* WorldRuntime::find_chunk(ChunkCoord coord) noexcept {
  const auto found = chunks_.find(coord);
  return found == chunks_.end() ? nullptr : &found->second;
}

const voxel::ChunkSection* WorldRuntime::find_chunk(ChunkCoord coord) const noexcept {
  const auto found = chunks_.find(coord);
  return found == chunks_.end() ? nullptr : &found->second;
}

usize WorldRuntime::loaded_chunk_count() const noexcept {
  return chunks_.size();
}

ChunkWorkloadSnapshot WorldRuntime::chunk_workload_snapshot() const noexcept {
  ChunkWorkloadSnapshot snapshot;
  snapshot.loaded_chunks = chunks_.size();
  snapshot.active_chunks = active_chunk_count_;
  snapshot.chunks_awaiting_generation =
      lifecycle_count(voxel::ChunkLifecycleState::Requested) +
      lifecycle_count(voxel::ChunkLifecycleState::Loading) +
      lifecycle_count(voxel::ChunkLifecycleState::Generating) +
      lifecycle_count(voxel::ChunkLifecycleState::Populating);
  snapshot.chunk_sections_awaiting_lighting =
      lighting_work_pending_ ? std::max<usize>(last_lighting_snapshot_chunk_count_, 1U) : 0U;
  snapshot.chunk_sections_awaiting_meshing = dirty_mesh_chunk_set_.size();
  return snapshot;
}

std::vector<ChunkCoord> WorldRuntime::loaded_chunk_coords() const {
  std::vector<ChunkCoord> coords;
  coords.reserve(chunks_.size());
  for (const auto& [coord, chunk] : chunks_) {
    (void)chunk;
    coords.emplace_back(coord);
  }
  return coords;
}

TickIndex WorldRuntime::tick_index() const noexcept {
  return tick_index_;
}

void WorldRuntime::set_tick_index(TickIndex tick_index) noexcept {
  tick_index_ = tick_index;
}

u64 WorldRuntime::lighting_revision() const noexcept {
  return lighting_revision_;
}

u64 WorldRuntime::mesh_revision() const noexcept {
  return mesh_revision_;
}

void WorldRuntime::set_generation_context(u64 seed, WorldGenerationConfig config) {
  generation_seed_ = seed;
  generation_config_ = std::move(config);
}

void WorldRuntime::clear_generation_context() noexcept {
  generation_config_.reset();
  generation_seed_ = 0U;
}

void WorldRuntime::set_tick_policy(WorldTickPolicy policy) noexcept {
  tick_policy_ = policy;
}

WorldTickPolicy WorldRuntime::tick_policy() const noexcept {
  return tick_policy_;
}

bool WorldRuntime::is_chunk_active(ChunkCoord coord) const noexcept {
  const voxel::ChunkSection* chunk = find_chunk(coord);
  return chunk != nullptr && chunk->lifecycle_state() == voxel::ChunkLifecycleState::Active;
}

usize WorldRuntime::active_chunk_count() const noexcept {
  return active_chunk_count_;
}

bool WorldRuntime::set_chunk_lifecycle_state(ChunkCoord coord, voxel::ChunkLifecycleState state) noexcept {
  voxel::ChunkSection* chunk = find_chunk(coord);
  if (chunk == nullptr) {
    return false;
  }
  set_chunk_lifecycle_state_internal(*chunk, state);
  if (state == voxel::ChunkLifecycleState::Active) {
    FluidSolver::enqueue_chunk_activation_updates(*this, coord);
  }
  return true;
}

void WorldRuntime::update_simulation_radius(ChunkCoord center, i32 radius) noexcept {
  const std::array<ChunkCoord, 1> centers{{center}};
  update_simulation_radii(centers, radius);
}

void WorldRuntime::update_simulation_radii(std::span<const ChunkCoord> centers, i32 radius) noexcept {
  for (auto& [coord, chunk] : chunks_) {
    if (chunk.lifecycle_state() != voxel::ChunkLifecycleState::Loaded &&
        chunk.lifecycle_state() != voxel::ChunkLifecycleState::Active) {
      continue;
    }

    bool active = false;
    if (radius >= 0) {
      for (const ChunkCoord center : centers) {
        const i32 dx = coord.value.x - center.value.x;
        const i32 dy = coord.value.y - center.value.y;
        const i32 dz = coord.value.z - center.value.z;
        if (max_abs3(dx, dy, dz) <= radius) {
          active = true;
          break;
        }
      }
    }

    const voxel::ChunkLifecycleState next_state =
        active ? voxel::ChunkLifecycleState::Active : voxel::ChunkLifecycleState::Loaded;
    const bool became_active = active &&
                               chunk.lifecycle_state() != voxel::ChunkLifecycleState::Active;
    if (chunk.lifecycle_state() != next_state) {
      set_chunk_lifecycle_state_internal(chunk, next_state);
    }
    if (became_active) {
      FluidSolver::enqueue_chunk_activation_updates(*this, coord);
    }
  }
}

voxel::BlockVariantID WorldRuntime::get_block(BlockPos pos) const noexcept {
  const ChunkCoord chunk_coord = block_to_chunk(pos);
  const voxel::ChunkSection* chunk = find_chunk(chunk_coord);
  if (chunk == nullptr) {
    return voxel::AirBlockVariantID;
  }
  return chunk->block(block_to_local(pos));
}

voxel::LightWord WorldRuntime::get_light(BlockPos pos) const noexcept {
  const ChunkCoord chunk_coord = block_to_chunk(pos);
  const voxel::ChunkSection* chunk = find_chunk(chunk_coord);
  if (chunk == nullptr) {
    return voxel::NoLight;
  }
  return chunk->light(block_to_local(pos));
}

voxel::FluidCell WorldRuntime::get_fluid(BlockPos pos) const noexcept {
  const ChunkCoord chunk_coord = block_to_chunk(pos);
  const voxel::ChunkSection* chunk = find_chunk(chunk_coord);
  if (chunk == nullptr) {
    return voxel::empty_fluid_cell();
  }
  return chunk->fluid(block_to_local(pos));
}

bool WorldRuntime::set_block(BlockPos pos, voxel::BlockVariantID variant) noexcept {
  const ChunkCoord chunk_coord = block_to_chunk(pos);
  voxel::ChunkSection* chunk = find_chunk(chunk_coord);
  if (chunk == nullptr) {
    return false;
  }

  const SectionLocalPos local = block_to_local(pos);
  if (!chunk->set_block(local, variant)) {
    return false;
  }

  mark_mesh_content_dirty();
  enqueue_dirty_mesh_chunk(chunk_coord);
  mark_boundary_neighbors_dirty(chunk_coord, local);
  mark_lighting_content_dirty();
  enqueue_neighbor_block_updates(pos);
  FluidSolver::enqueue_neighbor_updates(*this, pos, FluidUpdateReason::BlockChanged);
  record_replicated_block_update(pos, variant);
  return true;
}

bool WorldRuntime::set_fluid(
    BlockPos pos,
    voxel::FluidCell fluid,
    const voxel::FluidDefinition& definition) noexcept {
  return set_fluid_internal(pos, fluid, definition, true);
}

bool WorldRuntime::set_fluid_internal(
    BlockPos pos,
    voxel::FluidCell fluid,
    const voxel::FluidDefinition& definition,
    bool enqueue_updates) noexcept {
  const ChunkCoord chunk_coord = block_to_chunk(pos);
  voxel::ChunkSection* chunk = find_chunk(chunk_coord);
  if (chunk == nullptr) {
    return false;
  }

  const SectionLocalPos local = block_to_local(pos);
  const bool affects_lighting = definition.reduces_sky_light || definition.emits_light;
  if (!chunk->set_fluid(local, fluid, affects_lighting)) {
    return false;
  }

  mark_mesh_content_dirty();
  enqueue_dirty_mesh_chunk(chunk_coord);
  mark_boundary_neighbors_dirty(chunk_coord, local);
  if (affects_lighting) {
    mark_lighting_content_dirty();
  }
  if (enqueue_updates) {
    FluidSolver::enqueue_neighbor_updates(
        *this,
        pos,
        fluid.empty() ? FluidUpdateReason::FluidRemoved : FluidUpdateReason::FluidAdded,
        definition.flow_interval_ticks);
  }
  return true;
}

bool WorldRuntime::place_fluid_source(BlockPos pos, const voxel::FluidDefinition& definition) noexcept {
  return set_fluid(pos, voxel::make_fluid_source(definition), definition);
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
            snapshot.storages[voxel::snapshot_storage_index(ox, oy, oz)] = neighbor->storage_view();
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
    u64 source_version,
    u64 source_light_version,
    u64 source_content_version) noexcept {
  voxel::ChunkSection* chunk = find_chunk(coord);
  if (chunk == nullptr || chunk->block_version() != source_version) {
    return false;
  }
  if (source_content_version != 0U && chunk->content_version() != source_content_version) {
    return false;
  }
  chunk->mark_mesh_ready(source_version, source_light_version);
  return true;
}

bool WorldRuntime::publish_generated_chunk(GeneratedChunk generated) noexcept {
  if (generated.world_id != id_) {
    return false;
  }

  voxel::ChunkSection& chunk = ensure_chunk(
      generated.coord,
      voxel::ChunkLifecycleState::Loaded);
  set_chunk_lifecycle_state_internal(chunk, voxel::ChunkLifecycleState::Loaded);
  chunk.replace_generated_blocks(std::move(generated.blocks));
  mark_mesh_content_dirty();
  enqueue_dirty_mesh_chunk(generated.coord);
  mark_lighting_content_dirty();
  mark_loaded_chunk_neighbors_dirty(generated.coord);
  return true;
}

void WorldRuntime::restore_saved_chunk(
    ChunkCoord coord,
    std::array<voxel::BlockVariantID, voxel::ChunkVolume> blocks,
    std::array<voxel::LightWord, voxel::ChunkVolume> lights,
    std::optional<std::array<voxel::FluidCell, voxel::ChunkVolume>> fluids) noexcept {
  voxel::ChunkSection& chunk = ensure_chunk(coord, voxel::ChunkLifecycleState::Loaded);
  chunk.replace_saved_contents(
      std::move(blocks),
      std::move(lights),
      std::move(fluids));
  chunk.mark_dirty(voxel::ChunkDirtyFlags::Lighting);
  mark_mesh_content_dirty();
  enqueue_dirty_mesh_chunk(coord);
  mark_lighting_content_dirty();
}

EntityID WorldRuntime::create_entity(EntityCreateInfo info) {
  if (info.type_name.empty()) {
    info.type_name = "arvane:unknown";
  }
  if (info.type_id.value == 0U && info.type_name == entity::PlayerEntityTypeName) {
    info.type_id = entity::PlayerEntityTypeID;
  }

  const EntityID entity_id{next_entity_id_++};
  const entt::entity handle = entity_registry_.create();
  entity_handles_[entity_id] = handle;

  entity_registry_.emplace<entity::RuntimeEntity>(handle, entity_id);
  entity_registry_.emplace<entity::EntityType>(handle, info.type_id, info.type_name);
  entity_registry_.emplace<entity::EntityTransform>(
      handle,
      info.position,
      info.position,
      info.rotation,
      info.rotation,
      Vec3f{1.0F, 1.0F, 1.0F});
  entity_registry_.emplace<entity::EntityVelocity>(handle, info.velocity);
  entity_registry_.emplace<entity::ChunkRelevance>(handle, chunk_for_position(info.position));

  if (info.replicated) {
    entity_registry_.emplace<entity::NetworkEntity>(handle, EntityNetID{next_entity_net_id_++});
  }

  if (info.persistent) {
    if (info.persistent_uuid.empty()) {
      // TODO(arvane): Replace this deterministic placeholder with real UUID
      // allocation once the global entity/profile identity policy is specified.
      info.persistent_uuid =
          "entity:" + std::to_string(id_.value) + ":" + std::to_string(entity_id.value);
    }
    entity_registry_.emplace<entity::PersistentEntity>(
        handle,
        std::move(info.persistent_uuid),
        info.type_id,
        info.type_name);
  }

  if (info.owning_player.has_value()) {
    entity_registry_.emplace<entity::PlayerEntity>(handle, info.owning_player.value());
    player_entities_[info.owning_player.value()] = entity_id;

    player::PlayerState player_state =
        info.player_state.has_value() ? std::move(info.player_state.value()) : player::PlayerState{};
    player_state.player_id = info.owning_player.value();
    player_state.world_id = id_;
    player_state.position = info.position;
    player_state.velocity = info.velocity;
    entity_registry_.emplace<player::PlayerState>(handle, std::move(player_state));
  }

  if (info.dropped_item_stack.has_value()) {
    entity_registry_.emplace<entity::DroppedItem>(
        handle,
        info.dropped_item_stack.value(),
        info.dropped_item_pickup_delay_seconds);
  }

  if (info.authored_components.has_value()) {
    apply_authored_components(handle, info.authored_components.value());
  }

  return entity_id;
}

EntityID WorldRuntime::create_dropped_item(
    player::ItemStack stack,
    Vec3f position,
    Vec3f velocity,
    Seconds pickup_delay_seconds,
    bool persistent) {
  EntityCreateInfo info;
  info.type_name = std::string{entity::DroppedItemEntityTypeName};
  info.position = position;
  info.velocity = velocity;
  info.persistent = persistent;
  info.replicated = true;
  info.dropped_item_stack = stack;
  info.dropped_item_pickup_delay_seconds = std::max<Seconds>(pickup_delay_seconds, 0.0F);
  return create_entity(std::move(info));
}

bool WorldRuntime::destroy_entity(EntityID entity_id, EntityDespawnReason reason) {
  (void)reason;
  if (!entity_exists(entity_id)) {
    return false;
  }
  if (std::find(pending_entity_despawns_.begin(), pending_entity_despawns_.end(), entity_id) ==
      pending_entity_despawns_.end()) {
    pending_entity_despawns_.push_back(entity_id);
  }
  return true;
}

void WorldRuntime::flush_entity_despawns() {
  for (const EntityID entity_id : pending_entity_despawns_) {
    const entt::entity handle = entity_handle(entity_id);
    if (handle == entt::null || !entity_registry_.valid(handle)) {
      continue;
    }
    if (const auto* player_entity = entity_registry_.try_get<entity::PlayerEntity>(handle);
        player_entity != nullptr) {
      player_entities_.erase(player_entity->player_id);
      player_region_states_.erase(player_entity->player_id);
    }
    entity_handles_.erase(entity_id);
    entity_registry_.destroy(handle);
  }
  pending_entity_despawns_.clear();
}

bool WorldRuntime::entity_exists(EntityID entity_id) const noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle != entt::null && entity_registry_.valid(handle);
}

usize WorldRuntime::entity_count() const noexcept {
  return entity_handles_.size();
}

std::vector<EntityID> WorldRuntime::entity_ids() const {
  std::vector<EntityID> ids;
  ids.reserve(entity_handles_.size());
  for (const auto& [entity_id, handle] : entity_handles_) {
    if (handle != entt::null && entity_registry_.valid(handle)) {
      ids.emplace_back(entity_id);
    }
  }
  return ids;
}

std::optional<EntityID> WorldRuntime::entity_for_player(PlayerID player_id) const noexcept {
  const auto found = player_entities_.find(player_id);
  return found == player_entities_.end() ? std::nullopt : std::optional<EntityID>{found->second};
}

std::optional<PlayerID> WorldRuntime::player_for_entity(EntityID entity_id) const noexcept {
  const entt::entity handle = entity_handle(entity_id);
  if (handle == entt::null || !entity_registry_.valid(handle)) {
    return std::nullopt;
  }
  const auto* player_entity = entity_registry_.try_get<entity::PlayerEntity>(handle);
  return player_entity == nullptr ? std::nullopt : std::optional<PlayerID>{player_entity->player_id};
}

std::optional<EntityNetID> WorldRuntime::network_id_for_entity(EntityID entity_id) const noexcept {
  const entt::entity handle = entity_handle(entity_id);
  if (handle == entt::null || !entity_registry_.valid(handle)) {
    return std::nullopt;
  }
  const auto* network = entity_registry_.try_get<entity::NetworkEntity>(handle);
  return network == nullptr ? std::nullopt : std::optional<EntityNetID>{network->net_id};
}

std::optional<ChunkCoord> WorldRuntime::entity_chunk(EntityID entity_id) const noexcept {
  const entt::entity handle = entity_handle(entity_id);
  if (handle == entt::null || !entity_registry_.valid(handle)) {
    return std::nullopt;
  }
  const auto* relevance = entity_registry_.try_get<entity::ChunkRelevance>(handle);
  return relevance == nullptr ? std::nullopt : std::optional<ChunkCoord>{relevance->current_chunk};
}

entity::EntityTransform* WorldRuntime::find_entity_transform(EntityID entity_id) noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::EntityTransform>(handle);
}

const entity::EntityTransform* WorldRuntime::find_entity_transform(EntityID entity_id) const noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::EntityTransform>(handle);
}

entity::EntityVelocity* WorldRuntime::find_entity_velocity(EntityID entity_id) noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::EntityVelocity>(handle);
}

const entity::EntityVelocity* WorldRuntime::find_entity_velocity(EntityID entity_id) const noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::EntityVelocity>(handle);
}

const entity::EntityType* WorldRuntime::find_entity_type(EntityID entity_id) const noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::EntityType>(handle);
}

entity::EntityCollision* WorldRuntime::find_entity_collision(EntityID entity_id) noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::EntityCollision>(handle);
}

const entity::EntityCollision* WorldRuntime::find_entity_collision(EntityID entity_id) const noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::EntityCollision>(handle);
}

entity::EntityMovement* WorldRuntime::find_entity_movement(EntityID entity_id) noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::EntityMovement>(handle);
}

const entity::EntityMovement* WorldRuntime::find_entity_movement(EntityID entity_id) const noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::EntityMovement>(handle);
}

entity::EntityHealth* WorldRuntime::find_entity_health(EntityID entity_id) noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::EntityHealth>(handle);
}

const entity::EntityHealth* WorldRuntime::find_entity_health(EntityID entity_id) const noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::EntityHealth>(handle);
}

entity::BrainComponent* WorldRuntime::find_entity_brain(EntityID entity_id) noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::BrainComponent>(handle);
}

const entity::BrainComponent* WorldRuntime::find_entity_brain(EntityID entity_id) const noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::BrainComponent>(handle);
}

entity::MovementIntent* WorldRuntime::find_entity_movement_intent(EntityID entity_id) noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::MovementIntent>(handle);
}

const entity::MovementIntent* WorldRuntime::find_entity_movement_intent(EntityID entity_id) const noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::MovementIntent>(handle);
}

entity::EntityAnimationState* WorldRuntime::find_entity_animation_state(EntityID entity_id) noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::EntityAnimationState>(handle);
}

const entity::EntityAnimationState* WorldRuntime::find_entity_animation_state(EntityID entity_id) const noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::EntityAnimationState>(handle);
}

entity::DroppedItem* WorldRuntime::find_dropped_item(EntityID entity_id) noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::DroppedItem>(handle);
}

const entity::DroppedItem* WorldRuntime::find_dropped_item(EntityID entity_id) const noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<entity::DroppedItem>(handle);
}

bool WorldRuntime::set_entity_animation(EntityID entity_id, std::string animation_name) noexcept {
  const entt::entity handle = entity_handle(entity_id);
  if (handle == entt::null || !entity_registry_.valid(handle)) {
    return false;
  }
  auto& animation = entity_registry_.get_or_emplace<entity::EntityAnimationState>(handle);
  if (animation.name != animation_name) {
    animation.name = std::move(animation_name);
    animation.time_seconds = 0.0F;
  }
  return true;
}

bool WorldRuntime::set_dropped_item_stack(EntityID entity_id, player::ItemStack stack) noexcept {
  entity::DroppedItem* dropped = find_dropped_item(entity_id);
  if (dropped == nullptr) {
    return false;
  }
  dropped->stack = stack;
  return true;
}

bool WorldRuntime::entity_is_persistent(EntityID entity_id) const noexcept {
  const entt::entity handle = entity_handle(entity_id);
  return handle != entt::null && entity_registry_.valid(handle) &&
         entity_registry_.all_of<entity::PersistentEntity>(handle);
}

bool WorldRuntime::set_entity_transform(EntityID entity_id, Vec3f position, Quatf rotation) noexcept {
  entity::EntityTransform* transform = find_entity_transform(entity_id);
  if (transform == nullptr) {
    return false;
  }
  transform->previous_position = transform->position;
  transform->previous_rotation = transform->rotation;
  transform->position = position;
  transform->rotation = rotation;
  update_entity_chunk(entity_handle(entity_id));
  return true;
}

bool WorldRuntime::set_entity_position(EntityID entity_id, Vec3f position) noexcept {
  entity::EntityTransform* transform = find_entity_transform(entity_id);
  if (transform == nullptr) {
    return false;
  }
  return set_entity_transform(entity_id, position, transform->rotation);
}

std::vector<entity::EntitySnapshotEntry> WorldRuntime::collect_entity_snapshots() const {
  std::vector<entity::EntitySnapshotEntry> snapshots;
  const auto view = entity_registry_.view<
      entity::RuntimeEntity,
      entity::EntityType,
      entity::EntityTransform,
      entity::EntityVelocity,
      entity::NetworkEntity,
      entity::ChunkRelevance>();
  snapshots.reserve(view.size_hint());
  for (const entt::entity handle : view) {
    if (entity_registry_.all_of<entity::DroppedItem>(handle)) {
      continue;
    }
    const auto& runtime = view.get<entity::RuntimeEntity>(handle);
    const auto& type = view.get<entity::EntityType>(handle);
    const auto& transform = view.get<entity::EntityTransform>(handle);
    const auto& velocity = view.get<entity::EntityVelocity>(handle);
    const auto& network = view.get<entity::NetworkEntity>(handle);
    const auto& relevance = view.get<entity::ChunkRelevance>(handle);

    entity::EntitySnapshotEntry entry;
    entry.entity_id = runtime.entity_id;
    entry.net_id = network.net_id;
    entry.type_id = type.type_id;
    entry.type_name = type.type_name;
    entry.position = transform.position;
    entry.velocity = velocity.linear;
    entry.rotation = transform.rotation;
    entry.chunk = relevance.current_chunk;
    if (const auto* player_entity = entity_registry_.try_get<entity::PlayerEntity>(handle);
        player_entity != nullptr) {
      entry.owning_player = player_entity->player_id;
    }
    if (const auto* animation = entity_registry_.try_get<entity::EntityAnimationState>(handle);
        animation != nullptr) {
      entry.animation_name = animation->name;
      entry.animation_time_seconds = animation->time_seconds;
    }
    snapshots.emplace_back(std::move(entry));
  }
  return snapshots;
}

std::vector<entity::DroppedItemEntitySnapshot> WorldRuntime::collect_dropped_item_snapshots() const {
  std::vector<entity::DroppedItemEntitySnapshot> snapshots;
  const auto view = entity_registry_.view<
      entity::RuntimeEntity,
      entity::EntityTransform,
      entity::EntityVelocity,
      entity::NetworkEntity,
      entity::ChunkRelevance,
      entity::DroppedItem>();
  snapshots.reserve(view.size_hint());
  for (const entt::entity handle : view) {
    const auto& runtime = view.get<entity::RuntimeEntity>(handle);
    const auto& transform = view.get<entity::EntityTransform>(handle);
    const auto& velocity = view.get<entity::EntityVelocity>(handle);
    const auto& network = view.get<entity::NetworkEntity>(handle);
    const auto& relevance = view.get<entity::ChunkRelevance>(handle);
    const auto& dropped = view.get<entity::DroppedItem>(handle);
    if (dropped.stack.count == 0U) {
      continue;
    }

    snapshots.push_back({
        runtime.entity_id,
        network.net_id,
        id_,
        transform.position,
        velocity.linear,
        relevance.current_chunk,
        dropped.stack,
        dropped.pickup_delay_seconds});
  }
  return snapshots;
}

std::vector<entity::PersistentEntityRecord> WorldRuntime::persistent_entity_records() const {
  std::vector<entity::PersistentEntityRecord> records;
  const auto view = entity_registry_.view<
      entity::PersistentEntity,
      entity::EntityTransform,
      entity::EntityVelocity>();
  records.reserve(view.size_hint());
  for (const entt::entity handle : view) {
    if (entity_registry_.all_of<entity::PlayerEntity>(handle)) {
      continue;
    }
    const auto& persistent = view.get<entity::PersistentEntity>(handle);
    const auto& transform = view.get<entity::EntityTransform>(handle);
    const auto& velocity = view.get<entity::EntityVelocity>(handle);
    entity::PersistentEntityRecord record{
        persistent.uuid,
        persistent.type_name,
        id_,
        transform.position,
        transform.rotation,
        velocity.linear};
    if (const auto* dropped = entity_registry_.try_get<entity::DroppedItem>(handle);
        dropped != nullptr) {
      record.dropped_item_stack = dropped->stack;
      record.dropped_item_pickup_delay_seconds = dropped->pickup_delay_seconds;
    }
    records.push_back(std::move(record));
  }
  return records;
}

void WorldRuntime::restore_persistent_entities(
    std::span<const entity::PersistentEntityRecord> records,
    const entity::EntityRegistry* entity_registry) {
  for (const entity::PersistentEntityRecord& record : records) {
    if (record.world_id != id_) {
      continue;
    }
    EntityCreateInfo info;
    info.type_name = record.type_name;
    if (entity_registry != nullptr) {
      if (const std::optional<EntityTypeID> type_id = entity_registry->find_type(record.type_name);
          type_id.has_value()) {
        info.type_id = type_id.value();
        if (const entity::EntityTypeRecord* type = entity_registry->type(type_id.value());
            type != nullptr) {
          info.authored_components = type->components;
        }
      }
    }
    info.position = record.position;
    info.rotation = record.rotation;
    info.velocity = record.velocity;
    info.persistent = true;
    info.persistent_uuid = record.uuid;
    info.replicated = true;
    info.dropped_item_stack = record.dropped_item_stack;
    info.dropped_item_pickup_delay_seconds = record.dropped_item_pickup_delay_seconds;
    (void)create_entity(std::move(info));
  }
}

void WorldRuntime::apply_authored_components(
    entt::entity handle,
    const entity::EntityComponentSet& components) {
  if (handle == entt::null || !entity_registry_.valid(handle)) {
    return;
  }

  if (components.collision.has_value()) {
    entity_registry_.emplace_or_replace<entity::EntityCollision>(
        handle,
        components.collision->box);
  }
  if (components.movement.has_value()) {
    entity_registry_.emplace_or_replace<entity::EntityMovement>(
        handle,
        components.movement->walk_speed);
  }
  if (components.health.has_value()) {
    const f32 max_health = std::max(0.0F, components.health->max);
    entity_registry_.emplace_or_replace<entity::EntityHealth>(
        handle,
        max_health,
        max_health);
  }
  if (components.animation.has_value()) {
    entity_registry_.emplace_or_replace<entity::EntityAnimationState>(
        handle,
        components.animation->initial,
        0.0F);
  }
  if (components.brain.has_value()) {
    entity::BrainComponent brain;
    brain.behavior = components.brain->behavior;
    brain.tick_interval = std::max<u32>(1U, components.brain->tick_interval);
    brain.goals.reserve(components.brain->goals.size());
    for (const entity::AuthoredGoalDefinition& source : components.brain->goals) {
      entity::GoalSlot goal;
      goal.type = source.type;
      goal.priority = source.priority;
      goal.radius = source.radius;
      goal.speed_multiplier = source.speed_multiplier;
      goal.duration_ticks = std::max<u32>(1U, source.duration_ticks);
      goal.cooldown_ticks = source.cooldown_ticks;
      goal.range = source.range;
      goal.target = source.target;
      goal.animation = source.animation;
      brain.goals.push_back(std::move(goal));
    }
    std::stable_sort(
        brain.goals.begin(),
        brain.goals.end(),
        [](const entity::GoalSlot& lhs, const entity::GoalSlot& rhs) {
          return lhs.priority < rhs.priority;
        });
    if (!brain.goals.empty()) {
      entity_registry_.emplace_or_replace<entity::BrainComponent>(handle, std::move(brain));
      (void)entity_registry_.get_or_emplace<entity::EntityAnimationState>(handle);
    }
  }
}

u32 WorldRuntime::tick_brains() {
  u32 updated = 0;
  const auto view = entity_registry_.view<
      entity::RuntimeEntity,
      entity::EntityTransform,
      entity::BrainComponent>();
  for (const entt::entity handle : view) {
    const auto& runtime = view.get<entity::RuntimeEntity>(handle);
    const auto& transform = view.get<entity::EntityTransform>(handle);
    auto& brain = view.get<entity::BrainComponent>(handle);
    if (brain.goals.empty() || tick_index_ < brain.next_tick) {
      continue;
    }
    brain.next_tick = tick_index_ + std::max<u32>(1U, brain.tick_interval);

    const auto set_animation = [&](std::string_view animation_name) {
      auto& animation =
          entity_registry_.get_or_emplace<entity::EntityAnimationState>(handle);
      if (animation.name != animation_name) {
        animation.name = std::string{animation_name};
        animation.time_seconds = 0.0F;
      }
    };
    const auto clear_movement_intent = [&]() {
      if (auto* intent = entity_registry_.try_get<entity::MovementIntent>(handle);
          intent != nullptr) {
        intent->desired_direction = {};
        intent->speed_multiplier = 1.0F;
        intent->jump = false;
        intent->sprint = false;
      }
    };
    const auto goal_can_start = [&](const entity::GoalSlot& goal) {
      if (goal.state.cooldown_until > tick_index_) {
        return false;
      }
      if (is_float_in_fluid_goal(goal.type)) {
        const BlockPos pos{{
            static_cast<i32>(std::floor(transform.position.x)),
            static_cast<i32>(std::floor(transform.position.y)),
            static_cast<i32>(std::floor(transform.position.z)),
        }};
        return !get_fluid(pos).empty();
      }
      return is_idle_goal(goal.type) || is_wander_goal(goal.type);
    };
    const auto goal_can_continue = [&](const entity::GoalSlot& goal) {
      if (is_float_in_fluid_goal(goal.type)) {
        const BlockPos pos{{
            static_cast<i32>(std::floor(transform.position.x)),
            static_cast<i32>(std::floor(transform.position.y)),
            static_cast<i32>(std::floor(transform.position.z)),
        }};
        return !get_fluid(pos).empty();
      }
      return goal.state.remaining_ticks > 0U;
    };
    const auto start_goal = [&](usize index) {
      entity::GoalSlot& goal = brain.goals[index];
      goal.state.remaining_ticks = std::max<u32>(1U, goal.duration_ticks);
      if (brain.blackboard.home_position == Vec3f{}) {
        brain.blackboard.home_position = transform.position;
      }
      if (is_wander_goal(goal.type)) {
        goal.state.movement_direction =
            deterministic_wander_direction(runtime.entity_id, tick_index_, index);
      } else if (is_float_in_fluid_goal(goal.type)) {
        goal.state.movement_direction = {0.0F, 1.0F, 0.0F};
      } else {
        goal.state.movement_direction = {};
      }
      set_animation(goal.animation);
      brain.active_goal = index;
    };
    const auto stop_goal = [&](usize index, entity::GoalStopReason reason) {
      entity::GoalSlot& goal = brain.goals[index];
      clear_movement_intent();
      if (reason == entity::GoalStopReason::Completed && goal.cooldown_ticks > 0U) {
        goal.state.cooldown_until = tick_index_ + goal.cooldown_ticks;
      }
      goal.state.remaining_ticks = 0U;
      if (brain.active_goal.has_value() && brain.active_goal.value() == index) {
        brain.active_goal = std::nullopt;
      }
    };
    const auto tick_goal = [&](entity::GoalSlot& goal) {
      if (goal.state.remaining_ticks > 0U) {
        --goal.state.remaining_ticks;
      }
      if (is_wander_goal(goal.type) || is_float_in_fluid_goal(goal.type)) {
        auto& intent = entity_registry_.get_or_emplace<entity::MovementIntent>(handle);
        intent.desired_direction = goal.state.movement_direction;
        intent.speed_multiplier = std::max(0.0F, goal.speed_multiplier);
        intent.jump = is_float_in_fluid_goal(goal.type);
        intent.sprint = false;
      } else {
        clear_movement_intent();
      }
      set_animation(goal.animation);
    };

    if (brain.active_goal.has_value()) {
      const usize active = brain.active_goal.value();
      if (active >= brain.goals.size() || !goal_can_continue(brain.goals[active])) {
        if (active < brain.goals.size()) {
          stop_goal(active, entity::GoalStopReason::Completed);
        } else {
          brain.active_goal = std::nullopt;
        }
      }
    }

    std::optional<usize> candidate;
    for (usize index = 0; index < brain.goals.size(); ++index) {
      if (goal_can_start(brain.goals[index])) {
        candidate = index;
        break;
      }
    }
    if (!brain.active_goal.has_value() && candidate.has_value()) {
      start_goal(candidate.value());
    } else if (brain.active_goal.has_value() && candidate.has_value()) {
      const usize active = brain.active_goal.value();
      if (brain.goals[candidate.value()].priority < brain.goals[active].priority) {
        stop_goal(active, entity::GoalStopReason::Interrupted);
        start_goal(candidate.value());
      }
    }

    if (brain.active_goal.has_value()) {
      tick_goal(brain.goals[brain.active_goal.value()]);
    } else {
      clear_movement_intent();
      set_animation({});
    }
    ++updated;
  }
  return updated;
}

u32 WorldRuntime::tick_entity_physics(
    const voxel::BlockRegistry* block_registry,
    Seconds tick_seconds) {
  if (tick_seconds <= 0.0F) {
    return 0;
  }

  u32 updated = 0;
  if (block_registry == nullptr) {
    const auto view = entity_registry_.view<
        entity::EntityTransform,
        entity::EntityVelocity,
        entity::EntityMovement,
        entity::MovementIntent>();
    for (const entt::entity handle : view) {
      if (entity_registry_.all_of<entity::PlayerEntity>(handle)) {
        continue;
      }
      auto& transform = view.get<entity::EntityTransform>(handle);
      auto& velocity = view.get<entity::EntityVelocity>(handle);
      const auto& movement = view.get<entity::EntityMovement>(handle);
      const auto& intent = view.get<entity::MovementIntent>(handle);
      const Vec3f direction = normalize_or_zero(intent.desired_direction);
      const f32 speed = std::max(0.0F, movement.walk_speed) *
                        std::max(0.0F, intent.speed_multiplier);
      velocity.linear = direction * speed;
      if (length(velocity.linear) <= 0.0F) {
        continue;
      }
      transform.previous_position = transform.position;
      transform.previous_rotation = transform.rotation;
      transform.position = transform.position + velocity.linear * tick_seconds;
      if (has_horizontal_motion(velocity.linear)) {
        transform.rotation = yaw_rotation(yaw_from_horizontal_direction(velocity.linear));
      }
      update_entity_chunk(handle);
      ++updated;
    }
    return updated;
  }

  const WorldCollisionQuery collision_query(*this, *block_registry, true);
  const physics::KinematicConfig physics_config;
  const auto view = entity_registry_.view<
      entity::EntityTransform,
      entity::EntityVelocity,
      entity::EntityCollision>();
  for (const entt::entity handle : view) {
    if (entity_registry_.all_of<entity::PlayerEntity>(handle)) {
      continue;
    }
    auto& transform = view.get<entity::EntityTransform>(handle);
    auto& velocity = view.get<entity::EntityVelocity>(handle);
    const auto& collision = view.get<entity::EntityCollision>(handle);
    const entity::MovementIntent* intent =
        entity_registry_.try_get<entity::MovementIntent>(handle);
    const entity::EntityMovement* movement =
        entity_registry_.try_get<entity::EntityMovement>(handle);

    if (intent != nullptr && movement != nullptr) {
      const Vec3f direction = normalize_or_zero(intent->desired_direction);
      const f32 speed = std::max(0.0F, movement->walk_speed) *
                        std::max(0.0F, intent->speed_multiplier);
      velocity.linear.x = direction.x * speed;
      velocity.linear.z = direction.z * speed;
      if (direction.y > 0.0F) {
        velocity.linear.y = std::max(velocity.linear.y, direction.y * speed);
      }
    }

    const Aabb3f bounds = entity_body_bounds(transform.position, collision.box);
    const bool grounded = physics::has_support_below(
        collision_query,
        bounds,
        physics_config.collision_epsilon * 2.0F);
    if (!grounded) {
      velocity.linear.y = std::max(
          velocity.linear.y - physics_config.gravity_per_second * tick_seconds,
          -physics_config.terminal_velocity);
    } else if (velocity.linear.y < 0.0F) {
      velocity.linear.y = 0.0F;
    }

    const Vec3f movement_delta = velocity.linear * tick_seconds;
    const physics::MovementResolution resolved = physics::resolve_movement(
        collision_query,
        bounds,
        movement_delta,
        physics_config.collision_epsilon);

    transform.previous_position = transform.position;
    transform.previous_rotation = transform.rotation;
    transform.position = transform.position + resolved.applied_delta;
    if (resolved.hit_x) {
      velocity.linear.x = 0.0F;
    }
    if (resolved.hit_y) {
      velocity.linear.y = 0.0F;
    }
    if (resolved.hit_z) {
      velocity.linear.z = 0.0F;
    }

    const Vec3f facing_direction =
        has_horizontal_motion(resolved.applied_delta) ? resolved.applied_delta : velocity.linear;
    if (has_horizontal_motion(facing_direction)) {
      transform.rotation = yaw_rotation(yaw_from_horizontal_direction(facing_direction));
    }

    update_entity_chunk(handle);
    ++updated;
  }
  return updated;
}

u32 WorldRuntime::tick_entity_animations(Seconds tick_seconds) {
  if (tick_seconds <= 0.0F) {
    return 0;
  }

  u32 updated = 0;
  const auto view = entity_registry_.view<entity::EntityAnimationState>();
  for (const entt::entity handle : view) {
    auto& animation = view.get<entity::EntityAnimationState>(handle);
    if (animation.name.empty()) {
      animation.time_seconds = 0.0F;
      continue;
    }
    animation.time_seconds += tick_seconds;
    ++updated;
  }
  return updated;
}

u32 WorldRuntime::tick_entities(const voxel::BlockRegistry* block_registry) {
  flush_entity_despawns();
  u32 updated = 0;
  const Seconds tick_seconds = tick_duration_seconds(DefaultBaseTps);
  const auto player_view = entity_registry_.view<
      entity::RuntimeEntity,
      entity::EntityTransform,
      entity::EntityVelocity,
      entity::PlayerEntity,
      player::PlayerState>();
  for (const entt::entity handle : player_view) {
    auto& transform = player_view.get<entity::EntityTransform>(handle);
    auto& velocity = player_view.get<entity::EntityVelocity>(handle);
    const auto& player_state = player_view.get<player::PlayerState>(handle);
    if (transform.position != player_state.position) {
      transform.previous_position = transform.position;
      transform.position = player_state.position;
    }
    velocity.linear = player_state.velocity;
    update_entity_chunk(handle);
    ++updated;
  }

  updated += tick_brains();
  updated += tick_entity_physics(block_registry, tick_seconds);
  updated += tick_entity_animations(tick_seconds);

  const auto transform_view = entity_registry_.view<entity::EntityTransform, entity::ChunkRelevance>();
  for (const entt::entity handle : transform_view) {
    update_entity_chunk(handle);
  }
  return updated;
}

u32 WorldRuntime::tick_dropped_items(Seconds tick_seconds) {
  u32 updated = 0;
  if (tick_seconds <= 0.0F) {
    return updated;
  }

  const auto view = entity_registry_.view<entity::DroppedItem>();
  for (const entt::entity handle : view) {
    auto& dropped = view.get<entity::DroppedItem>(handle);
    if (dropped.pickup_delay_seconds <= 0.0F) {
      continue;
    }
    dropped.pickup_delay_seconds =
        std::max<Seconds>(0.0F, dropped.pickup_delay_seconds - tick_seconds);
    ++updated;
  }
  return updated;
}

player::PlayerState& WorldRuntime::ensure_player(PlayerID player_id) {
  if (const std::optional<EntityID> existing = entity_for_player(player_id);
      existing.has_value()) {
    const entt::entity handle = entity_handle(existing.value());
    return entity_registry_.get<player::PlayerState>(handle);
  }

  EntityCreateInfo info;
  info.type_id = entity::PlayerEntityTypeID;
  info.type_name = std::string{entity::PlayerEntityTypeName};
  info.position = {8.0F, 20.0F, -20.0F};
  info.persistent = true;
  info.persistent_uuid = "player:" + std::to_string(player_id.value);
  info.replicated = true;
  info.owning_player = player_id;
  const EntityID entity_id = create_entity(std::move(info));
  return entity_registry_.get<player::PlayerState>(entity_handle(entity_id));
}

player::PlayerState* WorldRuntime::find_player(PlayerID player_id) noexcept {
  const auto found = player_entities_.find(player_id);
  if (found == player_entities_.end()) {
    return nullptr;
  }
  const entt::entity handle = entity_handle(found->second);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<player::PlayerState>(handle);
}

const player::PlayerState* WorldRuntime::find_player(PlayerID player_id) const noexcept {
  const auto found = player_entities_.find(player_id);
  if (found == player_entities_.end()) {
    return nullptr;
  }
  const entt::entity handle = entity_handle(found->second);
  return handle == entt::null || !entity_registry_.valid(handle)
             ? nullptr
             : entity_registry_.try_get<player::PlayerState>(handle);
}

usize WorldRuntime::player_count() const noexcept {
  return player_entities_.size();
}

std::vector<PlayerID> WorldRuntime::player_ids() const {
  std::vector<PlayerID> ids;
  ids.reserve(player_entities_.size());
  for (const auto& [player_id, entity_id] : player_entities_) {
    (void)entity_id;
    ids.emplace_back(player_id);
  }
  return ids;
}

const PlayerRegionState* WorldRuntime::player_region_state(PlayerID player_id) const noexcept {
  const auto found = player_region_states_.find(player_id);
  return found == player_region_states_.end() ? nullptr : &found->second;
}

RegionMembershipChange WorldRuntime::update_player_region_state(
    PlayerID player_id,
    RegionQuery query) {
  PlayerRegionState& state = player_region_states_[player_id];
  return regions_.update_membership(state, query);
}

void WorldRuntime::schedule_block_update(
    BlockPos pos,
    TickIndex delay_ticks,
    BlockUpdateReason reason) {
  scheduled_block_updates_.emplace_back(pos, tick_index_ + delay_ticks, reason);
}

void WorldRuntime::enqueue_fluid_update(
    BlockPos pos,
    FluidUpdateReason reason,
    u32 delay_ticks) {
  enqueue_fluid_update_unique({pos, reason, tick_index_ + delay_ticks});
}

usize WorldRuntime::pending_fluid_update_count() const noexcept {
  return pending_fluid_updates_by_position_.size();
}

usize WorldRuntime::deferred_boundary_fluid_event_count(ChunkCoord coord) const noexcept {
  const auto found = deferred_boundary_fluid_updates_.find(coord);
  return found == deferred_boundary_fluid_updates_.end() ? 0U : found->second.size();
}

usize WorldRuntime::total_deferred_boundary_fluid_event_count() const noexcept {
  usize count = 0;
  for (const auto& [coord, updates] : deferred_boundary_fluid_updates_) {
    (void)coord;
    count += updates.size();
  }
  return count;
}

usize WorldRuntime::pending_block_update_count() const noexcept {
  return block_updates_.size() + scheduled_block_updates_.size();
}

bool WorldRuntime::has_lighting_work() const noexcept {
  return lighting_work_pending_;
}

LightingSnapshot WorldRuntime::make_lighting_snapshot() const {
  ARV_PROFILE_ZONE("Snapshot creation");
  LightingSnapshot snapshot = WorldLighting::make_snapshot(*this);
  last_lighting_snapshot_chunk_count_ = snapshot.pending_dirty_sections;
  return snapshot;
}

LightingResult WorldRuntime::build_lighting_result(
    const LightingSnapshot& snapshot,
    const voxel::BlockRegistry& registry) {
  return WorldLighting::build_result(snapshot, registry);
}

LightingPublishStats WorldRuntime::publish_lighting_result(LightingResult result) noexcept {
  const LightingPublishStats stats = WorldLighting::publish_result(*this, std::move(result));
  if (stats.changed_chunks > 0U || stats.remeshes_triggered > 0U) {
    mark_mesh_content_dirty();
  }
  recompute_lighting_work_pending();
  return stats;
}

u32 WorldRuntime::process_fluid_updates(
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    u32 budget) {
  return FluidSolver::process_updates(*this, registry, definition, budget);
}

std::optional<FluidSimulationRequest> WorldRuntime::make_fluid_simulation_request(
    u32 budget) {
  std::vector<FluidUpdate> due_updates = drain_due_fluid_updates(budget);
  if (budget == 0U || due_updates.empty()) {
    return std::nullopt;
  }

  FluidSimulationRequest request;
  request.current_tick = tick_index_;
  request.budget = budget;
  request.updates = std::move(due_updates);

  request.chunks.reserve(chunks_.size());
  for (const auto& [coord, chunk] : chunks_) {
    request.chunks.emplace_back(
        coord,
        chunk.lifecycle_state() == voxel::ChunkLifecycleState::Active,
        chunk.content_version(),
        chunk.storage_view());
  }

  return request;
}

u32 WorldRuntime::apply_fluid_simulation_result(
    FluidSimulationResult result,
    const voxel::FluidDefinition& definition) {
  HashMap<ChunkCoord, bool> accepted_chunks;
  for (const FluidChange& change : result.changes) {
    const ChunkCoord coord = block_to_chunk(change.pos);
    auto [accepted, inserted] = accepted_chunks.emplace(coord, false);
    if (inserted) {
      const voxel::ChunkSection* chunk = find_chunk(coord);
      accepted->second =
          chunk != nullptr && chunk->content_version() == change.source_content_version;
    }

    if (!accepted->second) {
      enqueue_fluid_update(change.pos, FluidUpdateReason::NeighborChanged);
      continue;
    }

    (void)set_fluid_internal(change.pos, change.fluid, definition, false);
  }

  for (const FluidUpdate& update : result.followup_updates) {
    enqueue_fluid_update_unique(update);
  }

  for (const FluidUpdate& update : result.deferred_updates) {
    const ChunkCoord coord = block_to_chunk(update.pos);
    if (is_chunk_active(coord)) {
      enqueue_fluid_update_unique(update);
    } else {
      FluidSolver::store_deferred_boundary_update(*this, coord, update);
    }
  }

  return result.processed;
}

WorldTickStats WorldRuntime::tick(
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& fluid_definition) {
  ARV_PROFILE_ZONE("Simulation tick");
  ++tick_index_;

  WorldTickStats stats;
  stats.entity_updates_processed = tick_entities(&registry);
  stats.scheduled_ticks_processed = process_due_scheduled_ticks();

  if (tick_policy_.block_update_interval == 0U ||
      tick_index_ % tick_policy_.block_update_interval == 0U) {
    stats.block_updates_processed = drain_block_updates(tick_policy_.block_update_budget);
  }

  if (tick_policy_.fluid_interval == 0U || tick_index_ % tick_policy_.fluid_interval == 0U) {
    stats.fluid_updates_processed =
        process_fluid_updates(registry, fluid_definition, tick_policy_.fluid_budget);
  }

  return stats;
}

std::vector<ReplicatedBlockUpdate> WorldRuntime::drain_replicated_block_updates() {
  std::vector<ReplicatedBlockUpdate> drained = std::move(replicated_block_updates_);
  replicated_block_updates_.clear();
  return drained;
}

std::vector<ChunkCoord> WorldRuntime::dirty_chunks(voxel::ChunkDirtyFlags flag) const {
  std::vector<ChunkCoord> coords;
  for (const auto& [coord, chunk] : chunks_) {
    if (chunk.is_dirty(flag)) {
      coords.emplace_back(coord);
    }
  }
  return coords;
}

std::vector<ChunkCoord> WorldRuntime::consume_dirty_mesh_chunks(
    voxel::ChunkDirtyFlags flag) {
  std::vector<ChunkCoord> coords;
  coords.reserve(dirty_mesh_chunks_.size());
  while (!dirty_mesh_chunks_.empty()) {
    const ChunkCoord coord = dirty_mesh_chunks_.front();
    dirty_mesh_chunks_.pop_front();
    dirty_mesh_chunk_set_.erase(coord);

    const voxel::ChunkSection* chunk = find_chunk(coord);
    if (chunk != nullptr && chunk->is_dirty(flag)) {
      coords.emplace_back(coord);
    }
  }
  return coords;
}

i32 WorldRuntime::abs_i32(i32 value) noexcept {
  return value < 0 ? -value : value;
}

i32 WorldRuntime::max_abs3(i32 x, i32 y, i32 z) noexcept {
  const i32 xy = abs_i32(x) > abs_i32(y) ? abs_i32(x) : abs_i32(y);
  return xy > abs_i32(z) ? xy : abs_i32(z);
}

BlockPos WorldRuntime::offset_block(BlockPos pos, Vec3i offset) noexcept {
  return {{pos.value.x + offset.x, pos.value.y + offset.y, pos.value.z + offset.z}};
}

usize WorldRuntime::lifecycle_index(voxel::ChunkLifecycleState state) noexcept {
  switch (state) {
    case voxel::ChunkLifecycleState::Unloaded:
      return 0U;
    case voxel::ChunkLifecycleState::Requested:
      return 1U;
    case voxel::ChunkLifecycleState::Loading:
      return 2U;
    case voxel::ChunkLifecycleState::Generating:
      return 3U;
    case voxel::ChunkLifecycleState::Populating:
      return 4U;
    case voxel::ChunkLifecycleState::Loaded:
      return 5U;
    case voxel::ChunkLifecycleState::Active:
      return 6U;
    case voxel::ChunkLifecycleState::MarkedForUnload:
      return 7U;
    case voxel::ChunkLifecycleState::Saving:
      return 8U;
  }
  return 0U;
}

usize WorldRuntime::lifecycle_count(voxel::ChunkLifecycleState state) const noexcept {
  return lifecycle_counts_[lifecycle_index(state)];
}

void WorldRuntime::set_chunk_lifecycle_state_internal(
    voxel::ChunkSection& chunk,
    voxel::ChunkLifecycleState state) noexcept {
  const voxel::ChunkLifecycleState previous = chunk.lifecycle_state();
  if (previous == state) {
    return;
  }

  usize& previous_count = lifecycle_counts_[lifecycle_index(previous)];
  if (previous_count > 0U) {
    --previous_count;
  }
  ++lifecycle_counts_[lifecycle_index(state)];

  if (previous == voxel::ChunkLifecycleState::Active && active_chunk_count_ > 0U) {
    --active_chunk_count_;
  }
  if (state == voxel::ChunkLifecycleState::Active) {
    ++active_chunk_count_;
  }

  chunk.set_lifecycle_state(state);
}

void WorldRuntime::mark_lighting_content_dirty() noexcept {
  ++lighting_revision_;
  lighting_work_pending_ = true;
  last_lighting_snapshot_chunk_count_ = std::max<usize>(last_lighting_snapshot_chunk_count_, 1U);
}

void WorldRuntime::mark_mesh_content_dirty() noexcept {
  ++mesh_revision_;
}

void WorldRuntime::enqueue_dirty_mesh_chunk(ChunkCoord coord) {
  if (dirty_mesh_chunk_set_.insert(coord).second) {
    dirty_mesh_chunks_.emplace_back(coord);
  }
}

void WorldRuntime::enqueue_fluid_update_unique(FluidUpdate update) {
  auto [pending, inserted] =
      pending_fluid_updates_by_position_.try_emplace(update.pos, update);
  if (inserted) {
    fluid_updates_.emplace_back(update);
    return;
  }

  if (!should_replace_pending_fluid_update(pending->second, update)) {
    return;
  }

  pending->second = update;
  fluid_updates_.emplace_back(update);
}

std::vector<FluidUpdate> WorldRuntime::drain_due_fluid_updates(u32 budget) {
  std::vector<FluidUpdate> due;
  if (budget == 0U || fluid_updates_.empty()) {
    return due;
  }

  due.reserve(std::min<usize>(budget, fluid_updates_.size()));
  std::deque<FluidUpdate> remaining;
  while (!fluid_updates_.empty()) {
    FluidUpdate update = fluid_updates_.front();
    fluid_updates_.pop_front();

    auto pending = pending_fluid_updates_by_position_.find(update.pos);
    if (pending == pending_fluid_updates_by_position_.end() ||
        !matches_pending_fluid_update(pending->second, update)) {
      continue;
    }

    if (update.execute_at > tick_index_ || due.size() >= budget) {
      remaining.emplace_back(update);
      continue;
    }

    pending_fluid_updates_by_position_.erase(pending);
    due.emplace_back(update);
  }

  fluid_updates_ = std::move(remaining);
  return due;
}

void WorldRuntime::recompute_lighting_work_pending() noexcept {
  lighting_work_pending_ = WorldLighting::has_work(*this);
}

void WorldRuntime::enqueue_neighbor_block_updates(BlockPos pos) {
  for (const Vec3i offset : NeighborOffsets) {
    block_updates_.emplace_back(offset_block(pos, offset), BlockUpdateReason::NeighborChanged);
  }
}

void WorldRuntime::mark_boundary_neighbors_dirty(ChunkCoord coord, SectionLocalPos local) noexcept {
  const i32 min_x = local.value.x == 0 ? -1 : 0;
  const i32 max_x = local.value.x == ChunkSize - 1 ? 1 : 0;
  const i32 min_y = local.value.y == 0 ? -1 : 0;
  const i32 max_y = local.value.y == ChunkSize - 1 ? 1 : 0;
  const i32 min_z = local.value.z == 0 ? -1 : 0;
  const i32 max_z = local.value.z == ChunkSize - 1 ? 1 : 0;

  for (i32 z = min_z; z <= max_z; ++z) {
    for (i32 y = min_y; y <= max_y; ++y) {
      for (i32 x = min_x; x <= max_x; ++x) {
        if (x == 0 && y == 0 && z == 0) {
          continue;
        }

        const ChunkCoord neighbor{{
            coord.value.x + x,
            coord.value.y + y,
            coord.value.z + z,
        }};
        const int axis_count = static_cast<int>(x != 0) + static_cast<int>(y != 0) + static_cast<int>(z != 0);
        if (axis_count == 1) {
          mark_neighbor_mesh_dependency_dirty(neighbor);
        } else {
          mark_neighbor_mesh_only_dependency_dirty(neighbor);
        }
      }
    }
  }
}

void WorldRuntime::mark_loaded_chunk_neighbors_dirty(ChunkCoord coord) noexcept {
  for (i32 z = -1; z <= 1; ++z) {
    for (i32 y = -1; y <= 1; ++y) {
      for (i32 x = -1; x <= 1; ++x) {
        if (x == 0 && y == 0 && z == 0) {
          continue;
        }

        const ChunkCoord neighbor{{
            coord.value.x + x,
            coord.value.y + y,
            coord.value.z + z,
        }};
        const int axis_count =
            static_cast<int>(x != 0) + static_cast<int>(y != 0) + static_cast<int>(z != 0);
        if (axis_count == 1) {
          mark_neighbor_mesh_dependency_dirty(neighbor);
        } else {
          mark_neighbor_mesh_only_dependency_dirty(neighbor);
        }
      }
    }
  }
}

void WorldRuntime::mark_light_dependency_neighbors_dirty(ChunkCoord coord) noexcept {
  for (const Vec3i& offset : NeighborOffsets) {
    mark_neighbor_light_dependency_dirty(
        {{
            coord.value.x + offset.x,
            coord.value.y + offset.y,
            coord.value.z + offset.z,
        }});
  }
}

void WorldRuntime::mark_neighbor_mesh_dependency_dirty(ChunkCoord coord) noexcept {
  voxel::ChunkSection* neighbor = find_chunk(coord);
  if (neighbor != nullptr) {
    neighbor->mark_mesh_dependency_dirty();
    enqueue_dirty_mesh_chunk(coord);
    neighbor->mark_dirty(voxel::ChunkDirtyFlags::Lighting | voxel::ChunkDirtyFlags::NeedsFluidStep);
    mark_mesh_content_dirty();
    lighting_work_pending_ = true;
  }
}

void WorldRuntime::mark_neighbor_mesh_only_dependency_dirty(ChunkCoord coord) noexcept {
  voxel::ChunkSection* neighbor = find_chunk(coord);
  if (neighbor != nullptr) {
    neighbor->mark_mesh_dependency_dirty();
    enqueue_dirty_mesh_chunk(coord);
    mark_mesh_content_dirty();
  }
}

void WorldRuntime::mark_neighbor_light_dependency_dirty(ChunkCoord coord) noexcept {
  voxel::ChunkSection* neighbor = find_chunk(coord);
  if (neighbor != nullptr) {
    neighbor->mark_light_dependency_dirty();
    enqueue_dirty_mesh_chunk(coord);
    mark_mesh_content_dirty();
  }
}

u32 WorldRuntime::process_due_scheduled_ticks() {
  std::vector<ScheduledBlockUpdate> remaining;
  remaining.reserve(scheduled_block_updates_.size());

  u32 processed = 0;
  for (const ScheduledBlockUpdate& update : scheduled_block_updates_) {
    if (update.execute_at <= tick_index_) {
      block_updates_.emplace_back(update.pos, update.reason);
      ++processed;
    } else {
      remaining.emplace_back(update);
    }
  }

  scheduled_block_updates_ = std::move(remaining);
  return processed;
}

u32 WorldRuntime::drain_block_updates(u32 budget) {
  u32 processed = 0;
  while (processed < budget && !block_updates_.empty()) {
    block_updates_.pop_front();
    ++processed;
  }
  return processed;
}

entt::entity WorldRuntime::entity_handle(EntityID entity_id) const noexcept {
  const auto found = entity_handles_.find(entity_id);
  return found == entity_handles_.end() ? entt::null : found->second;
}

ChunkCoord WorldRuntime::chunk_for_position(Vec3f position) const noexcept {
  return block_to_chunk({{
      static_cast<i32>(std::floor(position.x)),
      static_cast<i32>(std::floor(position.y)),
      static_cast<i32>(std::floor(position.z)),
  }});
}

void WorldRuntime::update_entity_chunk(entt::entity handle) noexcept {
  if (handle == entt::null || !entity_registry_.valid(handle)) {
    return;
  }
  auto* transform = entity_registry_.try_get<entity::EntityTransform>(handle);
  auto* relevance = entity_registry_.try_get<entity::ChunkRelevance>(handle);
  if (transform == nullptr || relevance == nullptr) {
    return;
  }
  relevance->current_chunk = chunk_for_position(transform->position);
}

void WorldRuntime::record_replicated_block_update(
    BlockPos pos,
    voxel::BlockVariantID new_state) {
  replicated_block_updates_.push_back({pos, new_state, tick_index_});
}

} // namespace arv::world
