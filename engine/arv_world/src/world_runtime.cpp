#include "arv/world/world_runtime.hpp"

#include <utility>

namespace arv::world {

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
  }
  return iter->second;
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

TickIndex WorldRuntime::tick_index() const noexcept {
  return tick_index_;
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
    enqueue_chunk_activation_updates(coord);
  }
  return true;
}

void WorldRuntime::update_simulation_radius(ChunkCoord center, i32 radius) noexcept {
  for (auto& [coord, chunk] : chunks_) {
    const i32 dx = coord.value.x - center.value.x;
    const i32 dy = coord.value.y - center.value.y;
    const i32 dz = coord.value.z - center.value.z;
    const i32 distance = max_abs3(dx, dy, dz);
    const bool active = radius >= 0 && distance <= radius;
    const voxel::ChunkLifecycleState next_state =
        active ? voxel::ChunkLifecycleState::Active : voxel::ChunkLifecycleState::Loaded;
    const bool became_active = active &&
                               chunk.lifecycle_state() != voxel::ChunkLifecycleState::Active;
    if (chunk.lifecycle_state() != next_state) {
      chunk.set_lifecycle_state(next_state);
    }
    if (became_active) {
      enqueue_chunk_activation_updates(coord);
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

  mark_boundary_neighbors_dirty(chunk_coord, local);
  enqueue_neighbor_block_updates(pos);
  enqueue_neighbor_fluid_updates(pos, FluidUpdateReason::BlockChanged);
  return true;
}

bool WorldRuntime::set_fluid(
    BlockPos pos,
    voxel::FluidCell fluid,
    const voxel::FluidDefinition& definition) noexcept {
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

  mark_boundary_neighbors_dirty(chunk_coord, local);
  enqueue_neighbor_fluid_updates(
      pos,
      fluid.empty() ? FluidUpdateReason::FluidRemoved : FluidUpdateReason::FluidAdded);
  return true;
}

bool WorldRuntime::place_fluid_source(BlockPos pos, const voxel::FluidDefinition& definition) noexcept {
  return set_fluid(pos, voxel::make_fluid_source(definition), definition);
}

std::optional<voxel::ChunkSnapshot> WorldRuntime::make_meshing_snapshot(
    ChunkCoord coord,
    MissingNeighborPolicy missing_neighbor_policy) const {
  const voxel::ChunkSection* chunk = find_chunk(coord);
  if (chunk == nullptr) {
    return std::nullopt;
  }

  voxel::ChunkSnapshot snapshot = voxel::make_chunk_snapshot_without_neighbors(*chunk);
  for (voxel::BlockFace face : AllFaces) {
    const ChunkCoord neighbor_coord = neighbor_chunk(coord, voxel::axis_direction_from_face(face));
    const voxel::ChunkSection* neighbor = find_chunk(neighbor_coord);
    if (neighbor == nullptr) {
      if (missing_neighbor_policy == MissingNeighborPolicy::RequireLoaded) {
        return std::nullopt;
      }
      snapshot.neighbors[voxel::face_index(face)] = voxel::air_boundary_snapshot();
      continue;
    }
    snapshot.neighbors[voxel::face_index(face)] = copy_neighbor_boundary(*neighbor, face);
  }

  std::array<const voxel::ChunkSection*, 27> halo_chunks{};
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
        halo_chunks[static_cast<usize>((ox + 1) + 3 * ((oy + 1) + 3 * (oz + 1)))] = neighbor;
      }
    }
  }

  for (i32 z = -1; z <= ChunkSize; ++z) {
    for (i32 y = -1; y <= ChunkSize; ++y) {
      for (i32 x = -1; x <= ChunkSize; ++x) {
        const SectionLocalPos halo_local{{x, y, z}};
        if (voxel::is_valid_local(halo_local)) {
          continue;
        }

        const i32 ox = x < 0 ? -1 : (x >= ChunkSize ? 1 : 0);
        const i32 oy = y < 0 ? -1 : (y >= ChunkSize ? 1 : 0);
        const i32 oz = z < 0 ? -1 : (z >= ChunkSize ? 1 : 0);
        const voxel::ChunkSection* neighbor =
            halo_chunks[static_cast<usize>((ox + 1) + 3 * ((oy + 1) + 3 * (oz + 1)))];
        if (neighbor == nullptr) {
          continue;
        }

        const SectionLocalPos neighbor_local{{
            x < 0 ? ChunkSize - 1 : (x >= ChunkSize ? 0 : x),
            y < 0 ? ChunkSize - 1 : (y >= ChunkSize ? 0 : y),
            z < 0 ? ChunkSize - 1 : (z >= ChunkSize ? 0 : z),
        }};
        snapshot.halo_blocks[voxel::halo_index(halo_local)] = neighbor->block(neighbor_local);
        snapshot.halo_lights[voxel::halo_index(halo_local)] = neighbor->light(neighbor_local);
      }
    }
  }

  return snapshot;
}

