#include "arv/world/worldgen/carver_runtime.hpp"

#include "arv/core/profiling.hpp"
#include "arv/world/worldgen/noise.hpp"
#include "arv/world/worldgen/seeds.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace arv::world::worldgen {
namespace {

inline constexpr f32 TwoPi = 6.28318530718F;

struct RavineCandidate {
  f32 start_x{};
  f32 start_z{};
  f32 end_x{};
  f32 end_z{};
  f32 width{};
  i32 depth{};
};

struct TunnelPathState {
  f32 x{};
  f32 y{};
  f32 z{};
  f32 yaw{};
  f32 pitch{};
  f32 length{};
  f32 radius{};
  f32 vertical_radius_multiplier{};
};

[[nodiscard]] f32 range_f32(DeterministicRng& rng, f32 min, f32 max) noexcept {
  if (max <= min) {
    return min;
  }
  return min + (max - min) * rng.next_f32();
}

[[nodiscard]] i32 effective_carver_min_y(
    const ResolvedDimension& dimension,
    const CarverConfig& carver) noexcept {
  return std::max(
      carver.y_range.min,
      dimension.raw.vertical.min_y + std::max(0, carver.floor_margin));
}

[[nodiscard]] i32 effective_carver_max_y(
    const ResolvedDimension& dimension,
    const CarverConfig& carver) noexcept {
  return std::min(carver.y_range.max, dimension.raw.vertical.max_y);
}

[[nodiscard]] u64 carver_runtime_salt(const CarverConfig& carver, u64 salt) noexcept {
  return salt ^ mix64(fnv1a(carver.id));
}

[[nodiscard]] RavineCandidate make_ravine_candidate(
    const CarverConfig& carver,
    u64 dimension_seed,
    i32 cell_x,
    i32 cell_z) noexcept {
  const RavineCarverConfig& config = carver.ravine;
  DeterministicRng rng{coordinate_seed(
      dimension_seed,
      cell_x,
      static_cast<i32>(config.salt & 0x7FFFFFFFULL),
      cell_z,
      config.salt ^ mix64(fnv1a(carver.id)))};
  const i32 region_chunks = std::max(1, config.region_size_chunks);
  const i32 start_chunk_x = cell_x * region_chunks + rng.range_i32(0, region_chunks - 1);
  const i32 start_chunk_z = cell_z * region_chunks + rng.range_i32(0, region_chunks - 1);
  const f32 start_x =
      static_cast<f32>(start_chunk_x * ChunkSize + rng.range_i32(0, ChunkSize - 1));
  const f32 start_z =
      static_cast<f32>(start_chunk_z * ChunkSize + rng.range_i32(0, ChunkSize - 1));
  const f32 length = static_cast<f32>(
      rng.range_i32(config.length_chunks.min, config.length_chunks.max) * ChunkSize +
      rng.range_i32(0, ChunkSize - 1));
  const f32 angle = rng.next_f32() * TwoPi;
  return {
      start_x,
      start_z,
      start_x + std::cos(angle) * length,
      start_z + std::sin(angle) * length,
      range_f32(rng, config.width.min, config.width.max),
      rng.range_i32(config.depth.min, config.depth.max)};
}

[[nodiscard]] bool ravine_passes_chance(
    const CarverConfig& carver,
    u64 dimension_seed,
    i32 cell_x,
    i32 cell_z) noexcept {
  const RavineCarverConfig& config = carver.ravine;
  DeterministicRng rng{coordinate_seed(
      dimension_seed,
      cell_x,
      static_cast<i32>(config.salt & 0x7FFFFFFFULL),
      cell_z,
      config.salt ^ mix64(fnv1a(carver.id)))};
  return rng.chance(config.chance_per_region);
}

[[nodiscard]] f32 horizontal_distance_sq_to_segment(
    const RavineCandidate& ravine,
    f32 x,
    f32 z) noexcept {
  const f32 vx = ravine.end_x - ravine.start_x;
  const f32 vz = ravine.end_z - ravine.start_z;
  const f32 length_sq = vx * vx + vz * vz;
  if (length_sq <= 0.001F) {
    const f32 dx = x - ravine.start_x;
    const f32 dz = z - ravine.start_z;
    return dx * dx + dz * dz;
  }
  const f32 t = std::clamp(
      ((x - ravine.start_x) * vx + (z - ravine.start_z) * vz) / length_sq,
      0.0F,
      1.0F);
  const f32 closest_x = ravine.start_x + vx * t;
  const f32 closest_z = ravine.start_z + vz * t;
  const f32 dx = x - closest_x;
  const f32 dz = z - closest_z;
  return dx * dx + dz * dz;
}

[[nodiscard]] TunnelPathState make_tunnel_path(
    const CarverConfig& carver,
    DeterministicRng& rng,
    i32 cell_x,
    i32 cell_z,
    i32 effective_min_y,
    i32 effective_max_y) noexcept {
  const TunnelCaveCarverConfig& config = carver.tunnel;
  const i32 region_chunks = std::max(1, config.region_size_chunks);
  const i32 start_chunk_x = cell_x * region_chunks + rng.range_i32(0, region_chunks - 1);
  const i32 start_chunk_z = cell_z * region_chunks + rng.range_i32(0, region_chunks - 1);
  const i32 start_min_y = std::max(effective_min_y, config.start_y.min);
  const i32 start_max_y = std::min(effective_max_y, config.start_y.max);
  return {
      static_cast<f32>(start_chunk_x * ChunkSize + rng.range_i32(0, ChunkSize - 1)),
      static_cast<f32>(rng.range_i32(start_min_y, start_max_y)),
      static_cast<f32>(start_chunk_z * ChunkSize + rng.range_i32(0, ChunkSize - 1)),
      rng.next_f32() * TwoPi,
      range_f32(rng, -0.22F, 0.22F),
      static_cast<f32>(
          rng.range_i32(config.length_blocks.min, config.length_blocks.max)),
      range_f32(rng, config.radius.min, config.radius.max),
      range_f32(
          rng,
          config.vertical_radius_multiplier.min,
          config.vertical_radius_multiplier.max)};
}

void carve_ellipsoid(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    GeneratedChunkEditView& view,
    const CarverConfig& carver,
    u64 dimension_seed,
    f32 center_x,
    f32 center_y,
    f32 center_z,
    f32 radius_xz,
    f32 radius_y,
    i32 effective_min_y,
    i32 effective_max_y) {
  if (view.sections == nullptr || radius_xz <= 0.0F || radius_y <= 0.0F) {
    return;
  }

  const i32 chunk_min_x = view.coord.x * ChunkSize;
  const i32 chunk_min_z = view.coord.z * ChunkSize;
  const i32 min_x = std::max(chunk_min_x, static_cast<i32>(std::floor(center_x - radius_xz)));
  const i32 max_x = std::min(
      chunk_min_x + ChunkSize - 1,
      static_cast<i32>(std::ceil(center_x + radius_xz)));
  const i32 min_z = std::max(chunk_min_z, static_cast<i32>(std::floor(center_z - radius_xz)));
  const i32 max_z = std::min(
      chunk_min_z + ChunkSize - 1,
      static_cast<i32>(std::ceil(center_z + radius_xz)));
  const i32 min_y =
      std::max(effective_min_y, static_cast<i32>(std::floor(center_y - radius_y)));
  const i32 max_y =
      std::min(effective_max_y, static_cast<i32>(std::ceil(center_y + radius_y)));
  if (min_x > max_x || min_y > max_y || min_z > max_z) {
    return;
  }

  const f32 inv_radius_xz_sq = 1.0F / (radius_xz * radius_xz);
  const f32 inv_radius_y_sq = 1.0F / (radius_y * radius_y);
  const u64 block_salt = carver_runtime_salt(carver, carver.tunnel.salt);

  for (i32 world_y = min_y; world_y <= max_y; ++world_y) {
    const std::optional<usize> section_index = view.section_index(floor_div(world_y, ChunkSize));
    if (!section_index || section_index.value() >= view.sections->size()) {
      continue;
    }
    GeneratedChunkSection& section = (*view.sections)[section_index.value()];
    for (i32 world_z = min_z; world_z <= max_z; ++world_z) {
      const i32 local_z = world_z - chunk_min_z;
      for (i32 world_x = min_x; world_x <= max_x; ++world_x) {
        const i32 local_x = world_x - chunk_min_x;
        const usize column_index = voxel::local_column_index({local_x, local_z});
        if (world_y > profile.surface_y_by_column[column_index] - carver.surface_margin) {
          continue;
        }

        const f32 dx = static_cast<f32>(world_x) + 0.5F - center_x;
        const f32 dy = static_cast<f32>(world_y) + 0.5F - center_y;
        const f32 dz = static_cast<f32>(world_z) + 0.5F - center_z;
        const f32 normalized =
            (dx * dx + dz * dz) * inv_radius_xz_sq + dy * dy * inv_radius_y_sq;
        const f32 edge_jitter =
            (hash_to_unit_float(coordinate_seed(
                 dimension_seed,
                 world_x,
                 world_y,
                 world_z,
                 block_salt)) -
             0.5F) *
            0.16F;
        if (normalized > 1.0F + edge_jitter) {
          continue;
        }

        const usize block_index = voxel::local_index(block_to_local({{world_x, world_y, world_z}}));
        if (section.blocks[block_index] == voxel::AirBlockVariantID ||
            section.blocks[block_index] == dimension.water_block) {
          continue;
        }
        section.blocks[block_index] = voxel::AirBlockVariantID;
      }
    }
  }
}

void carve_tunnel_path(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    GeneratedChunkEditView& view,
    const CarverConfig& carver,
    u64 dimension_seed,
    DeterministicRng& rng,
    TunnelPathState path,
    i32 effective_min_y,
    i32 effective_max_y,
    bool allow_branches) {
  const TunnelCaveCarverConfig& config = carver.tunnel;
  constexpr f32 step_blocks = 5.0F;
  const i32 steps = std::max(4, static_cast<i32>(std::ceil(path.length / step_blocks)));
  const bool has_room = rng.chance(config.room_chance);
  const i32 room_step = has_room ? rng.range_i32(1, steps - 2) : -1;
  const f32 room_radius = has_room
                              ? range_f32(rng, config.room_radius.min, config.room_radius.max)
                              : 0.0F;

  std::vector<i32> branch_steps;
  if (allow_branches && rng.chance(config.branch_chance) && config.branch_count.max > 0) {
    const i32 count = rng.range_i32(config.branch_count.min, config.branch_count.max);
    branch_steps.reserve(static_cast<usize>(std::max(0, count)));
    for (i32 index = 0; index < count; ++index) {
      branch_steps.push_back(rng.range_i32(std::max(1, steps / 4), std::max(1, (steps * 3) / 4)));
    }
  }

  for (i32 step = 0; step < steps; ++step) {
    const f32 t = static_cast<f32>(step) / static_cast<f32>(std::max(1, steps - 1));
    const f32 taper = std::sin(t * 3.14159265359F);
    const f32 radius_jitter = range_f32(rng, 0.88F, 1.12F);
    const f32 radius = std::max(0.75F, path.radius * (0.55F + 0.45F * taper) * radius_jitter);
    const f32 vertical_radius =
        std::max(0.65F, radius * path.vertical_radius_multiplier);
    carve_ellipsoid(
        dimension,
        profile,
        view,
        carver,
        dimension_seed,
        path.x,
        path.y,
        path.z,
        radius,
        vertical_radius,
        effective_min_y,
        effective_max_y);

    if (step == room_step) {
      carve_ellipsoid(
          dimension,
          profile,
          view,
          carver,
          dimension_seed,
          path.x,
          path.y,
          path.z,
          room_radius,
          std::max(1.0F, room_radius * 0.72F),
          effective_min_y,
          effective_max_y);
    }

    for (const i32 branch_step : branch_steps) {
      if (branch_step != step) {
        continue;
      }
      TunnelPathState branch = path;
      branch.yaw += range_f32(rng, 0.75F, 2.2F) * (rng.chance(0.5F) ? -1.0F : 1.0F);
      branch.pitch = range_f32(rng, -0.28F, 0.28F);
      branch.length *= range_f32(rng, 0.32F, 0.58F);
      branch.radius *= range_f32(rng, 0.55F, 0.72F);
      branch.vertical_radius_multiplier =
          range_f32(rng, config.vertical_radius_multiplier.min, config.vertical_radius_multiplier.max);
      carve_tunnel_path(
          dimension,
          profile,
          view,
          carver,
          dimension_seed,
          rng,
          branch,
          effective_min_y,
          effective_max_y,
          false);
    }

    path.yaw += range_f32(rng, -config.horizontal_curl, config.horizontal_curl);
    path.pitch = std::clamp(
        path.pitch + range_f32(rng, -config.vertical_curl, config.vertical_curl),
        -0.45F,
        0.45F);
    const f32 horizontal = std::cos(path.pitch) * step_blocks;
    path.x += std::cos(path.yaw) * horizontal;
    path.y += std::sin(path.pitch) * step_blocks;
    path.z += std::sin(path.yaw) * horizontal;
  }
}

void apply_noise_cave_carver(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    GeneratedChunkEditView& view,
    const CarverConfig& carver,
    u64 dimension_seed) {
  const NoiseSampler sampler{carver.noise, dimension_seed};
  if (view.sections == nullptr) {
    return;
  }
  const i32 effective_min_y = effective_carver_min_y(dimension, carver);
  const i32 effective_max_y = effective_carver_max_y(dimension, carver);
  if (effective_min_y > effective_max_y) {
    return;
  }
  for (GeneratedChunkSection& section : *view.sections) {
    const i32 section_min_y = section.coord.value.y * ChunkSize;
    const i32 section_max_y = section_min_y + ChunkSize - 1;
    if (section_max_y < effective_min_y || section_min_y > effective_max_y) {
      continue;
    }
    for (usize index = 0; index < voxel::ChunkVolume; ++index) {
      if (section.blocks[index] == voxel::AirBlockVariantID ||
          section.blocks[index] == dimension.water_block) {
        continue;
      }
      const SectionLocalPos local = voxel::local_pos(index);
      const usize column_index = voxel::local_column_index({local.value.x, local.value.z});
      const i32 world_x = view.coord.x * ChunkSize + local.value.x;
      const i32 world_y = section_min_y + local.value.y;
      const i32 world_z = view.coord.z * ChunkSize + local.value.z;
      if (world_y < effective_min_y || world_y > effective_max_y ||
          world_y > profile.surface_y_by_column[column_index] - carver.surface_margin) {
        continue;
      }
      if (sampler.sample_3d(
              static_cast<f32>(world_x),
              static_cast<f32>(world_y),
              static_cast<f32>(world_z)) > carver.threshold) {
        section.blocks[index] = voxel::AirBlockVariantID;
      }
    }
  }
}

void carve_ravine_candidate(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    GeneratedChunkEditView& view,
    const CarverConfig& carver,
    const RavineCandidate& ravine) {
  if (view.sections == nullptr) {
    return;
  }
  const i32 effective_min_y = effective_carver_min_y(dimension, carver);
  const i32 effective_max_y = effective_carver_max_y(dimension, carver);
  if (effective_min_y > effective_max_y) {
    return;
  }
  const i32 chunk_min_x = view.coord.x * ChunkSize;
  const i32 chunk_min_z = view.coord.z * ChunkSize;
  const f32 max_width = std::max(0.75F, ravine.width);
  const i32 min_x = std::max(
      chunk_min_x,
      static_cast<i32>(std::floor(std::min(ravine.start_x, ravine.end_x) - max_width - 1.0F)));
  const i32 max_x = std::min(
      chunk_min_x + ChunkSize - 1,
      static_cast<i32>(std::ceil(std::max(ravine.start_x, ravine.end_x) + max_width + 1.0F)));
  const i32 min_z = std::max(
      chunk_min_z,
      static_cast<i32>(std::floor(std::min(ravine.start_z, ravine.end_z) - max_width - 1.0F)));
  const i32 max_z = std::min(
      chunk_min_z + ChunkSize - 1,
      static_cast<i32>(std::ceil(std::max(ravine.start_z, ravine.end_z) + max_width + 1.0F)));
  if (min_x > max_x || min_z > max_z) {
    return;
  }

  for (i32 world_z = min_z; world_z <= max_z; ++world_z) {
    const i32 local_z = world_z - chunk_min_z;
    for (i32 world_x = min_x; world_x <= max_x; ++world_x) {
      const i32 local_x = world_x - chunk_min_x;
      const usize column_index = voxel::local_column_index({local_x, local_z});
      const i32 top_y = std::min(
          effective_max_y,
          profile.surface_y_by_column[column_index] - carver.surface_margin);
      const i32 bottom_y = std::max(effective_min_y, top_y - ravine.depth);
      if (bottom_y > top_y) {
        continue;
      }
      const f32 distance_sq = horizontal_distance_sq_to_segment(
          ravine,
          static_cast<f32>(world_x),
          static_cast<f32>(world_z));
      if (distance_sq > max_width * max_width) {
        continue;
      }

      for (i32 world_y = bottom_y; world_y <= top_y; ++world_y) {
        const f32 vertical_t =
            static_cast<f32>(top_y - world_y) /
            static_cast<f32>(std::max(1, top_y - bottom_y));
        const f32 width_at_y = std::max(0.75F, ravine.width * (1.0F - 0.72F * vertical_t));
        if (distance_sq > width_at_y * width_at_y) {
          continue;
        }
        const std::optional<usize> section_index = view.section_index(floor_div(world_y, ChunkSize));
        if (!section_index || section_index.value() >= view.sections->size()) {
          continue;
        }
        GeneratedChunkSection& section = (*view.sections)[section_index.value()];
        const usize index = voxel::local_index(block_to_local({{world_x, world_y, world_z}}));
        if (section.blocks[index] == voxel::AirBlockVariantID ||
            section.blocks[index] == dimension.water_block) {
          continue;
        }
        section.blocks[index] = voxel::AirBlockVariantID;
      }
    }
  }
}

void apply_ravine_carver(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    GeneratedChunkEditView& view,
    const CarverConfig& carver,
    u64 dimension_seed) {
  const RavineCarverConfig& config = carver.ravine;
  const i32 region_chunks = std::max(1, config.region_size_chunks);
  const i32 radius_chunks = std::max(1, config.length_chunks.max + 1);
  const i32 min_cell_x = floor_div(view.coord.x - radius_chunks, region_chunks);
  const i32 max_cell_x = floor_div(view.coord.x + radius_chunks, region_chunks);
  const i32 min_cell_z = floor_div(view.coord.z - radius_chunks, region_chunks);
  const i32 max_cell_z = floor_div(view.coord.z + radius_chunks, region_chunks);

  for (i32 cell_z = min_cell_z; cell_z <= max_cell_z; ++cell_z) {
    for (i32 cell_x = min_cell_x; cell_x <= max_cell_x; ++cell_x) {
      if (!ravine_passes_chance(carver, dimension_seed, cell_x, cell_z)) {
        continue;
      }
      carve_ravine_candidate(
          dimension,
          profile,
          view,
          carver,
          make_ravine_candidate(carver, dimension_seed, cell_x, cell_z));
    }
  }
}

void apply_tunnel_cave_carver(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    GeneratedChunkEditView& view,
    const CarverConfig& carver,
    u64 dimension_seed) {
  if (view.sections == nullptr) {
    return;
  }
  const TunnelCaveCarverConfig& config = carver.tunnel;
  const i32 effective_min_y = effective_carver_min_y(dimension, carver);
  const i32 effective_max_y = effective_carver_max_y(dimension, carver);
  if (effective_min_y > effective_max_y ||
      std::max(effective_min_y, config.start_y.min) >
          std::min(effective_max_y, config.start_y.max)) {
    return;
  }

  const i32 region_chunks = std::max(1, config.region_size_chunks);
  const i32 max_extent_blocks =
      config.length_blocks.max + config.length_blocks.max / 2 +
      static_cast<i32>(std::ceil(config.room_radius.max + config.radius.max)) + ChunkSize;
  const i32 radius_chunks = std::max(1, (max_extent_blocks + ChunkSize - 1) / ChunkSize);
  const i32 min_cell_x = floor_div(view.coord.x - radius_chunks, region_chunks);
  const i32 max_cell_x = floor_div(view.coord.x + radius_chunks, region_chunks);
  const i32 min_cell_z = floor_div(view.coord.z - radius_chunks, region_chunks);
  const i32 max_cell_z = floor_div(view.coord.z + radius_chunks, region_chunks);
  const u64 salt = carver_runtime_salt(carver, config.salt);

  for (i32 cell_z = min_cell_z; cell_z <= max_cell_z; ++cell_z) {
    for (i32 cell_x = min_cell_x; cell_x <= max_cell_x; ++cell_x) {
      DeterministicRng rng{coordinate_seed(
          dimension_seed,
          cell_x,
          static_cast<i32>(config.salt & 0x7FFFFFFFULL),
          cell_z,
          salt)};
      if (!rng.chance(config.chance_per_region)) {
        continue;
      }
      const i32 tunnels =
          rng.range_i32(config.tunnels_per_region.min, config.tunnels_per_region.max);
      for (i32 tunnel = 0; tunnel < tunnels; ++tunnel) {
        carve_tunnel_path(
            dimension,
            profile,
            view,
            carver,
            dimension_seed,
            rng,
            make_tunnel_path(
                carver,
                rng,
                cell_x,
                cell_z,
                effective_min_y,
                effective_max_y),
            effective_min_y,
            effective_max_y,
            true);
      }
    }
  }
}

} // namespace

void apply_carvers(
    const ResolvedDimension& dimension,
    const TerrainProfile& profile,
    GeneratedChunkEditView& view,
    u64 seed) {
  ARV_PROFILE_ZONE("Worldgen carver runtime");
  const u64 dimension_seed = salted_seed(seed, dimension.raw.seed_salt, dimension.raw.version);
  for (const CarverConfig& carver : dimension.carvers) {
    if (!carver.enabled) {
      continue;
    }
    switch (carver.carver_type) {
      case CarverType::NoiseCaves:
        apply_noise_cave_carver(dimension, profile, view, carver, dimension_seed);
        break;
      case CarverType::Ravines:
        apply_ravine_carver(dimension, profile, view, carver, dimension_seed);
        break;
      case CarverType::TunnelCaves:
        apply_tunnel_cave_carver(dimension, profile, view, carver, dimension_seed);
        break;
    }
  }
}

} // namespace arv::world::worldgen
