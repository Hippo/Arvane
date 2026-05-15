#include "arv/world/worldgen/blueprint_placement.hpp"

#include "arv/world/worldgen/seeds.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

namespace arv::world::worldgen {

BlockPos blueprint_local_to_world(
    const blueprint::Blueprint& blueprint,
    BlockPos origin,
    BlockPos local,
    StructureRotationY rotation) noexcept {
  const Vec3i relative{
      local.value.x - blueprint.origin.value.x,
      local.value.y - blueprint.origin.value.y,
      local.value.z - blueprint.origin.value.z};
  const Vec3i rotated = rotate_blueprint_relative_y(relative, rotation);
  return {{
      origin.value.x + rotated.x,
      origin.value.y + rotated.y,
      origin.value.z + rotated.z,
  }};
}

std::optional<BlockPos> blueprint_world_to_local(
    const blueprint::Blueprint& blueprint,
    BlockPos origin,
    BlockPos world,
    StructureRotationY rotation) noexcept {
  const Vec3i relative{
      world.value.x - origin.value.x,
      world.value.y - origin.value.y,
      world.value.z - origin.value.z};
  const Vec3i unrotated = inverse_rotate_blueprint_relative_y(relative, rotation);
  const BlockPos local{{
      unrotated.x + blueprint.origin.value.x,
      unrotated.y + blueprint.origin.value.y,
      unrotated.z + blueprint.origin.value.z,
  }};
  if (local.value.x < 0 || local.value.x >= blueprint.size.value.x ||
      local.value.y < 0 || local.value.y >= blueprint.size.value.y ||
      local.value.z < 0 || local.value.z >= blueprint.size.value.z) {
    return std::nullopt;
  }
  return local;
}

BlockAabb blueprint_world_bounds(
    const blueprint::Blueprint& blueprint,
    BlockPos origin,
    StructureRotationY rotation) noexcept {
  BlockAabb bounds{
      {{std::numeric_limits<i32>::max(), std::numeric_limits<i32>::max(), std::numeric_limits<i32>::max()}},
      {{std::numeric_limits<i32>::min(), std::numeric_limits<i32>::min(), std::numeric_limits<i32>::min()}}};
  for (const i32 y : {0, blueprint.size.value.y - 1}) {
    for (const i32 z : {0, blueprint.size.value.z - 1}) {
      for (const i32 x : {0, blueprint.size.value.x - 1}) {
        const BlockPos world = blueprint_local_to_world(blueprint, origin, {{x, y, z}}, rotation);
        bounds.min.value.x = std::min(bounds.min.value.x, world.value.x);
        bounds.min.value.y = std::min(bounds.min.value.y, world.value.y);
        bounds.min.value.z = std::min(bounds.min.value.z, world.value.z);
        bounds.max.value.x = std::max(bounds.max.value.x, world.value.x);
        bounds.max.value.y = std::max(bounds.max.value.y, world.value.y);
        bounds.max.value.z = std::max(bounds.max.value.z, world.value.z);
      }
    }
  }
  return bounds;
}

i32 blueprint_max_horizontal_extent_blocks(const blueprint::Blueprint& blueprint) noexcept {
  i32 max_extent = 0;
  for (const StructureRotationY rotation : {
           StructureRotationY::Deg0,
           StructureRotationY::Deg90,
           StructureRotationY::Deg180,
           StructureRotationY::Deg270}) {
    const BlockAabb bounds = blueprint_world_bounds(blueprint, {{0, 0, 0}}, rotation);
    max_extent = std::max(max_extent, std::abs(bounds.min.value.x));
    max_extent = std::max(max_extent, std::abs(bounds.max.value.x));
    max_extent = std::max(max_extent, std::abs(bounds.min.value.z));
    max_extent = std::max(max_extent, std::abs(bounds.max.value.z));
  }
  return max_extent + 1;
}

bool GeneratedChunkEditView::contains(BlockPos pos) const noexcept {
  return floor_div(pos.value.x, ChunkSize) == coord.x &&
         floor_div(pos.value.z, ChunkSize) == coord.z &&
         section_index(floor_div(pos.value.y, ChunkSize)).has_value();
}

std::optional<usize> GeneratedChunkEditView::section_index(i32 chunk_y) const noexcept {
  if (chunk_y < min_chunk_y || chunk_y > max_chunk_y) {
    return std::nullopt;
  }
  return static_cast<usize>(chunk_y - min_chunk_y);
}

voxel::BlockVariantID GeneratedChunkEditView::get(BlockPos pos) const noexcept {
  if (floor_div(pos.value.x, ChunkSize) != coord.x ||
      floor_div(pos.value.z, ChunkSize) != coord.z) {
    return voxel::AirBlockVariantID;
  }
  const std::optional<usize> section = section_index(floor_div(pos.value.y, ChunkSize));
  if (!section || section.value() >= sections->size()) {
    return voxel::AirBlockVariantID;
  }
  return (*sections)[section.value()].blocks[voxel::local_index(block_to_local(pos))];
}

void GeneratedChunkEditView::set(BlockPos pos, voxel::BlockVariantID block) noexcept {
  if (floor_div(pos.value.x, ChunkSize) != coord.x ||
      floor_div(pos.value.z, ChunkSize) != coord.z) {
    return;
  }
  const std::optional<usize> section = section_index(floor_div(pos.value.y, ChunkSize));
  if (!section || section.value() >= sections->size()) {
    return;
  }
  (*sections)[section.value()].blocks[voxel::local_index(block_to_local(pos))] = block;
}

namespace {

[[nodiscard]] bool replacement_allows(
    voxel::BlockVariantID existing,
    const ResolvedReplaceRule& replace) noexcept {
  if (!replace.specified) {
    return true;
  }
  if (replace.only_air) {
    return existing == voxel::AirBlockVariantID;
  }
  if (!replace.blocks.empty()) {
    return std::find(replace.blocks.begin(), replace.blocks.end(), existing) !=
           replace.blocks.end();
  }
  return false;
}

[[nodiscard]] usize blueprint_block_index(
    const blueprint::Blueprint& blueprint,
    BlockPos local) noexcept {
  return static_cast<usize>(local.value.x) +
         static_cast<usize>(blueprint.size.value.x) *
             (static_cast<usize>(local.value.z) +
              static_cast<usize>(blueprint.size.value.z) *
                  static_cast<usize>(local.value.y));
}

} // namespace

void place_blueprint_intersection(
    const BlueprintPlacementInput& input,
    GeneratedChunkEditView& view) {
  if (input.feature == nullptr) {
    return;
  }
  const ResolvedConfiguredFeature& feature = *input.feature;
  const auto* blueprint_feature = std::get_if<BlueprintFeatureConfig>(&feature.raw.config);
  if (blueprint_feature == nullptr) {
    return;
  }

  const blueprint::Blueprint& blueprint = feature.blueprint;
  const std::optional<BlockAabb> overlap =
      intersection(input.bounds, chunk_column_bounds(view.coord, view.min_chunk_y, view.max_chunk_y));
  if (!overlap.has_value()) {
    return;
  }

  const f32 integrity = std::clamp(blueprint_feature->integrity, 0.0F, 1.0F);
  const u64 resource_hash =
      mix64(fnv1a(feature.id)) ^ mix64(fnv1a(blueprint_feature->blueprint.id));
  for (i32 y = overlap->min.value.y; y <= overlap->max.value.y; ++y) {
    for (i32 z = overlap->min.value.z; z <= overlap->max.value.z; ++z) {
      for (i32 x = overlap->min.value.x; x <= overlap->max.value.x; ++x) {
        const BlockPos world{{x, y, z}};
        const std::optional<BlockPos> local =
            blueprint_world_to_local(blueprint, input.origin, world, input.rotation);
        if (!local.has_value()) {
          continue;
        }
        const usize index = blueprint_block_index(blueprint, local.value());
        if (index >= blueprint.blocks.size()) {
          continue;
        }
        const u32 palette_index = blueprint.blocks[index];
        if (palette_index >= feature.blueprint_palette.size()) {
          continue;
        }
        const voxel::BlockVariantID block = feature.blueprint_palette[palette_index];
        if (block == voxel::AirBlockVariantID) {
          continue;
        }
        if (integrity < 1.0F &&
            hash_to_unit_float(structure_block_seed(
                input.world_seed,
                input.salt,
                input.origin,
                local.value(),
                resource_hash)) > integrity) {
          continue;
        }
        if (!replacement_allows(view.get(world), feature.replace)) {
          continue;
        }
        view.set(world, block);
      }
    }
  }
}

} // namespace arv::world::worldgen
