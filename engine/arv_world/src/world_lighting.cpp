#include "arv/world/world_lighting.hpp"

#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"
#include "arv/world/world_runtime.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <cstdio>
#include <utility>
#include <vector>

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

[[nodiscard]] usize local_index(i32 x, i32 y, i32 z) noexcept {
  return static_cast<usize>(x) +
         voxel::ChunkSide * (static_cast<usize>(y) + voxel::ChunkSide * static_cast<usize>(z));
}

[[nodiscard]] usize column_index(i32 x, i32 z) noexcept {
  return static_cast<usize>(x) + voxel::ChunkSide * static_cast<usize>(z);
}

[[nodiscard]] SectionLocalPos local_pos(i32 x, i32 y, i32 z) noexcept {
  return {{x, y, z}};
}

[[nodiscard]] bool chunk_coord_less(ChunkCoord lhs, ChunkCoord rhs) noexcept {
  if (lhs.value.x != rhs.value.x) {
    return lhs.value.x < rhs.value.x;
  }
  if (lhs.value.z != rhs.value.z) {
    return lhs.value.z < rhs.value.z;
  }
  return lhs.value.y > rhs.value.y;
}

[[nodiscard]] const char* lighting_job_type_name(LightingJobType type) noexcept {
  switch (type) {
    case LightingJobType::SectionRelight:
      return "section";
    case LightingJobType::BoundaryContinuation:
      return "boundary";
  }
  return "unknown";
}

[[nodiscard]] u8 boundary_bit(usize face_index) noexcept {
  return static_cast<u8>(1U << face_index);
}

[[nodiscard]] u32 count_boundary_bits(u8 mask) noexcept {
  u32 count = 0;
  while (mask != 0U) {
    count += static_cast<u32>(mask & 1U);
    mask = static_cast<u8>(mask >> 1U);
  }
  return count;
}

[[nodiscard]] SectionLocalPos target_boundary_local(usize face_index, i32 a, i32 b) noexcept {
  switch (static_cast<voxel::BlockFace>(face_index)) {
    case voxel::BlockFace::PositiveX:
      return local_pos(ChunkSize - 1, a, b);
    case voxel::BlockFace::NegativeX:
      return local_pos(0, a, b);
    case voxel::BlockFace::PositiveY:
      return local_pos(a, ChunkSize - 1, b);
    case voxel::BlockFace::NegativeY:
      return local_pos(a, 0, b);
    case voxel::BlockFace::PositiveZ:
      return local_pos(a, b, ChunkSize - 1);
    case voxel::BlockFace::NegativeZ:
      return local_pos(a, b, 0);
  }
  return {};
}

[[nodiscard]] SectionLocalPos neighbor_boundary_local(usize face_index, i32 a, i32 b) noexcept {
  switch (static_cast<voxel::BlockFace>(face_index)) {
    case voxel::BlockFace::PositiveX:
      return local_pos(0, a, b);
    case voxel::BlockFace::NegativeX:
      return local_pos(ChunkSize - 1, a, b);
    case voxel::BlockFace::PositiveY:
      return local_pos(a, 0, b);
    case voxel::BlockFace::NegativeY:
      return local_pos(a, ChunkSize - 1, b);
    case voxel::BlockFace::PositiveZ:
      return local_pos(a, b, 0);
    case voxel::BlockFace::NegativeZ:
      return local_pos(a, b, ChunkSize - 1);
  }
  return {};
}

[[nodiscard]] u8 boundary_mask_for_local(i32 x, i32 y, i32 z) noexcept {
  u8 mask = 0;
  if (x == ChunkSize - 1) {
    mask |= boundary_bit(voxel::face_index(voxel::BlockFace::PositiveX));
  }
  if (x == 0) {
    mask |= boundary_bit(voxel::face_index(voxel::BlockFace::NegativeX));
  }
  if (y == ChunkSize - 1) {
    mask |= boundary_bit(voxel::face_index(voxel::BlockFace::PositiveY));
  }
  if (y == 0) {
    mask |= boundary_bit(voxel::face_index(voxel::BlockFace::NegativeY));
  }
  if (z == ChunkSize - 1) {
    mask |= boundary_bit(voxel::face_index(voxel::BlockFace::PositiveZ));
  }
  if (z == 0) {
    mask |= boundary_bit(voxel::face_index(voxel::BlockFace::NegativeZ));
  }
  return mask;
}

