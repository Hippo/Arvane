#include "arv/world/world_runtime.hpp"

#include "arv/core/profiling.hpp"

#include <algorithm>
#include <array>
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
    std::optional<std::array<voxel::FluidCell, voxel::ChunkVolume>> fluids) noexcept {
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

  std::array<voxel::LightWord, voxel::ChunkVolume> resolved_lights;
  if (lights.has_value()) {
    resolved_lights = std::move(lights.value());
  } else {
    resolved_lights.fill(voxel::pack_light(0, 0, 0, voxel::MaxLightLevel));
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
  chunk.set_lifecycle_state(voxel::ChunkLifecycleState::Loaded);
  chunk.replace_saved_contents(
      std::move(blocks),
      std::move(resolved_lights),
      std::move(fluids));

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
  if (chunks_.erase(coord) == 0U) {
    return false;
  }

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
  usize count = 0;
  for (const auto& [coord, chunk] : chunks_) {
    (void)coord;
    if (chunk.lifecycle_state() == voxel::ChunkLifecycleState::Active) {
      ++count;
    }
  }
  return count;
}

bool WorldRuntime::set_chunk_lifecycle_state(ChunkCoord coord, voxel::ChunkLifecycleState state) noexcept {
  voxel::ChunkSection* chunk = find_chunk(coord);
  if (chunk == nullptr) {
    return false;
  }
  chunk->set_lifecycle_state(state);
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
      chunk.set_lifecycle_state(next_state);
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
  const voxel::ChunkSection* chunk = find_chunk(coord);
  if (chunk == nullptr) {
    return std::nullopt;
  }

  voxel::ChunkSnapshot snapshot = voxel::make_chunk_snapshot_without_neighbors(*chunk);
  // TODO(arvane): Narrow meshing neighbor inputs to compact immutable boundary/edge views
  // if retaining whole neighboring chunk payload handles becomes a measurable cost.
  {
    ARV_PROFILE_ZONE("Chunk lookup / access");
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
  chunk.set_lifecycle_state(voxel::ChunkLifecycleState::Loaded);
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

player::PlayerState& WorldRuntime::ensure_player(PlayerID player_id) {
  auto [iter, inserted] = players_.try_emplace(player_id);
  if (inserted) {
    iter->second.player_id = player_id;
    iter->second.world_id = id_;
  }
  return iter->second;
}

player::PlayerState* WorldRuntime::find_player(PlayerID player_id) noexcept {
  const auto found = players_.find(player_id);
  return found == players_.end() ? nullptr : &found->second;
}

const player::PlayerState* WorldRuntime::find_player(PlayerID player_id) const noexcept {
  const auto found = players_.find(player_id);
  return found == players_.end() ? nullptr : &found->second;
}

usize WorldRuntime::player_count() const noexcept {
  return players_.size();
}

std::vector<PlayerID> WorldRuntime::player_ids() const {
  std::vector<PlayerID> ids;
  ids.reserve(players_.size());
  for (const auto& [player_id, player] : players_) {
    (void)player;
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
  return WorldLighting::make_snapshot(*this);
}

LightingResult WorldRuntime::build_lighting_result(
    const LightingSnapshot& snapshot,
    const voxel::BlockRegistry& registry) {
  return WorldLighting::build_result(snapshot, registry);
}

LightingPublishStats WorldRuntime::publish_lighting_result(LightingResult result) noexcept {
  const LightingPublishStats stats = WorldLighting::publish_result(*this, std::move(result));
  if (stats.changed_chunks > 0U) {
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

void WorldRuntime::mark_lighting_content_dirty() noexcept {
  ++lighting_revision_;
  lighting_work_pending_ = true;
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

} // namespace arv::world
