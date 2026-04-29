#include "arv/world/world_lighting.hpp"

#include "arv/world/world_runtime.hpp"

#include <queue>
#include <utility>

namespace arv::world {
namespace {

using LightArray = std::array<voxel::LightWord, voxel::ChunkVolume>;

inline constexpr std::array<Vec3i, voxel::BlockFaceCount> NeighborOffsets{{
    {1, 0, 0},
    {-1, 0, 0},
    {0, 1, 0},
    {0, -1, 0},
    {0, 0, 1},
    {0, 0, -1},
}};

[[nodiscard]] BlockPos offset_block(BlockPos pos, Vec3i offset) noexcept {
  return {{pos.value.x + offset.x, pos.value.y + offset.y, pos.value.z + offset.z}};
}

} // namespace

bool WorldLighting::has_work(const WorldRuntime& world) noexcept {
  for (const auto& [coord, chunk] : world.chunks_) {
    (void)coord;
    if (chunk.is_dirty(voxel::ChunkDirtyFlags::Lighting)) {
      return true;
    }
  }
  return false;
}

LightingSnapshot WorldLighting::make_snapshot(const WorldRuntime& world) {
  LightingSnapshot snapshot;
  snapshot.world_id = world.id_;
  snapshot.source_revision = world.lighting_revision_;

  std::vector<ChunkCoord> coords;
  HashSet<ChunkCoord> coord_set;
  auto add_if_loaded = [&world, &coords, &coord_set](ChunkCoord coord) {
    if (world.find_chunk(coord) == nullptr) {
      return;
    }
    if (coord_set.insert(coord).second) {
      coords.push_back(coord);
    }
  };

  for (const auto& [coord, chunk] : world.chunks_) {
    if (!chunk.is_dirty(voxel::ChunkDirtyFlags::Lighting)) {
      continue;
    }
    add_if_loaded(coord);
    for (const Vec3i offset : NeighborOffsets) {
      add_if_loaded({{
          coord.value.x + offset.x,
          coord.value.y + offset.y,
          coord.value.z + offset.z,
      }});
    }
  }

  snapshot.chunks.reserve(coords.size());
  for (const ChunkCoord coord : coords) {
    const voxel::ChunkSection* chunk = world.find_chunk(coord);
    if (chunk == nullptr) {
      continue;
    }
    snapshot.chunks.push_back({
        coord,
        chunk->block_version(),
        chunk->storage_view(),
        chunk->content_version(),
        chunk->is_dirty(voxel::ChunkDirtyFlags::Lighting)});
  }
  return snapshot;
}

LightingResult WorldLighting::build_result(
    const LightingSnapshot& snapshot,
    const voxel::BlockRegistry& registry) {
  LightingResult result;
  result.world_id = snapshot.world_id;
  result.source_revision = snapshot.source_revision;
  usize dirty_chunk_count = 0;
  for (const ChunkLightingSnapshot& chunk : snapshot.chunks) {
    if (chunk.needs_relight) {
      ++dirty_chunk_count;
    }
  }
  result.chunks.reserve(dirty_chunk_count);

  HashMap<ChunkCoord, LightArray> buffers;
  HashMap<ChunkCoord, const ChunkLightingSnapshot*> chunks;
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
        registry.variant(chunk->blocks()[voxel::local_index(block_to_local(pos))]);
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

  HashMap<ChunkColumnCoord, i32> top_by_column;
  HashMap<ChunkColumnCoord, i32> bottom_by_column;
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
              registry.variant(chunk.blocks()[voxel::local_index(local)]);
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
    if (found != buffers.end() &&
        (chunk.needs_relight || found->second != chunk.storage->lights)) {
      result.chunks.push_back({
          chunk.coord,
          chunk.source_version,
          std::move(found->second),
          chunk.source_content_version,
      });
    }
  }
  return result;
}

LightingPublishStats WorldLighting::publish_result(
    WorldRuntime& world,
    LightingResult result) noexcept {
  LightingPublishStats stats;
  if (result.world_id != world.id_ ||
      (result.source_revision != 0U && result.source_revision != world.lighting_revision_)) {
    stats.stale_chunks = static_cast<u32>(result.chunks.size());
    return stats;
  }

  for (ChunkLightingResult& chunk_result : result.chunks) {
    voxel::ChunkSection* chunk = world.find_chunk(chunk_result.coord);
    const bool chunk_matches_source =
        chunk != nullptr &&
        (chunk->block_version() == chunk_result.source_version ||
         (chunk_result.source_content_version != 0U &&
          chunk->content_version() == chunk_result.source_content_version));
    if (!chunk_matches_source) {
      ++stats.stale_chunks;
      continue;
    }

    if (chunk->replace_lights(std::move(chunk_result.lights))) {
      ++stats.changed_chunks;
      world.enqueue_dirty_mesh_chunk(chunk_result.coord);
      world.mark_light_dependency_neighbors_dirty(chunk_result.coord);
    }
    chunk->clear_dirty(voxel::ChunkDirtyFlags::Lighting);
    ++stats.accepted_chunks;
  }
  return stats;
}

} // namespace arv::world