[[nodiscard]] ChunkCoord offset_chunk(ChunkCoord coord, Vec3i offset) noexcept {
  return {{
      coord.value.x + offset.x,
      coord.value.y + offset.y,
      coord.value.z + offset.z,
  }};
}

[[nodiscard]] const ChunkLightingSnapshot* find_snapshot_chunk(
    const LightingSnapshot& snapshot,
    ChunkCoord coord) noexcept {
  for (const ChunkLightingSnapshot& chunk : snapshot.chunks) {
    if (chunk.coord == coord) {
      return &chunk;
    }
  }
  return nullptr;
}

struct LightingTables {
  std::vector<u8> blocks_light{};
  std::vector<voxel::LightWord> emission{};
};

[[nodiscard]] LightingTables make_lighting_tables(const voxel::BlockRegistry& registry) {
  LightingTables tables;
  tables.blocks_light.resize(registry.variant_count(), 1U);
  tables.emission.resize(registry.variant_count(), voxel::NoLight);
  for (u32 value = 0; value < static_cast<u32>(registry.variant_count()); ++value) {
    const voxel::BlockVariantRecord* variant = registry.variant(voxel::BlockVariantID{value});
    if (variant == nullptr) {
      continue;
    }
    tables.blocks_light[value] = variant->properties.blocks_light ? 1U : 0U;
    if (variant->properties.emits_light) {
      tables.emission[value] = voxel::pack_block_light(
          variant->properties.emission_r,
          variant->properties.emission_g,
          variant->properties.emission_b);
    }
  }
  return tables;
}

[[nodiscard]] bool blocks_light(
    const LightingTables& tables,
    voxel::BlockVariantID variant) noexcept {
  const usize index = static_cast<usize>(variant.value);
  return index >= tables.blocks_light.size() || tables.blocks_light[index] != 0U;
}

[[nodiscard]] voxel::LightWord emission_for(
    const LightingTables& tables,
    voxel::BlockVariantID variant) noexcept {
  const usize index = static_cast<usize>(variant.value);
  return index < tables.emission.size() ? tables.emission[index] : voxel::NoLight;
}

void count_light_delta(
    voxel::LightWord previous,
    voxel::LightWord next,
    u32& add_nodes,
    u32& remove_nodes) noexcept {
  bool added = false;
  bool removed = false;
  auto compare = [&](u8 before, u8 after) {
    added = added || after > before;
    removed = removed || after < before;
  };
  compare(voxel::light_r(previous), voxel::light_r(next));
  compare(voxel::light_g(previous), voxel::light_g(next));
  compare(voxel::light_b(previous), voxel::light_b(next));
  compare(voxel::light_sky(previous), voxel::light_sky(next));
  if (added) {
    ++add_nodes;
  }
  if (removed) {
    ++remove_nodes;
  }
}