bool WorldRuntime::validate_mesh_result(ChunkCoord coord, u64 source_version) const noexcept {
  const voxel::ChunkSection* chunk = find_chunk(coord);
  return chunk != nullptr && chunk->version() == source_version;
}

bool WorldRuntime::publish_mesh_result(ChunkCoord coord, u64 source_version) noexcept {
  voxel::ChunkSection* chunk = find_chunk(coord);
  if (chunk == nullptr || chunk->version() != source_version) {
    return false;
  }
  chunk->mark_mesh_ready(source_version);
  return true;
}

bool WorldRuntime::publish_generated_chunk(GeneratedChunk generated) noexcept {
  if (generated.world_id != id_) {
    return false;
  }

  voxel::ChunkSection& chunk = ensure_chunk(
      generated.coord,
      voxel::ChunkLifecycleState::Loaded);
  chunk.replace_generated_blocks(std::move(generated.blocks));
  return true;
}

void WorldRuntime::schedule_block_update(
    BlockPos pos,
    TickIndex delay_ticks,
    BlockUpdateReason reason) {
  scheduled_block_updates_.push_back({pos, tick_index_ + delay_ticks, reason});
}

void WorldRuntime::enqueue_fluid_update(
    BlockPos pos,
    FluidUpdateReason reason) {
  fluid_updates_.push_back({pos, reason});
}

usize WorldRuntime::pending_fluid_update_count() const noexcept {
  return fluid_updates_.size();
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

bool WorldRuntime::process_lighting(const voxel::BlockRegistry& registry) {
  bool needs_lighting = false;
  for (const auto& [coord, chunk] : chunks_) {
    (void)coord;
    if (chunk.is_dirty(voxel::ChunkDirtyFlags::NeedsRelight)) {
      needs_lighting = true;
      break;
    }
  }

  if (!needs_lighting) {
    return false;
  }

  rebuild_loaded_lighting(registry);
  for (auto& [coord, chunk] : chunks_) {
    (void)coord;
    chunk.clear_dirty(voxel::ChunkDirtyFlags::NeedsRelight);
  }
  return true;
}

bool WorldRuntime::has_lighting_work() const noexcept {
  for (const auto& [coord, chunk] : chunks_) {
    (void)coord;
    if (chunk.is_dirty(voxel::ChunkDirtyFlags::NeedsRelight)) {
      return true;
    }
  }
  return false;
}

LightingSnapshot WorldRuntime::make_lighting_snapshot() const {
  LightingSnapshot snapshot;
  snapshot.world_id = id_;
  snapshot.chunks.reserve(chunks_.size());
  for (const auto& [coord, chunk] : chunks_) {
    snapshot.chunks.push_back({coord, chunk.version(), chunk.blocks()});
  }
  return snapshot;
}

LightingResult WorldRuntime::build_lighting_result(
    const LightingSnapshot& snapshot,
    const voxel::BlockRegistry& registry) {
  using LightArray = std::array<voxel::LightWord, voxel::ChunkVolume>;

  LightingResult result;
  result.world_id = snapshot.world_id;
  result.chunks.reserve(snapshot.chunks.size());

  std::unordered_map<ChunkCoord, LightArray> buffers;
  std::unordered_map<ChunkCoord, const ChunkLightingSnapshot*> chunks;
  buffers.reserve(snapshot.chunks.size());
  chunks.reserve(snapshot.chunks.size());
  for (const ChunkLightingSnapshot& chunk : snapshot.chunks) {
    LightArray buffer{};
    buffer.fill(voxel::NoLight);
    buffers.emplace(chunk.coord, buffer);
    chunks.emplace(chunk.coord, &chunk);
  }

  auto find_snapshot_chunk = [&chunks](ChunkCoord coord) -> const ChunkLightingSnapshot* {
    const auto found = chunks.find(coord);
    return found == chunks.end() ? nullptr : found->second;
  };

  auto can_light_enter_snapshot =
      [&registry, &find_snapshot_chunk](BlockPos pos) -> bool {
    const ChunkLightingSnapshot* chunk = find_snapshot_chunk(block_to_chunk(pos));
    if (chunk == nullptr) {
      return false;
    }

    const voxel::BlockVariantRecord* variant =
        registry.variant(chunk->blocks[voxel::local_index(block_to_local(pos))]);
    return variant != nullptr && !variant->properties.blocks_light;
  };

  auto set_sky = [&buffers](BlockPos pos, u8 value) {
    const ChunkCoord coord = block_to_chunk(pos);
    auto found = buffers.find(coord);
    if (found == buffers.end()) {
      return false;
    }

    voxel::LightWord& light = found->second[voxel::local_index(block_to_local(pos))];
    if (value <= voxel::light_sky(light)) {
      return false;
    }

    light = voxel::with_sky_light(light, value);
    return true;
  };

  auto set_rgb = [&buffers](BlockPos pos, u8 r, u8 g, u8 b) {
    const ChunkCoord coord = block_to_chunk(pos);
    auto found = buffers.find(coord);
    if (found == buffers.end()) {
      return false;
    }

    voxel::LightWord& light = found->second[voxel::local_index(block_to_local(pos))];
    const u8 next_r = r > voxel::light_r(light) ? r : voxel::light_r(light);
    const u8 next_g = g > voxel::light_g(light) ? g : voxel::light_g(light);
    const u8 next_b = b > voxel::light_b(light) ? b : voxel::light_b(light);
    if (next_r == voxel::light_r(light) && next_g == voxel::light_g(light) &&
        next_b == voxel::light_b(light)) {
      return false;
    }

    light = voxel::pack_light(next_r, next_g, next_b, voxel::light_sky(light));
    return true;
  };

  std::unordered_map<ChunkColumnCoord, i32> top_by_column;
  std::unordered_map<ChunkColumnCoord, i32> bottom_by_column;
  for (const ChunkLightingSnapshot& chunk : snapshot.chunks) {
    for (i32 z = 0; z < ChunkSize; ++z) {
      for (i32 x = 0; x < ChunkSize; ++x) {
        const i32 world_x = chunk.coord.value.x * ChunkSize + x;
        const i32 world_z = chunk.coord.value.z * ChunkSize + z;
        const i32 top_y = chunk.coord.value.y * ChunkSize + (ChunkSize - 1);
        const i32 bottom_y = chunk.coord.value.y * ChunkSize;
        const ChunkColumnCoord key{world_x, world_z};
        auto top = top_by_column.find(key);
        if (top == top_by_column.end() || top_y > top->second) {
          top_by_column[key] = top_y;
        }
        auto bottom = bottom_by_column.find(key);
        if (bottom == bottom_by_column.end() || bottom_y < bottom->second) {
          bottom_by_column[key] = bottom_y;
        }
      }
    }
  }

  std::queue<std::pair<BlockPos, u8>> sky_queue;
  for (const auto& [key, top_y] : top_by_column) {
    const i32 world_x = key.x;
    const i32 world_z = key.z;
    const i32 bottom_y = bottom_by_column[key];
    for (i32 world_y = top_y; world_y >= bottom_y; --world_y) {
      const BlockPos pos{{world_x, world_y, world_z}};
      if (!can_light_enter_snapshot(pos)) {
        break;
      }
      if (set_sky(pos, voxel::MaxLightLevel)) {
        sky_queue.push({pos, voxel::MaxLightLevel});
      }
    }
  }

  while (!sky_queue.empty()) {
    const auto [pos, level] = sky_queue.front();
    sky_queue.pop();
    if (level <= 1U) {
      continue;
    }

    const u8 next = static_cast<u8>(level - 1U);
    for (const Vec3i offset : NeighborOffsets) {
      const BlockPos neighbor = offset_block(pos, offset);
      if (!can_light_enter_snapshot(neighbor)) {
        continue;
      }
      if (set_sky(neighbor, next)) {
        sky_queue.push({neighbor, next});
      }
    }
  }

  struct RgbNode {
    BlockPos pos{};
    u8 r{};
    u8 g{};
    u8 b{};
  };

  std::queue<RgbNode> rgb_queue;
  for (const ChunkLightingSnapshot& chunk : snapshot.chunks) {
    for (i32 z = 0; z < ChunkSize; ++z) {
      for (i32 y = 0; y < ChunkSize; ++y) {
        for (i32 x = 0; x < ChunkSize; ++x) {
          const SectionLocalPos local{{x, y, z}};
          const voxel::BlockVariantRecord* variant =
              registry.variant(chunk.blocks[voxel::local_index(local)]);
          if (variant == nullptr || !variant->properties.emits_light) {
            continue;
          }

          const BlockPos pos = chunk_local_to_block(chunk.coord, local);
          const u8 r = voxel::clamp_light_channel(variant->properties.emission_r);
          const u8 g = voxel::clamp_light_channel(variant->properties.emission_g);
          const u8 b = voxel::clamp_light_channel(variant->properties.emission_b);
          if (set_rgb(pos, r, g, b)) {
            rgb_queue.push({pos, r, g, b});
          }
        }
      }
    }
  }

  while (!rgb_queue.empty()) {
    const RgbNode node = rgb_queue.front();
    rgb_queue.pop();

    const u8 next_r = node.r > 0 ? static_cast<u8>(node.r - 1U) : 0;
    const u8 next_g = node.g > 0 ? static_cast<u8>(node.g - 1U) : 0;
    const u8 next_b = node.b > 0 ? static_cast<u8>(node.b - 1U) : 0;
    if (next_r == 0 && next_g == 0 && next_b == 0) {
      continue;
    }

    for (const Vec3i offset : NeighborOffsets) {
      const BlockPos neighbor = offset_block(node.pos, offset);
      if (!can_light_enter_snapshot(neighbor)) {
        continue;
      }
      if (set_rgb(neighbor, next_r, next_g, next_b)) {
        rgb_queue.push({neighbor, next_r, next_g, next_b});
      }
    }
  }

  for (const ChunkLightingSnapshot& chunk : snapshot.chunks) {
    auto found = buffers.find(chunk.coord);
    if (found != buffers.end()) {
      result.chunks.push_back({chunk.coord, chunk.source_version, std::move(found->second)});
    }
  }
  return result;
}

LightingPublishStats WorldRuntime::publish_lighting_result(LightingResult result) noexcept {
  LightingPublishStats stats;
  if (result.world_id != id_) {
    stats.stale_chunks = static_cast<u32>(result.chunks.size());
    return stats;
  }

  for (ChunkLightingResult& chunk_result : result.chunks) {
    voxel::ChunkSection* chunk = find_chunk(chunk_result.coord);
    if (chunk == nullptr || chunk->version() != chunk_result.source_version) {
      ++stats.stale_chunks;
      continue;
    }

    if (chunk->replace_lights(std::move(chunk_result.lights))) {
      ++stats.changed_chunks;
    }
    chunk->clear_dirty(voxel::ChunkDirtyFlags::NeedsRelight);
    ++stats.accepted_chunks;
  }
  return stats;
}

u32 WorldRuntime::process_fluid_updates(
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    u32 budget) {
  u32 processed = 0;
  while (processed < budget && !fluid_updates_.empty()) {
    FluidUpdate update = fluid_updates_.front();
    fluid_updates_.pop_front();

    const ChunkCoord chunk_coord = block_to_chunk(update.pos);
    if (!is_chunk_active(chunk_coord)) {
      store_deferred_boundary_fluid_update(chunk_coord, update);
      ++processed;
      continue;
    }

    process_single_fluid_update(registry, definition, update.pos);
    ++processed;
  }
  return processed;
}

WorldTickStats WorldRuntime::tick(
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& fluid_definition) {
  ++tick_index_;

  WorldTickStats stats;
  stats.lighting_processed = tick_policy_.process_lighting_synchronously
                                 ? process_lighting(registry)
                                 : false;
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
      coords.push_back(coord);
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

void WorldRuntime::enqueue_neighbor_block_updates(BlockPos pos) {
  for (const Vec3i offset : NeighborOffsets) {
    block_updates_.push_back({offset_block(pos, offset), BlockUpdateReason::NeighborChanged});
  }
}

void WorldRuntime::enqueue_neighbor_fluid_updates(BlockPos pos, FluidUpdateReason reason) {
  fluid_updates_.push_back({pos, reason});
  for (const Vec3i offset : NeighborOffsets) {
    const BlockPos neighbor = offset_block(pos, offset);
    const ChunkCoord origin_chunk = block_to_chunk(pos);
    const ChunkCoord neighbor_chunk_coord = block_to_chunk(neighbor);
    if (origin_chunk != neighbor_chunk_coord && !is_chunk_active(neighbor_chunk_coord)) {
      store_deferred_boundary_fluid_update(neighbor_chunk_coord, {neighbor, reason});
      continue;
    }
    fluid_updates_.push_back({neighbor, reason});
  }
}

void WorldRuntime::enqueue_chunk_activation_updates(ChunkCoord coord) {
  const voxel::ChunkSection* chunk = find_chunk(coord);
  if (chunk == nullptr) {
    return;
  }

  auto deferred = deferred_boundary_fluid_updates_.find(coord);
  if (deferred != deferred_boundary_fluid_updates_.end()) {
    for (const FluidUpdate& update : deferred->second) {
      fluid_updates_.push_back(update);
    }
    deferred_boundary_fluid_updates_.erase(deferred);
  }

  for (i32 z = 0; z < ChunkSize; ++z) {
    for (i32 y = 0; y < ChunkSize; ++y) {
      for (i32 x = 0; x < ChunkSize; ++x) {
        const SectionLocalPos local{{x, y, z}};
        if (!chunk->fluid(local).empty()) {
          fluid_updates_.push_back(
              {chunk_local_to_block(coord, local), FluidUpdateReason::ChunkActivated});
        }
      }
    }
  }
}

void WorldRuntime::store_deferred_boundary_fluid_update(ChunkCoord target, FluidUpdate update) {
  auto& updates = deferred_boundary_fluid_updates_[target];
  for (const FluidUpdate& existing : updates) {
    if (existing.pos == update.pos) {
      return;
    }
  }
  updates.push_back(update);
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

void WorldRuntime::mark_neighbor_mesh_dependency_dirty(ChunkCoord coord) noexcept {
  voxel::ChunkSection* neighbor = find_chunk(coord);
  if (neighbor != nullptr) {
    neighbor->mark_mesh_dependency_dirty();
    neighbor->mark_dirty(voxel::ChunkDirtyFlags::NeedsRelight | voxel::ChunkDirtyFlags::NeedsFluidStep);
  }
}

void WorldRuntime::mark_neighbor_mesh_only_dependency_dirty(ChunkCoord coord) noexcept {
  voxel::ChunkSection* neighbor = find_chunk(coord);
  if (neighbor != nullptr) {
    neighbor->mark_mesh_dependency_dirty();
  }
}

u32 WorldRuntime::process_due_scheduled_ticks() {
  std::vector<ScheduledBlockUpdate> remaining;
  remaining.reserve(scheduled_block_updates_.size());

  u32 processed = 0;
  for (const ScheduledBlockUpdate& update : scheduled_block_updates_) {
    if (update.execute_at <= tick_index_) {
      block_updates_.push_back({update.pos, update.reason});
      ++processed;
    } else {
      remaining.push_back(update);
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

bool WorldRuntime::can_fluid_occupy(
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    BlockPos pos) const noexcept {
  if (!definition.can_replace_air) {
    return false;
  }

  const voxel::ChunkSection* chunk = find_chunk(block_to_chunk(pos));
  if (chunk == nullptr) {
    return false;
  }

  const voxel::BlockVariantRecord* variant = registry.variant(chunk->block(block_to_local(pos)));
  return variant != nullptr &&
         (registry.is_air(variant->id) || variant->properties.replaceable);
}

bool WorldRuntime::can_flow_down_from(
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    BlockPos pos) const noexcept {
  return definition.can_flow_down &&
         can_fluid_occupy(registry, definition, offset_block(pos, {0, -1, 0}));
}

voxel::FluidCell WorldRuntime::ideal_fluid_at(
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    BlockPos pos,
    voxel::FluidCell current) const noexcept {
  if (!can_fluid_occupy(registry, definition, pos)) {
    return voxel::empty_fluid_cell();
  }

  if (current.source() && current.type == definition.type) {
    return voxel::make_fluid_source(definition);
  }

  const BlockPos above = offset_block(pos, {0, 1, 0});
  const voxel::FluidCell above_fluid = get_fluid(above);
  if (definition.can_flow_down && above_fluid.type == definition.type && !above_fluid.empty()) {
    return voxel::make_flowing_fluid(
        definition,
        definition.vertical_flow_level,
        voxel::FluidFlags::Falling);
  }

  const BlockPos below = offset_block(pos, {0, -1, 0});
  const voxel::FluidCell below_fluid = get_fluid(below);
  if (definition.can_create_sources &&
      (!can_fluid_occupy(registry, definition, below) ||
       (below_fluid.type == definition.type && below_fluid.source()))) {
    u32 adjacent_sources = 0;
    for (const Vec3i offset : std::array<Vec3i, 4>{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}}) {
      const voxel::FluidCell neighbor = get_fluid(offset_block(pos, offset));
      if (neighbor.type == definition.type && neighbor.source()) {
        ++adjacent_sources;
      }
    }
    if (adjacent_sources >= 2U) {
      return voxel::make_fluid_source(definition);
    }
  }

  if (!definition.can_flow_sideways) {
    return voxel::empty_fluid_cell();
  }

  u8 best_level = 0;
  for (const Vec3i offset : std::array<Vec3i, 4>{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}}) {
    const BlockPos neighbor_pos = offset_block(pos, offset);
    if (can_flow_down_from(registry, definition, neighbor_pos)) {
      continue;
    }

    const voxel::FluidCell neighbor = get_fluid(neighbor_pos);
    if (neighbor.type != definition.type || neighbor.empty()) {
      continue;
    }

    const u8 neighbor_level = neighbor.source() ? definition.max_level : neighbor.level;
    if (neighbor_level <= definition.horizontal_decay) {
      continue;
    }

    const u8 candidate = static_cast<u8>(neighbor_level - definition.horizontal_decay);
    if (candidate > best_level) {
      best_level = candidate;
    }
  }

  if (best_level == 0) {
    return voxel::empty_fluid_cell();
  }
  return voxel::make_flowing_fluid(definition, best_level);
}

void WorldRuntime::process_single_fluid_update(
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    BlockPos pos) {
  voxel::ChunkSection* chunk = find_chunk(block_to_chunk(pos));
  if (chunk == nullptr) {
    return;
  }

  const SectionLocalPos local = block_to_local(pos);
  const voxel::FluidCell current = chunk->fluid(local);
  const voxel::FluidCell ideal = ideal_fluid_at(registry, definition, pos, current);
  if (current == ideal) {
    return;
  }

  const bool affects_lighting = definition.reduces_sky_light || definition.emits_light;
  if (!chunk->set_fluid(local, ideal, affects_lighting)) {
    return;
  }

  mark_boundary_neighbors_dirty(block_to_chunk(pos), local);
  enqueue_neighbor_fluid_updates(
      pos,
      ideal.empty() ? FluidUpdateReason::FluidRemoved : FluidUpdateReason::FluidAdded);
}

bool WorldRuntime::can_light_enter(
    const voxel::BlockRegistry& registry,
    BlockPos pos) const noexcept {
  const voxel::ChunkSection* chunk = find_chunk(block_to_chunk(pos));
  if (chunk == nullptr) {
    return false;
  }

  const voxel::BlockVariantRecord* variant = registry.variant(chunk->block(block_to_local(pos)));
  return variant != nullptr && !variant->properties.blocks_light;
}

void WorldRuntime::rebuild_loaded_lighting(const voxel::BlockRegistry& registry) {
  using LightArray = std::array<voxel::LightWord, voxel::ChunkVolume>;

  std::unordered_map<ChunkCoord, LightArray> buffers;
  buffers.reserve(chunks_.size());
  for (const auto& [coord, chunk] : chunks_) {
    (void)chunk;
    LightArray buffer{};
    buffer.fill(voxel::NoLight);
    buffers.emplace(coord, buffer);
  }

  auto set_sky = [&buffers](BlockPos pos, u8 value) {
    const ChunkCoord coord = block_to_chunk(pos);
    auto found = buffers.find(coord);
    if (found == buffers.end()) {
      return false;
    }

    const SectionLocalPos local = block_to_local(pos);
    voxel::LightWord& light = found->second[voxel::local_index(local)];
    if (value <= voxel::light_sky(light)) {
      return false;
    }

    light = voxel::with_sky_light(light, value);
    return true;
  };

  auto set_rgb = [&buffers](BlockPos pos, u8 r, u8 g, u8 b) {
    const ChunkCoord coord = block_to_chunk(pos);
    auto found = buffers.find(coord);
    if (found == buffers.end()) {
      return false;
    }

    const SectionLocalPos local = block_to_local(pos);
    voxel::LightWord& light = found->second[voxel::local_index(local)];
    const u8 next_r = r > voxel::light_r(light) ? r : voxel::light_r(light);
    const u8 next_g = g > voxel::light_g(light) ? g : voxel::light_g(light);
    const u8 next_b = b > voxel::light_b(light) ? b : voxel::light_b(light);
    if (next_r == voxel::light_r(light) && next_g == voxel::light_g(light) &&
        next_b == voxel::light_b(light)) {
      return false;
    }

    light = voxel::pack_light(next_r, next_g, next_b, voxel::light_sky(light));
    return true;
  };

  std::unordered_map<ChunkColumnCoord, i32> top_by_column;
  std::unordered_map<ChunkColumnCoord, i32> bottom_by_column;
  for (const auto& [coord, chunk] : chunks_) {
    (void)chunk;
    for (i32 z = 0; z < ChunkSize; ++z) {
      for (i32 x = 0; x < ChunkSize; ++x) {
        const i32 world_x = coord.value.x * ChunkSize + x;
        const i32 world_z = coord.value.z * ChunkSize + z;
        const i32 top_y = coord.value.y * ChunkSize + (ChunkSize - 1);
        const i32 bottom_y = coord.value.y * ChunkSize;
        const ChunkColumnCoord key{world_x, world_z};
        auto top = top_by_column.find(key);
        if (top == top_by_column.end() || top_y > top->second) {
          top_by_column[key] = top_y;
        }
        auto bottom = bottom_by_column.find(key);
        if (bottom == bottom_by_column.end() || bottom_y < bottom->second) {
          bottom_by_column[key] = bottom_y;
        }
      }
    }
  }

  std::queue<std::pair<BlockPos, u8>> sky_queue;
  for (const auto& [key, top_y] : top_by_column) {
    const i32 world_x = key.x;
    const i32 world_z = key.z;
    const i32 bottom_y = bottom_by_column[key];
    for (i32 world_y = top_y; world_y >= bottom_y; --world_y) {
      const BlockPos pos{{world_x, world_y, world_z}};
      const voxel::ChunkSection* chunk = find_chunk(block_to_chunk(pos));
      if (chunk == nullptr) {
        continue;
      }
      if (!can_light_enter(registry, pos)) {
        break;
      }
      if (set_sky(pos, voxel::MaxLightLevel)) {
        sky_queue.push({pos, voxel::MaxLightLevel});
      }
    }
  }

  while (!sky_queue.empty()) {
    const auto [pos, level] = sky_queue.front();
    sky_queue.pop();
    if (level <= 1U) {
      continue;
    }

    const u8 next = static_cast<u8>(level - 1U);
    for (const Vec3i offset : NeighborOffsets) {
      const BlockPos neighbor = offset_block(pos, offset);
      if (!can_light_enter(registry, neighbor)) {
        continue;
      }
      if (set_sky(neighbor, next)) {
        sky_queue.push({neighbor, next});
      }
    }
  }

  struct RgbNode {
    BlockPos pos{};
    u8 r{};
    u8 g{};
    u8 b{};
  };

  std::queue<RgbNode> rgb_queue;
  for (const auto& [coord, chunk] : chunks_) {
    for (i32 z = 0; z < ChunkSize; ++z) {
      for (i32 y = 0; y < ChunkSize; ++y) {
        for (i32 x = 0; x < ChunkSize; ++x) {
          const SectionLocalPos local{{x, y, z}};
          const voxel::BlockVariantRecord* variant = registry.variant(chunk.block(local));
          if (variant == nullptr || !variant->properties.emits_light) {
            continue;
          }

          const BlockPos pos = chunk_local_to_block(coord, local);
          const u8 r = voxel::clamp_light_channel(variant->properties.emission_r);
          const u8 g = voxel::clamp_light_channel(variant->properties.emission_g);
          const u8 b = voxel::clamp_light_channel(variant->properties.emission_b);
          if (set_rgb(pos, r, g, b)) {
            rgb_queue.push({pos, r, g, b});
          }
        }
      }
    }
  }

  while (!rgb_queue.empty()) {
    const RgbNode node = rgb_queue.front();
    rgb_queue.pop();

    const u8 next_r = node.r > 0 ? static_cast<u8>(node.r - 1U) : 0;
    const u8 next_g = node.g > 0 ? static_cast<u8>(node.g - 1U) : 0;
    const u8 next_b = node.b > 0 ? static_cast<u8>(node.b - 1U) : 0;
    if (next_r == 0 && next_g == 0 && next_b == 0) {
      continue;
    }

    for (const Vec3i offset : NeighborOffsets) {
      const BlockPos neighbor = offset_block(node.pos, offset);
      if (!can_light_enter(registry, neighbor)) {
        continue;
      }
      if (set_rgb(neighbor, next_r, next_g, next_b)) {
        rgb_queue.push({neighbor, next_r, next_g, next_b});
      }
    }
  }

  for (auto& [coord, buffer] : buffers) {
    voxel::ChunkSection* chunk = find_chunk(coord);
    if (chunk != nullptr) {
      chunk->replace_lights(std::move(buffer));
    }
  }
}

voxel::ChunkBoundarySnapshot WorldRuntime::copy_neighbor_boundary(
    const voxel::ChunkSection& neighbor,
    voxel::BlockFace target_face) noexcept {
  voxel::ChunkBoundarySnapshot boundary;
  boundary.available = true;

  for (i32 a = 0; a < ChunkSize; ++a) {
    for (i32 b = 0; b < ChunkSize; ++b) {
      SectionLocalPos local{};
      switch (target_face) {
        case voxel::BlockFace::PositiveX:
          local = {{0, a, b}};
          break;
        case voxel::BlockFace::NegativeX:
          local = {{ChunkSize - 1, a, b}};
          break;
        case voxel::BlockFace::PositiveY:
          local = {{a, 0, b}};
          break;
        case voxel::BlockFace::NegativeY:
          local = {{a, ChunkSize - 1, b}};
          break;
        case voxel::BlockFace::PositiveZ:
          local = {{a, b, 0}};
          break;
        case voxel::BlockFace::NegativeZ:
          local = {{a, b, ChunkSize - 1}};
          break;
      }
      boundary.blocks[voxel::boundary_index_for_face(target_face, local)] = neighbor.block(local);
      boundary.lights[voxel::boundary_index_for_face(target_face, local)] = neighbor.light(local);
    }
  }

  return boundary;
}

} // namespace arv::world