void record_lighting_job_finished(
    const LightingResult& result,
    std::chrono::steady_clock::time_point start) noexcept {
  if constexpr (core::perf_stats_enabled()) {
    std::array<char, core::PerfEventContextCapacity> profile_context{};
    (void)std::snprintf(
        profile_context.data(),
        profile_context.size(),
        "section=(%d,%d,%d) type=%s nodes=%u cont=%u neighbor=%u",
        result.stats.section_coord.value.x,
        result.stats.section_coord.value.y,
        result.stats.section_coord.value.z,
        lighting_job_type_name(result.stats.job_type),
        result.stats.nodes_processed,
        result.stats.continuation_count,
        result.stats.touched_neighbor_sections ? 1U : 0U);
    const auto end = std::chrono::steady_clock::now();
    core::record_thread_timing(
        core::PerfMetric::LightingTask,
        std::chrono::duration<double, std::milli>(end - start).count(),
        "worker/server",
        profile_context.data());
    core::record_lighting_job_completed(
        {
            result.stats.nodes_processed,
            result.stats.add_nodes_processed,
            result.stats.remove_nodes_processed,
            result.stats.boundary_deferrals,
            result.stats.sections_touched,
            0U,
            result.stats.max_queue_depth,
            0U,
            result.continuation_requested,
        });
  } else {
    (void)result;
    (void)start;
  }
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
  ARV_PROFILE_ZONE("Snapshot creation");
  LightingSnapshot snapshot;
  snapshot.world_id = world.id_;
  snapshot.source_revision = world.lighting_revision_;

  std::vector<ChunkCoord> dirty_coords;
  dirty_coords.reserve(world.chunks_.size());
  for (const auto& [coord, chunk] : world.chunks_) {
    if (chunk.is_dirty(voxel::ChunkDirtyFlags::Lighting)) {
      dirty_coords.emplace_back(coord);
    }
  }

  if (dirty_coords.empty()) {
    return snapshot;
  }

  std::sort(dirty_coords.begin(), dirty_coords.end(), chunk_coord_less);
  snapshot.target_coord = dirty_coords.front();
  snapshot.pending_dirty_sections = static_cast<u32>(
      std::min<usize>(dirty_coords.size(), static_cast<usize>(~u32{0})));
  if (const voxel::ChunkSection* target = world.find_chunk(snapshot.target_coord);
      target != nullptr && !target->is_dirty(voxel::ChunkDirtyFlags::Geometry)) {
    snapshot.job_type = LightingJobType::BoundaryContinuation;
  }

  std::array<i32, voxel::ChunkArea> target_surface_y{};
  bool has_generation_surface = false;
  i32 max_surface_section_y = snapshot.target_coord.value.y;
  if (world.generation_config_.has_value()) {
    has_generation_surface = true;
    const WorldGenerationConfig& generation_config = world.generation_config_.value();
    for (i32 z = 0; z < ChunkSize; ++z) {
      for (i32 x = 0; x < ChunkSize; ++x) {
        const i32 world_x = snapshot.target_coord.value.x * ChunkSize + x;
        const i32 world_z = snapshot.target_coord.value.z * ChunkSize + z;
        const i32 surface_y = terrain_height_at(
            generation_config,
            world.generation_seed_,
            world_x,
            world_z);
        target_surface_y[column_index(x, z)] = surface_y;
        max_surface_section_y =
            std::max(max_surface_section_y, floor_div(surface_y, ChunkSize));
      }
    }
  }

  auto has_loaded_section_above = [&world](ChunkCoord coord) noexcept {
    for (const auto& [loaded_coord, chunk] : world.chunks_) {
      (void)chunk;
      if (loaded_coord.value.x == coord.value.x &&
          loaded_coord.value.z == coord.value.z &&
          loaded_coord.value.y > coord.value.y) {
        return true;
      }
    }
    return false;
  };

  auto make_missing_above_sky = [&world, &has_loaded_section_above, &target_surface_y](
                                    ChunkCoord coord) noexcept {
    std::array<u8, voxel::ChunkArea> sky{};
    if (has_loaded_section_above(coord)) {
      return sky;
    }

    if (!world.generation_config_.has_value()) {
      if (coord.value.y >= 0) {
        sky.fill(voxel::MaxLightLevel);
      }
      return sky;
    }

    const i32 section_top_y =
        coord.value.y * ChunkSize + static_cast<i32>(ChunkSize - 1);
    for (i32 z = 0; z < ChunkSize; ++z) {
      for (i32 x = 0; x < ChunkSize; ++x) {
        if (section_top_y >= target_surface_y[column_index(x, z)]) {
          sky[column_index(x, z)] = voxel::MaxLightLevel;
        }
      }
    }
    return sky;
  };

  std::vector<ChunkCoord> coords;
  coords.reserve(voxel::BlockFaceCount + 1U);
  auto add_if_loaded = [&world, &coords](ChunkCoord coord) {
    if (world.find_chunk(coord) == nullptr) {
      return;
    }
    if (std::find(coords.begin(), coords.end(), coord) == coords.end()) {
      coords.emplace_back(coord);
    }
  };

  add_if_loaded(snapshot.target_coord);
  for (const Vec3i offset : NeighborOffsets) {
    add_if_loaded(offset_chunk(snapshot.target_coord, offset));
  }
  if (has_generation_surface) {
    for (i32 y = snapshot.target_coord.value.y + 1; y <= max_surface_section_y; ++y) {
      add_if_loaded({{
          snapshot.target_coord.value.x,
          y,
          snapshot.target_coord.value.z,
      }});
    }
  }

  snapshot.chunks.reserve(coords.size());
  for (const ChunkCoord coord : coords) {
    const voxel::ChunkSection* chunk = world.find_chunk(coord);
    if (chunk == nullptr) {
      continue;
    }
    ChunkLightingSnapshot chunk_snapshot;
    chunk_snapshot.coord = coord;
    chunk_snapshot.source_version = chunk->block_version();
    chunk_snapshot.storage = chunk->storage_view();
    chunk_snapshot.source_content_version = chunk->content_version();
    chunk_snapshot.needs_relight = chunk->is_dirty(voxel::ChunkDirtyFlags::Lighting);
    chunk_snapshot.is_target = coord == snapshot.target_coord;
    if (chunk_snapshot.is_target) {
      chunk_snapshot.missing_above_sky = make_missing_above_sky(coord);
      chunk_snapshot.generation_surface_y = target_surface_y;
      chunk_snapshot.has_generation_surface = has_generation_surface;
    }
    snapshot.chunks.emplace_back(std::move(chunk_snapshot));
  }
  return snapshot;
}

LightingResult WorldLighting::build_result(
    const LightingSnapshot& snapshot,
    const voxel::BlockRegistry& registry) {
  ARV_PROFILE_ZONE("Lighting propagation");
  const auto timing_start = std::chrono::steady_clock::now();
  LightingResult result;
  result.world_id = snapshot.world_id;
  result.source_revision = snapshot.source_revision;
  result.stats.section_coord = snapshot.target_coord;
  result.stats.job_type = snapshot.job_type;
  result.chunks.reserve(1U);

  const ChunkLightingSnapshot* target = find_snapshot_chunk(snapshot, snapshot.target_coord);
  if (target == nullptr) {
    record_lighting_job_finished(result, timing_start);
    return result;
  }

  const LightingTables tables = make_lighting_tables(registry);
  std::array<const ChunkLightingSnapshot*, voxel::BlockFaceCount> face_neighbors{};
  for (usize face_index = 0; face_index < face_neighbors.size(); ++face_index) {
    face_neighbors[face_index] =
        find_snapshot_chunk(snapshot, offset_chunk(target->coord, NeighborOffsets[face_index]));
    if (face_neighbors[face_index] != nullptr) {
      ++result.stats.sections_touched;
      result.stats.touched_neighbor_sections = true;
    }
  }
  ++result.stats.sections_touched;

  LightArray lights{};
  lights.fill(voxel::NoLight);

  auto can_light_enter_index = [&](usize index) noexcept {
    return !blocks_light(tables, target->blocks()[index]);
  };

  auto can_light_enter_world = [&](i32 world_x, i32 world_y, i32 world_z)
      -> std::optional<bool> {
    const BlockPos pos{{world_x, world_y, world_z}};
    const ChunkLightingSnapshot* chunk = find_snapshot_chunk(snapshot, block_to_chunk(pos));
    if (chunk == nullptr) {
      return std::nullopt;
    }
    return !blocks_light(tables, chunk->blocks()[voxel::local_index(block_to_local(pos))]);
  };

  auto sky_from_generation_surface = [&](i32 x, i32 z) -> std::optional<u8> {
    if (!target->has_generation_surface) {
      return std::nullopt;
    }

    const i32 surface_y = target->generation_surface_y[column_index(x, z)];
    const i32 target_top_y =
        target->coord.value.y * ChunkSize + static_cast<i32>(ChunkSize - 1);
    if (target_top_y >= surface_y) {
      return voxel::MaxLightLevel;
    }

    const i32 world_x = target->coord.value.x * ChunkSize + x;
    const i32 world_z = target->coord.value.z * ChunkSize + z;
    for (i32 world_y = surface_y; world_y > target_top_y; --world_y) {
      const std::optional<bool> can_enter =
          can_light_enter_world(world_x, world_y, world_z);
      if (!can_enter.has_value()) {
        return std::nullopt;
      }
      if (!can_enter.value()) {
        return 0U;
      }
    }
    return voxel::MaxLightLevel;
  };

  struct SkyNode {
    u8 x{};
    u8 y{};
    u8 z{};
    u8 level{};
  };

  struct RgbNode {
    u8 x{};
    u8 y{};
    u8 z{};
    u8 r{};
    u8 g{};
    u8 b{};
  };

  std::vector<SkyNode> sky_queue;
  std::vector<RgbNode> rgb_queue;
  sky_queue.reserve(voxel::ChunkVolume);
  rgb_queue.reserve(voxel::ChunkVolume * 2U);
  usize sky_queue_head = 0;
  usize rgb_queue_head = 0;

  auto note_sky_queue_depth = [&]() noexcept {
    result.stats.max_queue_depth = std::max<u32>(
        result.stats.max_queue_depth,
        static_cast<u32>(sky_queue.size() - sky_queue_head));
  };

  auto note_rgb_queue_depth = [&]() noexcept {
    result.stats.max_queue_depth = std::max<u32>(
        result.stats.max_queue_depth,
        static_cast<u32>(rgb_queue.size() - rgb_queue_head));
  };

  auto set_sky = [&](i32 x, i32 y, i32 z, u8 value) {
    const usize index = local_index(x, y, z);
    voxel::LightWord& light = lights[index];
    if (value <= voxel::light_sky(light)) {
      return false;
    }

    light = voxel::with_sky_light(light, value);
    sky_queue.push_back(
        {
            static_cast<u8>(x),
            static_cast<u8>(y),
            static_cast<u8>(z),
            value,
        });
    note_sky_queue_depth();
    return true;
  };

  auto set_rgb = [&](i32 x, i32 y, i32 z, u8 r, u8 g, u8 b) {
    const usize index = local_index(x, y, z);
    voxel::LightWord& light = lights[index];
    const u8 next_r = r > voxel::light_r(light) ? r : voxel::light_r(light);
    const u8 next_g = g > voxel::light_g(light) ? g : voxel::light_g(light);
    const u8 next_b = b > voxel::light_b(light) ? b : voxel::light_b(light);
    if (next_r == voxel::light_r(light) && next_g == voxel::light_g(light) &&
        next_b == voxel::light_b(light)) {
      return false;
    }

    light = voxel::pack_light(next_r, next_g, next_b, voxel::light_sky(light));
    rgb_queue.push_back(
        {
            static_cast<u8>(x),
            static_cast<u8>(y),
            static_cast<u8>(z),
            next_r,
            next_g,
            next_b,
        });
    note_rgb_queue_depth();
    return true;
  };

  const usize positive_y = voxel::face_index(voxel::BlockFace::PositiveY);
  const ChunkLightingSnapshot* above = face_neighbors[positive_y];
  for (i32 z = 0; z < ChunkSize; ++z) {
    for (i32 x = 0; x < ChunkSize; ++x) {
      u8 column_sky = 0U;
      if (const std::optional<u8> surface_sky = sky_from_generation_surface(x, z);
          surface_sky.has_value()) {
        column_sky = surface_sky.value();
      } else if (above != nullptr && !above->needs_relight) {
        column_sky =
            voxel::light_sky(above->storage->lights[local_index(x, 0, z)]);
      } else {
        column_sky = target->missing_above_sky[column_index(x, z)];
      }
      if (column_sky == 0U) {
        continue;
      }
      for (i32 y = ChunkSize - 1; y >= 0; --y) {
        const usize index = local_index(x, y, z);
        if (!can_light_enter_index(index)) {
          break;
        }
        (void)set_sky(x, y, z, column_sky);
      }
    }
  }

  for (usize face_index = 0; face_index < face_neighbors.size(); ++face_index) {
    if (face_index == positive_y) {
      continue;
    }
    const ChunkLightingSnapshot* neighbor = face_neighbors[face_index];
    if (neighbor == nullptr || neighbor->needs_relight) {
      continue;
    }
    for (i32 b = 0; b < ChunkSize; ++b) {
      for (i32 a = 0; a < ChunkSize; ++a) {
        const SectionLocalPos target_local = target_boundary_local(face_index, a, b);
        const usize target_index = voxel::local_index(target_local);
        if (!can_light_enter_index(target_index)) {
          continue;
        }

        const SectionLocalPos neighbor_local = neighbor_boundary_local(face_index, a, b);
        const u8 level =
            voxel::light_sky(neighbor->storage->lights[voxel::local_index(neighbor_local)]);
        if (level > 1U) {
          (void)set_sky(
              target_local.value.x,
              target_local.value.y,
              target_local.value.z,
              static_cast<u8>(level - 1U));
        }
      }
    }
  }

  for (usize face_index = 0; face_index < face_neighbors.size(); ++face_index) {
    const ChunkLightingSnapshot* neighbor = face_neighbors[face_index];
    if (neighbor == nullptr || !neighbor->needs_relight) {
      continue;
    }
    if (face_index == positive_y && target->has_generation_surface) {
      continue;
    }
    for (i32 b = 0; b < ChunkSize; ++b) {
      for (i32 a = 0; a < ChunkSize; ++a) {
        const SectionLocalPos target_local = target_boundary_local(face_index, a, b);
        const usize target_index = voxel::local_index(target_local);
        if (!can_light_enter_index(target_index)) {
          continue;
        }

        const u8 previous = voxel::light_sky(target->storage->lights[target_index]);
        if (previous != 0U) {
          (void)set_sky(
              target_local.value.x,
              target_local.value.y,
              target_local.value.z,
              previous);
        }
      }
    }
  }

  while (sky_queue_head < sky_queue.size()) {
    const SkyNode node = sky_queue[sky_queue_head++];
    ++result.stats.nodes_processed;
    if (node.level <= 1U) {
      continue;
    }

    const u8 next = static_cast<u8>(node.level - 1U);
    for (usize face_index = 0; face_index < NeighborOffsets.size(); ++face_index) {
      const i32 x = static_cast<i32>(node.x) + NeighborOffsets[face_index].x;
      const i32 y = static_cast<i32>(node.y) + NeighborOffsets[face_index].y;
      const i32 z = static_cast<i32>(node.z) + NeighborOffsets[face_index].z;
      if (x < 0 || x >= ChunkSize || y < 0 || y >= ChunkSize || z < 0 || z >= ChunkSize) {
        continue;
      }
      const usize index = local_index(x, y, z);
      if (can_light_enter_index(index)) {
        (void)set_sky(x, y, z, next);
      }
    }
  }

  for (usize face_index = 0; face_index < face_neighbors.size(); ++face_index) {
    const ChunkLightingSnapshot* neighbor = face_neighbors[face_index];
    if (neighbor == nullptr || neighbor->needs_relight) {
      continue;
    }
    for (i32 b = 0; b < ChunkSize; ++b) {
      for (i32 a = 0; a < ChunkSize; ++a) {
        const SectionLocalPos target_local = target_boundary_local(face_index, a, b);
        const usize target_index = voxel::local_index(target_local);
        if (!can_light_enter_index(target_index)) {
          continue;
        }

        const SectionLocalPos neighbor_local = neighbor_boundary_local(face_index, a, b);
        const voxel::LightWord neighbor_light =
            neighbor->storage->lights[voxel::local_index(neighbor_local)];
        const u8 r = voxel::light_r(neighbor_light) > 1U
                         ? static_cast<u8>(voxel::light_r(neighbor_light) - 1U)
                         : 0U;
        const u8 g = voxel::light_g(neighbor_light) > 1U
                         ? static_cast<u8>(voxel::light_g(neighbor_light) - 1U)
                         : 0U;
        const u8 b_light = voxel::light_b(neighbor_light) > 1U
                               ? static_cast<u8>(voxel::light_b(neighbor_light) - 1U)
                               : 0U;
        if (r != 0U || g != 0U || b_light != 0U) {
          (void)set_rgb(
              target_local.value.x,
              target_local.value.y,
              target_local.value.z,
              r,
              g,
              b_light);
        }
      }
    }
  }

  for (usize face_index = 0; face_index < face_neighbors.size(); ++face_index) {
    const ChunkLightingSnapshot* neighbor = face_neighbors[face_index];
    if (neighbor == nullptr || !neighbor->needs_relight) {
      continue;
    }
    for (i32 b = 0; b < ChunkSize; ++b) {
      for (i32 a = 0; a < ChunkSize; ++a) {
        const SectionLocalPos target_local = target_boundary_local(face_index, a, b);
        const usize target_index = voxel::local_index(target_local);
        if (!can_light_enter_index(target_index)) {
          continue;
        }

        const voxel::LightWord previous_light = target->storage->lights[target_index];
        const u8 r = voxel::light_r(previous_light);
        const u8 g = voxel::light_g(previous_light);
        const u8 b_light = voxel::light_b(previous_light);
        if (r != 0U || g != 0U || b_light != 0U) {
          (void)set_rgb(
              target_local.value.x,
              target_local.value.y,
              target_local.value.z,
              r,
              g,
              b_light);
        }
      }
    }
  }

  for (i32 z = 0; z < ChunkSize; ++z) {
    for (i32 y = 0; y < ChunkSize; ++y) {
      for (i32 x = 0; x < ChunkSize; ++x) {
        const usize index = local_index(x, y, z);
        const voxel::LightWord emission = emission_for(tables, target->blocks()[index]);
        if (emission == voxel::NoLight) {
          continue;
        }
        (void)set_rgb(
            x,
            y,
            z,
            voxel::light_r(emission),
            voxel::light_g(emission),
            voxel::light_b(emission));
      }
    }
  }

  while (rgb_queue_head < rgb_queue.size()) {
    const RgbNode node = rgb_queue[rgb_queue_head++];
    ++result.stats.nodes_processed;

    const u8 next_r = node.r > 0U ? static_cast<u8>(node.r - 1U) : 0U;
    const u8 next_g = node.g > 0U ? static_cast<u8>(node.g - 1U) : 0U;
    const u8 next_b = node.b > 0U ? static_cast<u8>(node.b - 1U) : 0U;
    if (next_r == 0U && next_g == 0U && next_b == 0U) {
      continue;
    }

    for (usize face_index = 0; face_index < NeighborOffsets.size(); ++face_index) {
      const i32 x = static_cast<i32>(node.x) + NeighborOffsets[face_index].x;
      const i32 y = static_cast<i32>(node.y) + NeighborOffsets[face_index].y;
      const i32 z = static_cast<i32>(node.z) + NeighborOffsets[face_index].z;
      if (x < 0 || x >= ChunkSize || y < 0 || y >= ChunkSize || z < 0 || z >= ChunkSize) {
        continue;
      }
      const usize index = local_index(x, y, z);
      if (can_light_enter_index(index)) {
        (void)set_rgb(x, y, z, next_r, next_g, next_b);
      }
    }
  }

  u8 changed_boundary_mask = 0;
  for (i32 z = 0; z < ChunkSize; ++z) {
    for (i32 y = 0; y < ChunkSize; ++y) {
      for (i32 x = 0; x < ChunkSize; ++x) {
        const usize index = local_index(x, y, z);
        const voxel::LightWord previous = target->storage->lights[index];
        const voxel::LightWord next = lights[index];
        if (previous == next) {
          continue;
        }
        count_light_delta(
            previous,
            next,
            result.stats.add_nodes_processed,
            result.stats.remove_nodes_processed);
        changed_boundary_mask |= boundary_mask_for_local(x, y, z);
      }
    }
  }

  for (usize face_index = 0; face_index < face_neighbors.size(); ++face_index) {
    if ((changed_boundary_mask & boundary_bit(face_index)) == 0U) {
      continue;
    }
    if (face_neighbors[face_index] == nullptr) {
      ++result.stats.boundary_deferrals;
    }
  }

  result.stats.exceeded_soft_budget =
      result.stats.nodes_processed > snapshot.work_budget_nodes;
  const u32 pending_continuations =
      snapshot.pending_dirty_sections > 0U ? snapshot.pending_dirty_sections - 1U : 0U;
  result.stats.continuation_count =
      pending_continuations + count_boundary_bits(changed_boundary_mask);
  result.continuation_requested =
      result.stats.continuation_count > 0U || result.stats.exceeded_soft_budget;

  if (target->needs_relight || lights != target->storage->lights) {
    result.chunks.emplace_back(
        target->coord,
        target->source_version,
        std::move(lights),
        target->source_content_version,
        changed_boundary_mask);
  }
  result.stats.sections_published = static_cast<u32>(result.chunks.size());
  record_lighting_job_finished(result, timing_start);
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

    const u8 changed_boundary_mask = chunk_result.changed_boundary_mask;
    if (chunk->replace_lights(std::move(chunk_result.lights))) {
      ++stats.changed_chunks;
      ++stats.remeshes_triggered;
      world.enqueue_dirty_mesh_chunk(chunk_result.coord);
      for (usize face_index = 0; face_index < NeighborOffsets.size(); ++face_index) {
        if ((changed_boundary_mask & boundary_bit(face_index)) == 0U) {
          continue;
        }

        const ChunkCoord neighbor_coord =
            offset_chunk(chunk_result.coord, NeighborOffsets[face_index]);
        voxel::ChunkSection* neighbor = world.find_chunk(neighbor_coord);
        if (neighbor == nullptr) {
          ++stats.boundary_deferrals;
          continue;
        }

        neighbor->mark_light_dependency_dirty();
        neighbor->mark_dirty(voxel::ChunkDirtyFlags::Lighting);
        world.enqueue_dirty_mesh_chunk(neighbor_coord);
        ++stats.boundary_continuations;
        ++stats.remeshes_triggered;
        world.lighting_work_pending_ = true;
      }
    }
    chunk->clear_dirty(voxel::ChunkDirtyFlags::Lighting);
    ++stats.accepted_chunks;
  }
  if (stats.accepted_chunks > 0U) {
    core::record_lighting_sections_published(stats.accepted_chunks);
  }
  if (stats.remeshes_triggered > 0U) {
    core::record_lighting_remeshes_triggered(stats.remeshes_triggered);
  }
  if (stats.boundary_deferrals > 0U) {
    core::record_lighting_boundary_deferrals(stats.boundary_deferrals);
  }
  return stats;
}

} // namespace arv::world
