#include "arv/sched/job_system.hpp"
#include "arv/asset/pack.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"
#include "arv/world/worldgen/blueprint_placement.hpp"
#include "arv/world/worldgen/chunk_generator.hpp"
#include "arv/world/worldgen/loader.hpp"
#include "arv/world/worldgen/seeds.hpp"
#include "arv/world/worldgen/structure_candidate.hpp"
#include "arv/world/worldgen/terrain_query.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

arv::voxel::BlockRegistry make_worldgen_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:dirt"));
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:grass_block"));
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:stone"));
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:sand"));
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:iron_ore"));
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:oak_log"));
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:oak_leaves"));
  arv::voxel::BlockDefinition short_grass =
      arv::voxel::make_full_cube_definition("arvane:short_grass");
  short_grass.properties.replaceable = true;
  definitions.push_back(std::move(short_grass));
  arv::voxel::BlockDefinition water;
  water.name = "arvane:water";
  water.render_category = arv::voxel::RenderCategory::Transparent;
  water.is_item = false;
  water.properties.replaceable = true;
  water.properties.fluid = true;
  definitions.push_back(std::move(water));

  auto result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(result);
  return std::move(result.value());
}

arv::world::WorldGenerationConfig make_pack_worldgen_config(
    const arv::voxel::BlockRegistry& registry) {
  auto packs = arv::asset::PackManager::load_from_packs_root("arvane_home/packs");
  EXPECT_TRUE(packs) << packs.error().describe();
  auto worldgen_registry =
      arv::world::worldgen::load_worldgen_registry_from_packs(packs.value());
  EXPECT_TRUE(worldgen_registry) << worldgen_registry.error().describe();
  auto resolved = arv::world::worldgen::resolve_dimension(
      worldgen_registry.value(),
      "arvane:worldgen/dimension/overworld",
      registry);
  EXPECT_TRUE(resolved) << resolved.error().describe();
  return arv::world::config_from_resolved_dimension(std::move(resolved.value()));
}

arv::world::worldgen::ResolvedDimension make_two_biome_blend_dimension() {
  namespace worldgen = arv::world::worldgen;

  worldgen::ResolvedDimension dimension;
  dimension.raw.id = "arvane:test/two_biome_blend";
  dimension.raw.version = 1;
  dimension.raw.seed_salt = "two_biome_blend";
  dimension.raw.vertical.min_y = -32;
  dimension.raw.vertical.max_y = 96;
  dimension.raw.vertical.sea_level = 0;
  dimension.terrain.height.base = 32;
  dimension.terrain.height.scale = 0;
  dimension.terrain.height.detail_scale = 0;
  dimension.terrain.primary_noise.seed_salt = "flat_primary";
  dimension.terrain.primary_noise.frequency = 0.01F;
  dimension.terrain.primary_noise.amplitude = 0.0F;
  dimension.terrain.detail_noise.seed_salt = "flat_detail";
  dimension.terrain.detail_noise.frequency = 0.01F;
  dimension.terrain.detail_noise.amplitude = 0.0F;

  worldgen::BiomeConfig low;
  low.id = "arvane:worldgen/biome/test_low";
  low.terrain.base_height_offset = -16.0F;
  low.terrain.height_scale_multiplier = 1.0F;
  low.terrain.roughness_multiplier = 1.0F;
  worldgen::BiomeConfig high;
  high.id = "arvane:worldgen/biome/test_high";
  high.terrain.base_height_offset = 16.0F;
  high.terrain.height_scale_multiplier = 1.0F;
  high.terrain.roughness_multiplier = 1.0F;
  dimension.biomes.push_back({std::move(low), std::nullopt});
  dimension.biomes.push_back({std::move(high), std::nullopt});

  worldgen::NoiseFieldConfig climate;
  climate.type = worldgen::NoiseType::Simplex2D;
  climate.type_id = "arvane:noise/simplex_2d";
  climate.seed_salt = "blend_temperature";
  climate.frequency = 0.025F;
  climate.octaves = 1;
  climate.amplitude = 1.0F;
  climate.lacunarity = 2.0F;
  climate.gain = 0.5F;
  dimension.biome_source.raw.id = "arvane:test/biome_source/two_biome_blend";
  dimension.biome_source.raw.sample_scale_xz = 4;
  dimension.biome_source.fields.push_back({"temperature", climate});

  worldgen::ResolvedBiomeSourceEntry low_entry;
  low_entry.biome_index = 0;
  low_entry.climate.push_back({"temperature", {-1.0F, 0.0F}});
  low_entry.resolved_climate.push_back({0U, {-1.0F, 0.0F}});
  worldgen::ResolvedBiomeSourceEntry high_entry;
  high_entry.biome_index = 1;
  high_entry.climate.push_back({"temperature", {0.0F, 1.0F}});
  high_entry.resolved_climate.push_back({0U, {0.0F, 1.0F}});
  dimension.biome_source.entries.push_back(std::move(low_entry));
  dimension.biome_source.entries.push_back(std::move(high_entry));
  return dimension;
}

arv::world::worldgen::ResolvedDimension make_flat_tree_dimension(
    const arv::voxel::BlockRegistry& registry) {
  namespace worldgen = arv::world::worldgen;

  worldgen::ResolvedDimension dimension;
  dimension.raw.id = "arvane:test/cross_chunk_tree";
  dimension.raw.version = 1;
  dimension.raw.seed_salt = "cross_chunk_tree";
  dimension.raw.vertical.min_y = 0;
  dimension.raw.vertical.max_y = 31;
  dimension.raw.vertical.sea_level = -1;
  dimension.terrain.height.base = 8;
  dimension.terrain.height.scale = 0;
  dimension.terrain.height.detail_scale = 0;
  dimension.terrain.primary_noise.seed_salt = "flat_primary";
  dimension.terrain.detail_noise.seed_salt = "flat_detail";

  worldgen::BiomeConfig biome;
  biome.id = "arvane:worldgen/biome/test_grassy";
  dimension.biomes.push_back({std::move(biome), std::nullopt});

  worldgen::ResolvedSurfaceRule rule;
  rule.layers.push_back({registry.find_variant("arvane:grass_block").value(), 1, false});
  rule.layers.push_back({registry.find_variant("arvane:dirt").value(), 1, true});
  dimension.surface_rule_sets.push_back({"arvane:worldgen/surface_rule_set/test_grassy", {rule}});
  dimension.dimension_surface_rule_set = 0U;

  worldgen::SimpleTreeFeatureConfig tree_config;
  tree_config.trunk_block = "arvane:oak_log";
  tree_config.trunk_height = {4, 4};
  tree_config.leaves_block = "arvane:oak_leaves";
  tree_config.leaves_radius = {2, 2};

  worldgen::ResolvedConfiguredFeature tree;
  tree.id = "arvane:worldgen/configured_feature/test_oak_tree";
  tree.type = "arvane:feature/simple_tree";
  tree.raw.id = tree.id;
  tree.raw.version = 1;
  tree.raw.type = tree.type;
  tree.raw.config = tree_config;
  tree.blocks = {
      registry.find_variant("arvane:oak_log").value(),
      registry.find_variant("arvane:oak_leaves").value()};
  tree.radius_blocks = 4;
  dimension.configured_features.push_back(std::move(tree));

  worldgen::ResolvedPlacedFeature placed;
  placed.raw.id = "arvane:worldgen/placed_feature/test_oak_tree";
  placed.raw.version = 1;
  placed.raw.feature = {"arvane:worldgen/configured_feature/test_oak_tree"};
  placed.raw.salt = 9871;
  placed.raw.stage = worldgen::GenerationStage::SurfaceDecoration;
  placed.raw.count.type = worldgen::PlacementCountConfig::Type::Uniform;
  placed.raw.count.min = 1;
  placed.raw.count.max = 1;
  placed.raw.height.type = worldgen::PlacementHeightConfig::Type::Surface;
  placed.feature_index = 0U;
  placed.radius_blocks = dimension.configured_features.front().radius_blocks;
  dimension.placed_features.push_back(std::move(placed));
  dimension.dimension_features_by_stage[static_cast<arv::usize>(worldgen::GenerationStage::SurfaceDecoration)]
      .push_back(0U);

  return dimension;
}

arv::world::worldgen::ResolvedDimension make_tree_with_surface_decoration_dimension(
    const arv::voxel::BlockRegistry& registry,
    arv::u64 placement_salt) {
  namespace worldgen = arv::world::worldgen;

  worldgen::ResolvedDimension dimension;
  dimension.raw.id = "arvane:test/tree_surface_decoration";
  dimension.raw.version = 1;
  dimension.raw.seed_salt = "tree_surface_decoration";
  dimension.raw.vertical.min_y = 0;
  dimension.raw.vertical.max_y = 31;
  dimension.raw.vertical.sea_level = -1;
  dimension.terrain.height.base = 8;
  dimension.terrain.height.scale = 0;
  dimension.terrain.height.detail_scale = 0;
  dimension.terrain.primary_noise.seed_salt = "flat_primary";
  dimension.terrain.detail_noise.seed_salt = "flat_detail";

  worldgen::BiomeConfig biome;
  biome.id = "arvane:worldgen/biome/test_grassy";
  dimension.biomes.push_back({std::move(biome), std::nullopt});

  worldgen::ResolvedSurfaceRule rule;
  rule.layers.push_back({registry.find_variant("arvane:grass_block").value(), 1, false});
  rule.layers.push_back({registry.find_variant("arvane:dirt").value(), 1, true});
  dimension.surface_rule_sets.push_back({"arvane:worldgen/surface_rule_set/test_grassy", {rule}});
  dimension.dimension_surface_rule_set = 0U;

  const arv::voxel::BlockVariantID grass = registry.find_variant("arvane:grass_block").value();
  const arv::voxel::BlockVariantID short_grass =
      registry.find_variant("arvane:short_grass").value();
  const arv::voxel::BlockVariantID log = registry.find_variant("arvane:oak_log").value();
  const arv::voxel::BlockVariantID leaves = registry.find_variant("arvane:oak_leaves").value();
  dimension.replaceable_blocks.push_back(short_grass);

  worldgen::SingleBlockFeatureConfig decoration_config;
  decoration_config.block = "arvane:short_grass";
  decoration_config.replace.specified = true;
  decoration_config.replace.only_air = true;
  decoration_config.place_on.blocks.push_back("arvane:grass_block");

  worldgen::ResolvedConfiguredFeature decoration;
  decoration.id = "arvane:worldgen/configured_feature/test_surface_decoration";
  decoration.type = "arvane:feature/single_block";
  decoration.raw.id = decoration.id;
  decoration.raw.version = 1;
  decoration.raw.type = decoration.type;
  decoration.raw.config = decoration_config;
  decoration.blocks = {short_grass};
  decoration.support_blocks = {grass};
  decoration.replace.specified = true;
  decoration.replace.only_air = true;
  decoration.radius_blocks = 0;
  dimension.configured_features.push_back(std::move(decoration));

  worldgen::SimpleTreeFeatureConfig tree_config;
  tree_config.trunk_block = "arvane:oak_log";
  tree_config.trunk_height = {4, 4};
  tree_config.leaves_block = "arvane:oak_leaves";
  tree_config.leaves_radius = {2, 2};

  worldgen::ResolvedConfiguredFeature tree;
  tree.id = "arvane:worldgen/configured_feature/test_oak_tree";
  tree.type = "arvane:feature/simple_tree";
  tree.raw.id = tree.id;
  tree.raw.version = 1;
  tree.raw.type = tree.type;
  tree.raw.config = tree_config;
  tree.blocks = {log, leaves};
  tree.radius_blocks = 4;
  dimension.configured_features.push_back(std::move(tree));

  worldgen::ResolvedPlacedFeature decoration_placed;
  decoration_placed.raw.id = "arvane:worldgen/placed_feature/test_surface_decoration";
  decoration_placed.raw.version = 1;
  decoration_placed.raw.feature = {"arvane:worldgen/configured_feature/test_surface_decoration"};
  decoration_placed.raw.salt = placement_salt;
  decoration_placed.raw.stage = worldgen::GenerationStage::SurfaceDecoration;
  decoration_placed.raw.count.type = worldgen::PlacementCountConfig::Type::Uniform;
  decoration_placed.raw.count.min = 1;
  decoration_placed.raw.count.max = 1;
  decoration_placed.raw.height.type = worldgen::PlacementHeightConfig::Type::Surface;
  decoration_placed.feature_index = 0U;
  decoration_placed.radius_blocks = 0;
  dimension.placed_features.push_back(std::move(decoration_placed));

  worldgen::ResolvedPlacedFeature tree_placed;
  tree_placed.raw.id = "arvane:worldgen/placed_feature/test_oak_tree";
  tree_placed.raw.version = 1;
  tree_placed.raw.feature = {"arvane:worldgen/configured_feature/test_oak_tree"};
  tree_placed.raw.salt = placement_salt;
  tree_placed.raw.stage = worldgen::GenerationStage::SurfaceDecoration;
  tree_placed.raw.count.type = worldgen::PlacementCountConfig::Type::Uniform;
  tree_placed.raw.count.min = 1;
  tree_placed.raw.count.max = 1;
  tree_placed.raw.height.type = worldgen::PlacementHeightConfig::Type::Surface;
  tree_placed.feature_index = 1U;
  tree_placed.radius_blocks = dimension.configured_features[1].radius_blocks;
  dimension.placed_features.push_back(std::move(tree_placed));

  dimension.dimension_features_by_stage[static_cast<arv::usize>(worldgen::GenerationStage::SurfaceDecoration)]
      .push_back(0U);
  dimension.dimension_features_by_stage[static_cast<arv::usize>(worldgen::GenerationStage::SurfaceDecoration)]
      .push_back(1U);

  return dimension;
}

arv::blueprint::Blueprint make_filled_blueprint(
    std::string name,
    arv::BlockPos size,
    arv::BlockPos origin,
    std::string block_id,
    std::vector<arv::u32> blocks = {}) {
  arv::blueprint::Blueprint blueprint;
  blueprint.metadata.name = std::move(name);
  blueprint.metadata.author = "test";
  blueprint.metadata.created_by = "world_generation_tests";
  blueprint.size = size;
  blueprint.origin = origin;
  blueprint.palette = {"arvane:air[]", std::move(block_id)};
  const arv::u64 volume = arv::blueprint::blueprint_volume(size);
  if (blocks.empty()) {
    blocks.assign(static_cast<arv::usize>(volume), 1U);
  }
  blueprint.blocks = std::move(blocks);
  return blueprint;
}

arv::world::worldgen::ResolvedDimension make_flat_structure_dimension(
    const arv::voxel::BlockRegistry& registry) {
  namespace worldgen = arv::world::worldgen;

  worldgen::ResolvedDimension dimension;
  dimension.raw.id = "arvane:test/structures";
  dimension.raw.version = 1;
  dimension.raw.seed_salt = "structures";
  dimension.raw.vertical.min_y = 0;
  dimension.raw.vertical.max_y = 31;
  dimension.raw.vertical.sea_level = -1;
  dimension.terrain.height.base = 8;
  dimension.terrain.height.scale = 0;
  dimension.terrain.height.detail_scale = 0;
  dimension.terrain.primary_noise.seed_salt = "flat_primary";
  dimension.terrain.detail_noise.seed_salt = "flat_detail";

  worldgen::BiomeConfig biome;
  biome.id = "arvane:worldgen/biome/test_land";
  biome.tags.push_back({"arvane:biome/land"});
  dimension.biomes.push_back({std::move(biome), std::nullopt});

  worldgen::ResolvedSurfaceRule rule;
  rule.layers.push_back({registry.find_variant("arvane:grass_block").value(), 1, false});
  rule.layers.push_back({registry.find_variant("arvane:dirt").value(), 1, true});
  dimension.surface_rule_sets.push_back({"arvane:worldgen/surface_rule_set/test_land", {rule}});
  dimension.dimension_surface_rule_set = 0U;
  return dimension;
}

arv::usize add_blueprint_feature(
    arv::world::worldgen::ResolvedDimension& dimension,
    const arv::voxel::BlockRegistry& registry,
    std::string id,
    arv::blueprint::Blueprint blueprint,
    arv::voxel::BlockVariantID block,
    arv::f32 integrity = 1.0F,
    arv::world::worldgen::ResolvedReplaceRule replace = {},
    bool random_y_rotation = false) {
  namespace worldgen = arv::world::worldgen;

  worldgen::BlueprintFeatureConfig config;
  config.blueprint = {id + "/blueprint"};
  config.random_y_rotation = random_y_rotation;
  config.integrity = integrity;
  config.replace.specified = replace.specified;
  config.replace.only_air = replace.only_air;
  for (const arv::voxel::BlockVariantID variant : replace.blocks) {
    const arv::voxel::BlockVariantRecord* record = registry.variant(variant);
    if (record != nullptr) {
      config.replace.blocks.push_back(record->debug_name);
    }
  }

  worldgen::ResolvedConfiguredFeature feature;
  feature.id = std::move(id);
  feature.type = "arvane:feature/blueprint";
  feature.raw.id = feature.id;
  feature.raw.version = 1;
  feature.raw.type = feature.type;
  feature.raw.config = config;
  feature.blueprint = std::move(blueprint);
  feature.blueprint_palette = {arv::voxel::AirBlockVariantID, block};
  feature.replace = std::move(replace);
  feature.radius_blocks =
      worldgen::blueprint_max_horizontal_extent_blocks(feature.blueprint);
  dimension.configured_features.push_back(std::move(feature));
  return dimension.configured_features.size() - 1U;
}

arv::usize add_structure_set(
    arv::world::worldgen::ResolvedDimension& dimension,
    std::string id,
    arv::usize feature_index,
    arv::u64 salt,
    arv::i32 average_distance_chunks = 1,
    arv::i32 separation_chunks = 0,
    std::vector<arv::world::worldgen::ResolvedPlacementFilter> filters = {}) {
  namespace worldgen = arv::world::worldgen;

  worldgen::ResolvedStructureSet set;
  set.raw.id = std::move(id);
  set.raw.version = 1;
  set.raw.salt = salt;
  set.raw.placement.type = "grid";
  set.raw.placement.average_distance_chunks = average_distance_chunks;
  set.raw.placement.separation_chunks = separation_chunks;
  set.raw.structures.push_back({{dimension.configured_features[feature_index].id}, 1});
  set.structures.push_back({feature_index, 1});
  set.filters = std::move(filters);
  set.radius_blocks = dimension.configured_features[feature_index].radius_blocks;
  dimension.structure_sets.push_back(std::move(set));
  return dimension.structure_sets.size() - 1U;
}

arv::world::ChunkGenerationRequest make_resolved_request(
    std::shared_ptr<const arv::world::worldgen::ResolvedDimension> dimension,
    arv::u64 seed,
    arv::ChunkCoord coord) {
  arv::world::ChunkGenerationRequest request;
  request.world_id = arv::WorldID{1};
  request.seed = seed;
  request.coord = coord;
  request.min_chunk_y = arv::floor_div(dimension->raw.vertical.min_y, arv::ChunkSize);
  request.max_chunk_y = arv::floor_div(dimension->raw.vertical.max_y, arv::ChunkSize);
  request.config = arv::world::config_from_resolved_dimension(dimension);
  request.resolved_dimension = std::move(dimension);
  return request;
}

arv::voxel::BlockVariantID generated_block_at(
    const arv::world::GeneratedChunk& chunk,
    arv::BlockPos pos) {
  const arv::ChunkSectionCoord coord = arv::block_to_chunk(pos);
  for (const arv::world::GeneratedChunkSection& section : chunk.sections) {
    if (section.coord == coord) {
      return section.blocks[arv::voxel::local_index(arv::block_to_local(pos))];
    }
  }
  return arv::voxel::AirBlockVariantID;
}

arv::voxel::FluidCell generated_fluid_at(
    const arv::world::GeneratedChunk& chunk,
    arv::BlockPos pos) {
  const arv::ChunkSectionCoord coord = arv::block_to_chunk(pos);
  for (const arv::world::GeneratedChunkSection& section : chunk.sections) {
    if (section.coord == coord) {
      return section.fluids[arv::voxel::local_index(arv::block_to_local(pos))];
    }
  }
  return arv::voxel::empty_fluid_cell();
}

std::optional<arv::BlockPos> first_solid_blueprint_block_in_bounds(
    const arv::world::worldgen::ResolvedConfiguredFeature& feature,
    const arv::world::worldgen::StructureCandidate& candidate,
    arv::world::worldgen::BlockAabb bounds) {
  const std::optional<arv::world::worldgen::BlockAabb> overlap =
      arv::world::worldgen::intersection(candidate.bounds, bounds);
  if (!overlap.has_value()) {
    return std::nullopt;
  }
  for (arv::i32 y = overlap->min.value.y; y <= overlap->max.value.y; ++y) {
    for (arv::i32 z = overlap->min.value.z; z <= overlap->max.value.z; ++z) {
      for (arv::i32 x = overlap->min.value.x; x <= overlap->max.value.x; ++x) {
        const arv::BlockPos world{{x, y, z}};
        const std::optional<arv::BlockPos> local =
            arv::world::worldgen::blueprint_world_to_local(
                feature.blueprint,
                candidate.origin,
                world,
                candidate.rotation);
        if (!local.has_value()) {
          continue;
        }
        const arv::usize index = static_cast<arv::usize>(local->value.x) +
                                 static_cast<arv::usize>(feature.blueprint.size.value.x) *
                                     (static_cast<arv::usize>(local->value.z) +
                                      static_cast<arv::usize>(feature.blueprint.size.value.z) *
                                          static_cast<arv::usize>(local->value.y));
        if (index < feature.blueprint.blocks.size() && feature.blueprint.blocks[index] != 0U) {
          return world;
        }
      }
    }
  }
  return std::nullopt;
}

struct BorderTreeSample {
  arv::u64 seed{};
  arv::i32 world_z{};
  arv::i32 base_y{};
};

std::optional<BorderTreeSample> find_east_border_tree_sample(
    const arv::world::GeneratedChunk& origin_chunk,
    const arv::world::GeneratedChunk& east_chunk,
    arv::u64 seed,
    arv::voxel::BlockVariantID log) {
  for (const arv::world::GeneratedChunkSection& section : east_chunk.sections) {
    for (arv::usize index = 0; index < arv::voxel::ChunkVolume; ++index) {
      if (section.blocks[index] != log) {
        continue;
      }
      const arv::SectionLocalPos local = arv::voxel::local_pos(index);
      if (local.value.x <= 4) {
        return std::nullopt;
      }
    }
  }

  std::array<arv::i32, arv::ChunkSize> min_trunk_y_by_z{};
  min_trunk_y_by_z.fill(std::numeric_limits<arv::i32>::max());
  for (const arv::world::GeneratedChunkSection& section : origin_chunk.sections) {
    for (arv::usize index = 0; index < arv::voxel::ChunkVolume; ++index) {
      if (section.blocks[index] != log) {
        continue;
      }
      const arv::SectionLocalPos local = arv::voxel::local_pos(index);
      if (local.value.x != arv::ChunkSize - 1) {
        continue;
      }
      const arv::i32 world_y = section.coord.value.y * arv::ChunkSize + local.value.y;
      min_trunk_y_by_z[static_cast<arv::usize>(local.value.z)] =
          std::min(min_trunk_y_by_z[static_cast<arv::usize>(local.value.z)], world_y);
    }
  }

  for (arv::usize z = 0; z < min_trunk_y_by_z.size(); ++z) {
    if (min_trunk_y_by_z[z] != std::numeric_limits<arv::i32>::max()) {
      return BorderTreeSample{seed, static_cast<arv::i32>(z), min_trunk_y_by_z[z]};
    }
  }
  return std::nullopt;
}

arv::world::WorldGenerationConfig make_worldgen_config() {
  auto parsed = arv::world::parse_world_generation_yaml(R"yaml(
id: arvane:test_overworld
version: 1
seed_salt: "test"
vertical:
  min_y: -16
  max_y: 64
  sea_level: 16
noise:
  terrain:
    frequency: 0.1
density:
  base_height: 8
  height_scale: 12
surface:
  rules:
    - layers:
        - block: arvane:grass_block
          depth: 1
        - block: arvane:dirt
          depth: 2
        - block: arvane:stone
          depth: remaining
)yaml");
  EXPECT_TRUE(parsed);
  return std::move(parsed.value());
}

arv::world::ChunkGenerationRequest make_request(
    const arv::voxel::BlockRegistry& registry,
    arv::u64 seed,
    arv::ChunkSectionCoord coord = {{0, 0, 0}}) {
  arv::world::ChunkGenerationRequest request;
  request.world_id = arv::WorldID{1};
  request.seed = seed;
  request.coord = arv::chunk_to_column(coord);
  request.config = make_worldgen_config();
  request.min_chunk_y = arv::floor_div(request.config.min_y, arv::ChunkSize);
  request.max_chunk_y = arv::floor_div(request.config.max_y, arv::ChunkSize);

  auto palette = arv::world::resolve_world_generation_palette(request.config, registry);
  EXPECT_TRUE(palette);
  request.palette = std::move(palette.value());
  return request;
}

} // namespace

TEST(WorldGenerationTests, SameSeedAndChunkProduceSameOutputThroughAJob) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::ChunkGenerationRequest request = make_request(registry, 1234);

  arv::sched::JobSystemConfig job_config;
  job_config.critical_cpu_workers = 1;
  job_config.background_cpu_workers = 1;
  job_config.blocking_io_workers = 1;
  arv::sched::JobSystem jobs{job_config};
  jobs.start();
  arv::sched::JobDesc first_desc;
  first_desc.name = "generate_first";
  first_desc.lane = arv::sched::JobLane::BackgroundCpu;
  first_desc.domain = arv::sched::JobDomain::ChunkGeneration;
  auto first_job = jobs.submit_result(std::move(first_desc), [request] {
    return arv::world::generate_chunk(request);
  });
  arv::sched::JobDesc second_desc;
  second_desc.name = "generate_second";
  second_desc.lane = arv::sched::JobLane::BackgroundCpu;
  second_desc.domain = arv::sched::JobDomain::ChunkGeneration;
  auto second_job = jobs.submit_result(std::move(second_desc), [request] {
    return arv::world::generate_chunk(request);
  });

  auto first = first_job.get();
  auto second = second_job.get();

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(first.value().sections, second.value().sections);
}

TEST(WorldGenerationTests, LoadsPackWorldgenResourcesAndResolvesOverworld) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::WorldGenerationConfig config = make_pack_worldgen_config(registry);

  ASSERT_NE(config.resolved_dimension, nullptr);
  EXPECT_EQ(config.id, "arvane:worldgen/dimension/overworld");
  EXPECT_EQ(config.min_y, -64);
  EXPECT_EQ(config.max_y, 128);
  EXPECT_GE(config.resolved_dimension->biomes.size(), 11U);
  EXPECT_GE(config.resolved_dimension->surface_rule_sets.size(), 3U);
  EXPECT_FALSE(config.resolved_dimension->carvers.empty());
  EXPECT_FALSE(config.resolved_dimension->placed_features.empty());
  EXPECT_FALSE(config.resolved_dimension->structure_sets.empty());
  EXPECT_TRUE(config.resolved_dimension->terrain.hill_noise.has_value());
  EXPECT_TRUE(config.resolved_dimension->terrain.ridge_noise.has_value());
  EXPECT_TRUE(config.resolved_dimension->terrain.warp_x_noise.has_value());
  EXPECT_TRUE(config.resolved_dimension->terrain.warp_z_noise.has_value());
  EXPECT_GT(config.resolved_dimension->terrain.height.hill_scale, 0);
  EXPECT_GT(config.resolved_dimension->terrain.height.ridge_scale, 0);
  EXPECT_GT(config.resolved_dimension->terrain.shape.domain_warp_amplitude, 0.0F);
  EXPECT_GT(config.resolved_dimension->terrain.shape.max_column_step, 0);
  ASSERT_GE(config.resolved_dimension->biome_source.fields.size(), 2U);
  const auto temperature_field = std::find_if(
      config.resolved_dimension->biome_source.fields.begin(),
      config.resolved_dimension->biome_source.fields.end(),
      [](const arv::world::worldgen::ResolvedClimateField& field) {
        return field.name == "temperature";
      });
  ASSERT_NE(temperature_field, config.resolved_dimension->biome_source.fields.end());
  EXPECT_LT(temperature_field->scale, 1.0F);
  EXPECT_LT(temperature_field->offset, 0.0F);
  auto has_biome = [&](std::string_view id) {
    return std::any_of(
        config.resolved_dimension->biomes.begin(),
        config.resolved_dimension->biomes.end(),
        [&](const arv::world::worldgen::ResolvedBiome& biome) {
          return biome.raw.id == id;
        });
  };
  EXPECT_TRUE(has_biome("arvane:worldgen/biome/scrubland"));
  EXPECT_TRUE(has_biome("arvane:worldgen/biome/rolling_hills"));
  EXPECT_TRUE(has_biome("arvane:worldgen/biome/savanna"));
  EXPECT_TRUE(has_biome("arvane:worldgen/biome/badlands"));
  for (const arv::world::worldgen::ResolvedBiome& biome : config.resolved_dimension->biomes) {
    EXPECT_FALSE(biome.raw.display_name.empty()) << biome.raw.id;
    EXPECT_NE(biome.raw.display_name, biome.raw.id) << biome.raw.id;
  }
  EXPECT_TRUE(std::any_of(
      config.resolved_dimension->carvers.begin(),
      config.resolved_dimension->carvers.end(),
      [](const arv::world::worldgen::CarverConfig& carver) {
        return carver.carver_type == arv::world::worldgen::CarverType::Ravines;
      }));
  EXPECT_TRUE(std::any_of(
      config.resolved_dimension->carvers.begin(),
      config.resolved_dimension->carvers.end(),
      [](const arv::world::worldgen::CarverConfig& carver) {
        return carver.carver_type == arv::world::worldgen::CarverType::TunnelCaves;
      }));
  const arv::usize structures_stage =
      static_cast<arv::usize>(arv::world::worldgen::GenerationStage::Structures);
  EXPECT_TRUE(std::none_of(
      config.resolved_dimension->dimension_features_by_stage[structures_stage].begin(),
      config.resolved_dimension->dimension_features_by_stage[structures_stage].end(),
      [&](arv::usize feature_index) {
        return feature_index < config.resolved_dimension->placed_features.size() &&
               config.resolved_dimension
                       ->configured_features[
                           config.resolved_dimension->placed_features[feature_index].feature_index]
                       .id == "arvane:worldgen/configured_feature/small_ruin";
      }));
}

TEST(WorldGenerationTests, PackOceanBiomesGenerateWaterFluids) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::WorldGenerationConfig config = make_pack_worldgen_config(registry);
  ASSERT_NE(config.resolved_dimension, nullptr);
  const auto palette = arv::world::resolve_world_generation_palette(config, registry);
  ASSERT_TRUE(palette) << palette.error().message;

  const std::array<arv::u64, 3> seeds{
      7390150598859ULL,
      32187121430964ULL,
      91827364554637ULL,
  };
  arv::u64 found_seed = seeds.front();
  std::optional<std::pair<arv::ChunkCoord, arv::ChunkLocalColumn>> ocean_sample;
  for (const arv::u64 seed : seeds) {
    for (arv::i32 chunk_z = -160; chunk_z <= 160 && !ocean_sample.has_value(); chunk_z += 8) {
      for (arv::i32 chunk_x = -160; chunk_x <= 160 && !ocean_sample.has_value(); chunk_x += 8) {
        const arv::ChunkCoord coord{chunk_x, chunk_z};
        const arv::world::worldgen::TerrainProfile profile =
            arv::world::worldgen::generate_terrain_profile(
                *config.resolved_dimension,
                seed,
                coord);
        for (arv::usize column = 0; column < arv::voxel::ChunkArea; ++column) {
          const arv::usize biome_index = profile.biome_by_column[column];
          ASSERT_LT(biome_index, config.resolved_dimension->biomes.size());
          const bool ocean_biome = arv::world::worldgen::biome_has_tag(
              config.resolved_dimension->biomes[biome_index].raw,
              "arvane:biome/ocean");
          if (ocean_biome &&
              profile.surface_y_by_column[column] <=
                  config.resolved_dimension->raw.vertical.sea_level - 4) {
            found_seed = seed;
            ocean_sample = std::make_pair(coord, arv::voxel::local_column(column));
            break;
          }
        }
      }
    }
    if (ocean_sample.has_value()) {
      break;
    }
  }
  ASSERT_TRUE(ocean_sample.has_value());

  arv::world::ChunkGenerationRequest request;
  request.world_id = arv::WorldID{1};
  request.seed = found_seed;
  request.coord = ocean_sample->first;
  request.min_chunk_y = arv::floor_div(config.min_y, arv::ChunkSize);
  request.max_chunk_y = arv::floor_div(config.max_y, arv::ChunkSize);
  request.config = config;
  request.palette = palette.value();

  const arv::world::worldgen::TerrainProfile profile =
      arv::world::worldgen::generate_terrain_profile(
          *config.resolved_dimension,
          found_seed,
          ocean_sample->first);
  const arv::usize column_index = arv::voxel::local_column_index(ocean_sample->second);
  const arv::i32 surface_y = profile.surface_y_by_column[column_index];
  const arv::BlockPos water_pos{{
      ocean_sample->first.x * arv::ChunkSize + ocean_sample->second.x,
      surface_y + 1,
      ocean_sample->first.z * arv::ChunkSize + ocean_sample->second.z}};

  const auto generated = arv::world::generate_chunk(request);
  ASSERT_TRUE(generated) << generated.error().message;
  EXPECT_EQ(generated_block_at(generated.value(), water_pos), arv::voxel::AirBlockVariantID);
  const arv::voxel::FluidCell fluid = generated_fluid_at(generated.value(), water_pos);
  EXPECT_TRUE(fluid.source());
  EXPECT_EQ(fluid.type, arv::voxel::DefaultWaterFluidTypeID);
}

TEST(WorldGenerationTests, DimensionClimateAdjustmentsShiftBiomeSelection) {
  arv::world::worldgen::ResolvedDimension cool = make_two_biome_blend_dimension();
  arv::world::worldgen::ResolvedDimension warm = cool;
  for (arv::world::worldgen::ResolvedClimateField& field : cool.biome_source.fields) {
    if (field.name == "temperature") {
      field.scale = 0.0F;
      field.offset = -0.75F;
    }
  }
  for (arv::world::worldgen::ResolvedClimateField& field : warm.biome_source.fields) {
    if (field.name == "temperature") {
      field.scale = 0.0F;
      field.offset = 0.75F;
    }
  }

  const arv::world::worldgen::TerrainProfile cool_profile =
      arv::world::worldgen::generate_terrain_profile(cool, 10101ULL, {0, 0});
  const arv::world::worldgen::TerrainProfile warm_profile =
      arv::world::worldgen::generate_terrain_profile(warm, 10101ULL, {0, 0});

  for (arv::usize column = 0; column < arv::voxel::ChunkArea; ++column) {
    EXPECT_EQ(cool_profile.biome_by_column[column], 0U);
    EXPECT_EQ(warm_profile.biome_by_column[column], 1U);
  }
  EXPECT_NE(cool_profile.surface_y_by_column, warm_profile.surface_y_by_column);
}

TEST(WorldGenerationTests, RefreshGenerationConfigExpandsSavedDepthForCurrentDimension) {
  arv::world::WorldGenerationConfig saved;
  saved.dimension_id = "arvane:worldgen/dimension/overworld";
  saved.min_y = -32;
  saved.max_y = 96;
  saved.sea_level = 16;
  saved.base_height = 12;

  arv::world::WorldGenerationConfig current;
  current.id = "arvane:worldgen/dimension/overworld";
  current.dimension_id = "arvane:worldgen/dimension/overworld";
  current.min_y = -64;
  current.max_y = 128;
  current.sea_level = 32;
  current.base_height = 20;

  const arv::world::WorldGenerationConfig refreshed =
      arv::world::refresh_generation_config_for_current_dimension(saved, current);

  EXPECT_EQ(refreshed.dimension_id, current.dimension_id);
  EXPECT_EQ(refreshed.min_y, -64);
  EXPECT_EQ(refreshed.max_y, 128);
  EXPECT_EQ(refreshed.sea_level, 32);
  EXPECT_EQ(refreshed.base_height, 20);
}

TEST(WorldGenerationTests, PackWorldgenIsDeterministicAndBiomeBacked) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  arv::world::WorldGenerationConfig config = make_pack_worldgen_config(registry);
  auto palette = arv::world::resolve_world_generation_palette(config, registry);
  ASSERT_TRUE(palette) << palette.error().message;

  arv::world::ChunkGenerationRequest request;
  request.world_id = arv::WorldID{1};
  request.seed = 8181;
  request.coord = {2, -3};
  request.min_chunk_y = arv::floor_div(config.min_y, arv::ChunkSize);
  request.max_chunk_y = arv::floor_div(config.max_y, arv::ChunkSize);
  request.config = config;
  request.palette = std::move(palette.value());

  auto first = arv::world::generate_chunk(request);
  auto second = arv::world::generate_chunk(request);
  ASSERT_TRUE(first) << first.error().message;
  ASSERT_TRUE(second) << second.error().message;
  EXPECT_EQ(first.value().sections, second.value().sections);

  const arv::world::worldgen::TerrainProfile profile =
      arv::world::worldgen::generate_terrain_profile(
          *config.resolved_dimension,
          request.seed,
          request.coord);
  bool saw_biome = false;
  for (const arv::usize biome_index : profile.biome_by_column) {
    saw_biome = saw_biome || biome_index < config.resolved_dimension->biomes.size();
  }
  EXPECT_TRUE(saw_biome);
}

TEST(WorldGenerationTests, FeaturesOriginatingInNeighborChunksCanPopulateCurrentChunk) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto dimension = std::make_shared<arv::world::worldgen::ResolvedDimension>(
      make_flat_tree_dimension(registry));
  const arv::voxel::BlockVariantID log = registry.find_variant("arvane:oak_log").value();
  const arv::voxel::BlockVariantID leaves =
      registry.find_variant("arvane:oak_leaves").value();

  std::optional<BorderTreeSample> sample;
  arv::world::GeneratedChunk east_chunk;
  for (arv::u64 seed = 1; seed < 5000 && !sample.has_value(); ++seed) {
    auto origin = arv::world::generate_chunk(make_resolved_request(dimension, seed, {0, 0}));
    auto east = arv::world::generate_chunk(make_resolved_request(dimension, seed, {1, 0}));
    ASSERT_TRUE(origin) << origin.error().message;
    ASSERT_TRUE(east) << east.error().message;
    sample = find_east_border_tree_sample(origin.value(), east.value(), seed, log);
    if (sample.has_value()) {
      east_chunk = std::move(east.value());
    }
  }

  ASSERT_TRUE(sample.has_value());
  const arv::BlockPos expected_leaf{{
      arv::ChunkSize,
      sample->base_y + 4,
      sample->world_z}};
  EXPECT_EQ(generated_block_at(east_chunk, expected_leaf), leaves);
}

TEST(WorldGenerationTests, SimpleTreeTrunkReplacesSurfaceDecorationAtBase) {
  constexpr arv::u64 seed = 424242ULL;
  constexpr arv::u64 salt = 13579ULL;
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto dimension = std::make_shared<arv::world::worldgen::ResolvedDimension>(
      make_tree_with_surface_decoration_dimension(registry, salt));
  const arv::voxel::BlockVariantID log = registry.find_variant("arvane:oak_log").value();
  const arv::voxel::BlockVariantID short_grass =
      registry.find_variant("arvane:short_grass").value();

  arv::world::worldgen::DeterministicRng rng{arv::world::worldgen::coordinate_seed(
      seed,
      0,
      static_cast<arv::i32>(salt & 0x7FFFFFFFULL),
      0,
      salt)};
  const arv::i32 local_x = rng.range_i32(0, arv::ChunkSize - 1);
  const arv::i32 local_z = rng.range_i32(0, arv::ChunkSize - 1);
  const arv::world::worldgen::TerrainProfile profile =
      arv::world::worldgen::generate_terrain_profile(*dimension, seed, {0, 0});
  const arv::i32 surface_y =
      profile.surface_y_by_column[arv::voxel::local_column_index({local_x, local_z})];
  const arv::BlockPos trunk_base{{local_x, surface_y + 1, local_z}};

  auto generated = arv::world::generate_chunk(make_resolved_request(dimension, seed, {0, 0}));

  ASSERT_TRUE(generated) << generated.error().message;
  EXPECT_EQ(generated_block_at(generated.value(), trunk_base), log);
  EXPECT_NE(generated_block_at(generated.value(), trunk_base), short_grass);
}

TEST(WorldGenerationTests, StructureSetSameSeedAndChunkProduceSameOutput) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto dimension = std::make_shared<arv::world::worldgen::ResolvedDimension>(
      make_flat_structure_dimension(registry));
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::usize feature_index = add_blueprint_feature(
      *dimension,
      registry,
      "arvane:worldgen/configured_feature/repeatable_ruin",
      make_filled_blueprint("repeatable_ruin", {{6, 2, 6}}, {{0, 0, 0}}, "arvane:stone[]"),
      stone);
  add_structure_set(
      *dimension,
      "arvane:worldgen/structure_set/repeatable_ruins",
      feature_index,
      19001);

  const auto first =
      arv::world::generate_chunk(make_resolved_request(dimension, 7777ULL, {0, 0}));
  const auto second =
      arv::world::generate_chunk(make_resolved_request(dimension, 7777ULL, {0, 0}));
  ASSERT_TRUE(first) << first.error().message;
  ASSERT_TRUE(second) << second.error().message;
  EXPECT_EQ(first.value().sections, second.value().sections);
}

TEST(WorldGenerationTests, BorderCrossingStructureGeneratesFromEitherSide) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto dimension = std::make_shared<arv::world::worldgen::ResolvedDimension>(
      make_flat_structure_dimension(registry));
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::usize feature_index = add_blueprint_feature(
      *dimension,
      registry,
      "arvane:worldgen/configured_feature/border_ruin",
      make_filled_blueprint("border_ruin", {{8, 1, 3}}, {{0, 0, 1}}, "arvane:stone[]"),
      stone);
  add_structure_set(*dimension, "arvane:worldgen/structure_set/border_ruins", feature_index, 731);

  std::optional<arv::world::worldgen::StructureCandidate> crossing;
  arv::u64 seed = 0;
  const arv::world::worldgen::BlockAabb origin_bounds =
      arv::world::worldgen::chunk_column_bounds({0, 0}, 0, 1);
  const arv::world::worldgen::BlockAabb east_bounds =
      arv::world::worldgen::chunk_column_bounds({1, 0}, 0, 1);
  for (arv::u64 candidate_seed = 1; candidate_seed < 5000 && !crossing.has_value();
       ++candidate_seed) {
    const std::vector<arv::world::worldgen::StructureCandidate> candidates =
        arv::world::worldgen::collect_structure_candidates_for_chunk(
            *dimension,
            candidate_seed,
            {1, 0},
            0,
            1);
    for (const auto& candidate : candidates) {
      if (candidate.candidate_chunk == arv::ChunkCoord{0, 0} &&
          arv::world::worldgen::intersects(candidate.bounds, east_bounds)) {
        crossing = candidate;
        seed = candidate_seed;
        break;
      }
    }
  }
  ASSERT_TRUE(crossing.has_value());

  const arv::world::worldgen::ResolvedConfiguredFeature& feature =
      dimension->configured_features[crossing->feature_index];
  const std::optional<arv::BlockPos> east_pos =
      first_solid_blueprint_block_in_bounds(feature, crossing.value(), east_bounds);
  const std::optional<arv::BlockPos> origin_pos =
      first_solid_blueprint_block_in_bounds(feature, crossing.value(), origin_bounds);
  ASSERT_TRUE(east_pos.has_value());
  ASSERT_TRUE(origin_pos.has_value());

  const auto east_only =
      arv::world::generate_chunk(make_resolved_request(dimension, seed, {1, 0}));
  const auto origin_after =
      arv::world::generate_chunk(make_resolved_request(dimension, seed, {0, 0}));
  ASSERT_TRUE(east_only) << east_only.error().message;
  ASSERT_TRUE(origin_after) << origin_after.error().message;
  EXPECT_EQ(generated_block_at(east_only.value(), east_pos.value()), stone);
  EXPECT_EQ(generated_block_at(origin_after.value(), origin_pos.value()), stone);
}

TEST(WorldGenerationTests, LargeStructureSetBlueprintCanCrossMultipleChunkColumns) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto dimension = std::make_shared<arv::world::worldgen::ResolvedDimension>(
      make_flat_structure_dimension(registry));
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::usize feature_index = add_blueprint_feature(
      *dimension,
      registry,
      "arvane:worldgen/configured_feature/large_border_ruin",
      make_filled_blueprint("large_border_ruin", {{48, 1, 3}}, {{0, 0, 1}}, "arvane:stone[]"),
      stone);
  add_structure_set(
      *dimension,
      "arvane:worldgen/structure_set/large_border_ruins",
      feature_index,
      1731);

  constexpr arv::u64 seed = 29ULL;
  const arv::ChunkCoord far_chunk{2, 0};
  const std::vector<arv::world::worldgen::StructureCandidate> candidates =
      arv::world::worldgen::collect_structure_candidates_for_chunk(
          *dimension,
          seed,
          far_chunk,
          0,
          1);
  const auto crossing = std::find_if(
      candidates.begin(),
      candidates.end(),
      [](const arv::world::worldgen::StructureCandidate& candidate) {
        return candidate.candidate_chunk == arv::ChunkCoord{0, 0};
      });
  ASSERT_NE(crossing, candidates.end());

  const arv::world::worldgen::ResolvedConfiguredFeature& feature =
      dimension->configured_features[crossing->feature_index];
  const arv::world::worldgen::BlockAabb far_bounds =
      arv::world::worldgen::chunk_column_bounds(far_chunk, 0, 1);
  const std::optional<arv::BlockPos> far_pos =
      first_solid_blueprint_block_in_bounds(feature, *crossing, far_bounds);
  ASSERT_TRUE(far_pos.has_value());

  const auto generated =
      arv::world::generate_chunk(make_resolved_request(dimension, seed, far_chunk));
  ASSERT_TRUE(generated) << generated.error().message;
  EXPECT_EQ(generated_block_at(generated.value(), far_pos.value()), stone);
}

TEST(WorldGenerationTests, ChunkGenerationOrderDoesNotChangeBorderStructureBlocks) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto dimension = std::make_shared<arv::world::worldgen::ResolvedDimension>(
      make_flat_structure_dimension(registry));
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::usize feature_index = add_blueprint_feature(
      *dimension,
      registry,
      "arvane:worldgen/configured_feature/order_ruin",
      make_filled_blueprint("order_ruin", {{8, 1, 3}}, {{0, 0, 1}}, "arvane:stone[]"),
      stone);
  add_structure_set(*dimension, "arvane:worldgen/structure_set/order_ruins", feature_index, 991);

  const arv::u64 seed = 341;
  const auto origin_first =
      arv::world::generate_chunk(make_resolved_request(dimension, seed, {0, 0}));
  const auto east_second =
      arv::world::generate_chunk(make_resolved_request(dimension, seed, {1, 0}));
  const auto east_first =
      arv::world::generate_chunk(make_resolved_request(dimension, seed, {1, 0}));
  const auto origin_second =
      arv::world::generate_chunk(make_resolved_request(dimension, seed, {0, 0}));
  ASSERT_TRUE(origin_first) << origin_first.error().message;
  ASSERT_TRUE(east_second) << east_second.error().message;
  ASSERT_TRUE(east_first) << east_first.error().message;
  ASSERT_TRUE(origin_second) << origin_second.error().message;
  EXPECT_EQ(origin_first.value().sections, origin_second.value().sections);
  EXPECT_EQ(east_first.value().sections, east_second.value().sections);
}

TEST(WorldGenerationTests, BlueprintIntegrityIsPositionHashedAcrossChunkIntersections) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto dimension = std::make_shared<arv::world::worldgen::ResolvedDimension>(
      make_flat_structure_dimension(registry));
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::f32 integrity = 0.5F;
  const arv::usize feature_index = add_blueprint_feature(
      *dimension,
      registry,
      "arvane:worldgen/configured_feature/damaged_border_ruin",
      make_filled_blueprint("damaged_border_ruin", {{8, 1, 1}}, {{0, 0, 0}}, "arvane:stone[]"),
      stone,
      integrity);
  const arv::usize set_index = add_structure_set(
      *dimension,
      "arvane:worldgen/structure_set/damaged_border_ruins",
      feature_index,
      8891);

  std::optional<arv::world::worldgen::StructureCandidate> crossing;
  arv::u64 seed = 0;
  const arv::world::worldgen::BlockAabb east_bounds =
      arv::world::worldgen::chunk_column_bounds({1, 0}, 0, 1);
  for (arv::u64 candidate_seed = 1; candidate_seed < 5000 && !crossing.has_value();
       ++candidate_seed) {
    const auto candidates = arv::world::worldgen::collect_structure_candidates_for_chunk(
        *dimension,
        candidate_seed,
        {1, 0},
        0,
        1);
    for (const auto& candidate : candidates) {
      if (candidate.candidate_chunk == arv::ChunkCoord{0, 0} &&
          arv::world::worldgen::intersects(candidate.bounds, east_bounds)) {
        crossing = candidate;
        seed = candidate_seed;
        break;
      }
    }
  }
  ASSERT_TRUE(crossing.has_value());

  const auto origin_chunk =
      arv::world::generate_chunk(make_resolved_request(dimension, seed, {0, 0}));
  const auto east_chunk =
      arv::world::generate_chunk(make_resolved_request(dimension, seed, {1, 0}));
  ASSERT_TRUE(origin_chunk) << origin_chunk.error().message;
  ASSERT_TRUE(east_chunk) << east_chunk.error().message;

  const auto& feature = dimension->configured_features[feature_index];
  const auto& set = dimension->structure_sets[set_index];
  const auto* config =
      std::get_if<arv::world::worldgen::BlueprintFeatureConfig>(&feature.raw.config);
  ASSERT_NE(config, nullptr);
  const arv::u64 resource_hash =
      arv::world::worldgen::mix64(arv::world::worldgen::fnv1a(feature.id)) ^
      arv::world::worldgen::mix64(arv::world::worldgen::fnv1a(config->blueprint.id));

  bool checked_origin = false;
  bool checked_neighbor = false;
  for (arv::i32 local_x = 0; local_x < feature.blueprint.size.value.x; ++local_x) {
    const arv::BlockPos local{{local_x, 0, 0}};
    const arv::BlockPos world = arv::world::worldgen::blueprint_local_to_world(
        feature.blueprint,
        crossing->origin,
        local,
        crossing->rotation);
    const bool expected_present =
        arv::world::worldgen::hash_to_unit_float(arv::world::worldgen::structure_block_seed(
            seed,
            set.raw.salt,
            crossing->origin,
            local,
            resource_hash)) <= integrity;
    const arv::world::GeneratedChunk& chunk =
        arv::floor_div(world.value.x, arv::ChunkSize) == 0
            ? origin_chunk.value()
            : east_chunk.value();
    EXPECT_EQ(generated_block_at(chunk, world) == stone, expected_present)
        << "world_x=" << world.value.x << " local_x=" << local_x;
    checked_origin =
        checked_origin || arv::floor_div(world.value.x, arv::ChunkSize) == 0;
    checked_neighbor =
        checked_neighbor || arv::floor_div(world.value.x, arv::ChunkSize) == 1;
  }
  EXPECT_TRUE(checked_origin);
  EXPECT_TRUE(checked_neighbor);
}

TEST(WorldGenerationTests, BlueprintPlacementClearsFluidUnderSolidBlocks) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  arv::world::worldgen::ResolvedDimension dimension = make_flat_structure_dimension(registry);
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::usize feature_index = add_blueprint_feature(
      dimension,
      registry,
      "arvane:worldgen/configured_feature/fluid_clearing_ruin",
      make_filled_blueprint("fluid_clearing_ruin", {{1, 1, 1}}, {{0, 0, 0}}, "arvane:stone[]"),
      stone);

  arv::world::GeneratedChunkSection section;
  section.coord = {{0, 0, 0}};
  section.blocks.fill(arv::voxel::AirBlockVariantID);
  section.fluids.fill(arv::voxel::make_fluid_source(arv::voxel::make_water_fluid_definition()));
  std::vector<arv::world::GeneratedChunkSection> sections;
  sections.push_back(std::move(section));

  arv::world::worldgen::GeneratedChunkEditView view{{0, 0}, 0, 0, &sections};
  arv::world::worldgen::BlueprintPlacementInput input;
  input.feature = &dimension.configured_features[feature_index];
  input.origin = {{4, 4, 4}};
  input.bounds = arv::world::worldgen::blueprint_world_bounds(
      dimension.configured_features[feature_index].blueprint,
      input.origin,
      input.rotation);
  input.world_seed = 1;
  input.salt = 2;

  arv::world::worldgen::place_blueprint_intersection(input, view);

  const arv::usize index = arv::voxel::local_index({{4, 4, 4}});
  EXPECT_EQ(sections.front().blocks[index], stone);
  EXPECT_TRUE(sections.front().fluids[index].empty());
}

TEST(WorldGenerationTests, StructureCandidateSearchIsBoundedByMaxExtent) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto dimension = std::make_shared<arv::world::worldgen::ResolvedDimension>(
      make_flat_structure_dimension(registry));
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::usize feature_index = add_blueprint_feature(
      *dimension,
      registry,
      "arvane:worldgen/configured_feature/sparse_ruin",
      make_filled_blueprint("sparse_ruin", {{3, 1, 3}}, {{1, 0, 1}}, "arvane:stone[]"),
      stone);
  add_structure_set(
      *dimension,
      "arvane:worldgen/structure_set/sparse_ruins",
      feature_index,
      918273,
      32,
      8);

  arv::world::worldgen::StructureCandidateSearchStats stats;
  (void)arv::world::worldgen::collect_structure_candidates_for_chunk(
      *dimension,
      12345ULL,
      {0, 0},
      0,
      1,
      &stats);
  const arv::i32 radius =
      arv::world::worldgen::structure_set_search_radius_chunks(dimension->structure_sets.front());
  const arv::i32 average =
      dimension->structure_sets.front().raw.placement.average_distance_chunks;
  const arv::i32 min_cell = arv::floor_div(-radius, average);
  const arv::i32 max_cell = arv::floor_div(radius, average);
  const arv::u32 expected_cells =
      static_cast<arv::u32>((max_cell - min_cell + 1) * (max_cell - min_cell + 1));
  EXPECT_EQ(stats.inspected_cells, expected_cells);
  EXPECT_LE(stats.accepted_candidates, stats.inspected_cells);
}

TEST(WorldGenerationTests, StructureReplacementRulesAreLocalToCurrentChunk) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto dimension = std::make_shared<arv::world::worldgen::ResolvedDimension>(
      make_flat_structure_dimension(registry));
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID grass = registry.find_variant("arvane:grass_block").value();
  arv::world::worldgen::ResolvedReplaceRule replace;
  replace.specified = true;
  replace.only_air = true;
  const arv::usize feature_index = add_blueprint_feature(
      *dimension,
      registry,
      "arvane:worldgen/configured_feature/air_only_marker",
      make_filled_blueprint("air_only_marker", {{1, 2, 1}}, {{0, 1, 0}}, "arvane:stone[]"),
      stone,
      1.0F,
      replace);
  add_structure_set(
      *dimension,
      "arvane:worldgen/structure_set/air_only_markers",
      feature_index,
      3127);

  const auto candidates = arv::world::worldgen::collect_structure_candidates_for_chunk(
      *dimension,
      444ULL,
      {0, 0},
      0,
      1);
  const auto candidate_iter = std::find_if(
      candidates.begin(),
      candidates.end(),
      [](const arv::world::worldgen::StructureCandidate& candidate) {
        return candidate.candidate_chunk == arv::ChunkCoord{0, 0};
      });
  ASSERT_NE(candidate_iter, candidates.end());
  const auto generated =
      arv::world::generate_chunk(make_resolved_request(dimension, 444ULL, {0, 0}));
  ASSERT_TRUE(generated) << generated.error().message;
  const arv::BlockPos surface{{
      candidate_iter->origin.value.x,
      candidate_iter->origin.value.y - 1,
      candidate_iter->origin.value.z}};
  EXPECT_EQ(generated_block_at(generated.value(), surface), grass);
  EXPECT_EQ(generated_block_at(generated.value(), candidate_iter->origin), stone);
}

TEST(WorldGenerationTests, RavineCarverIsDeterministicAndChunkLocal) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto dimension = std::make_shared<arv::world::worldgen::ResolvedDimension>(
      make_flat_structure_dimension(registry));

  arv::world::worldgen::CarverConfig ravine;
  ravine.id = "arvane:worldgen/carver/test_ravines";
  ravine.version = 1;
  ravine.type = "arvane:carver/ravines";
  ravine.carver_type = arv::world::worldgen::CarverType::Ravines;
  ravine.y_range = {0, 15};
  ravine.surface_margin = 0;
  ravine.floor_margin = 4;
  ravine.ravine.salt = 45001;
  ravine.ravine.region_size_chunks = 1;
  ravine.ravine.chance_per_region = 1.0F;
  ravine.ravine.length_chunks = {2, 2};
  ravine.ravine.width = {3.0F, 3.0F};
  ravine.ravine.depth = {16, 16};
  dimension->carvers.push_back(ravine);

  const auto first = arv::world::generate_chunk(make_resolved_request(dimension, 5151ULL, {0, 0}));
  const auto second = arv::world::generate_chunk(make_resolved_request(dimension, 5151ULL, {0, 0}));
  ASSERT_TRUE(first) << first.error().message;
  ASSERT_TRUE(second) << second.error().message;
  EXPECT_EQ(first.value().sections, second.value().sections);

  arv::usize carved_air_below_surface = 0;
  arv::usize carved_air_below_floor_margin = 0;
  for (const arv::world::GeneratedChunkSection& section : first.value().sections) {
    for (arv::usize index = 0; index < arv::voxel::ChunkVolume; ++index) {
      const arv::SectionLocalPos local = arv::voxel::local_pos(index);
      const arv::i32 world_y = section.coord.value.y * arv::ChunkSize + local.value.y;
      if (world_y <= 8 && section.blocks[index] == arv::voxel::AirBlockVariantID) {
        ++carved_air_below_surface;
      }
      if (world_y < 4 && section.blocks[index] == arv::voxel::AirBlockVariantID) {
        ++carved_air_below_floor_margin;
      }
    }
  }
  EXPECT_GT(carved_air_below_surface, 0U);
  EXPECT_EQ(carved_air_below_floor_margin, 0U);
}

TEST(WorldGenerationTests, TunnelCaveCarverCreatesDeterministicUndergroundNetwork) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto dimension = std::make_shared<arv::world::worldgen::ResolvedDimension>(
      make_flat_structure_dimension(registry));
  dimension->raw.vertical.min_y = -32;
  dimension->raw.vertical.max_y = 31;
  dimension->terrain.height.base = 18;

  arv::world::worldgen::CarverConfig tunnels;
  tunnels.id = "arvane:worldgen/carver/test_tunnel_caves";
  tunnels.version = 1;
  tunnels.type = "arvane:carver/tunnel_caves";
  tunnels.carver_type = arv::world::worldgen::CarverType::TunnelCaves;
  tunnels.y_range = {-28, 20};
  tunnels.surface_margin = 6;
  tunnels.floor_margin = 6;
  tunnels.tunnel.salt = 62017;
  tunnels.tunnel.region_size_chunks = 1;
  tunnels.tunnel.chance_per_region = 1.0F;
  tunnels.tunnel.tunnels_per_region = {2, 2};
  tunnels.tunnel.length_blocks = {56, 56};
  tunnels.tunnel.radius = {2.4F, 2.4F};
  tunnels.tunnel.vertical_radius_multiplier = {0.75F, 0.75F};
  tunnels.tunnel.start_y = {0, 12};
  tunnels.tunnel.horizontal_curl = 0.2F;
  tunnels.tunnel.vertical_curl = 0.12F;
  tunnels.tunnel.branch_chance = 1.0F;
  tunnels.tunnel.branch_count = {1, 1};
  tunnels.tunnel.room_chance = 1.0F;
  tunnels.tunnel.room_radius = {4.0F, 4.0F};
  dimension->carvers.push_back(tunnels);

  const auto first = arv::world::generate_chunk(make_resolved_request(dimension, 616161ULL, {0, 0}));
  const auto second =
      arv::world::generate_chunk(make_resolved_request(dimension, 616161ULL, {0, 0}));
  ASSERT_TRUE(first) << first.error().message;
  ASSERT_TRUE(second) << second.error().message;
  EXPECT_EQ(first.value().sections, second.value().sections);

  arv::usize underground_air = 0;
  arv::usize surface_margin_breaks = 0;
  arv::usize floor_margin_breaks = 0;
  std::unordered_set<arv::i32> carved_y_levels;
  for (const arv::world::GeneratedChunkSection& section : first.value().sections) {
    for (arv::usize index = 0; index < arv::voxel::ChunkVolume; ++index) {
      const arv::SectionLocalPos local = arv::voxel::local_pos(index);
      const arv::i32 world_y = section.coord.value.y * arv::ChunkSize + local.value.y;
      if (section.blocks[index] != arv::voxel::AirBlockVariantID) {
        continue;
      }
      if (world_y <= 12 && world_y >= -26) {
        ++underground_air;
        carved_y_levels.insert(world_y);
      }
      if (world_y > 12 && world_y <= 18) {
        ++surface_margin_breaks;
      }
      if (world_y < -26) {
        ++floor_margin_breaks;
      }
    }
  }

  EXPECT_GT(underground_air, 0U);
  EXPECT_GT(carved_y_levels.size(), 3U);
  EXPECT_EQ(surface_margin_breaks, 0U);
  EXPECT_EQ(floor_margin_breaks, 0U);
}

TEST(WorldGenerationTests, StructurePlacementFiltersUseDeterministicTerrainData) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto dimension = std::make_shared<arv::world::worldgen::ResolvedDimension>(
      make_flat_structure_dimension(registry));
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID grass = registry.find_variant("arvane:grass_block").value();
  const arv::usize feature_index = add_blueprint_feature(
      *dimension,
      registry,
      "arvane:worldgen/configured_feature/filtered_ruin",
      make_filled_blueprint("filtered_ruin", {{3, 1, 3}}, {{1, 0, 1}}, "arvane:stone[]"),
      stone);
  arv::world::worldgen::ResolvedPlacementFilter filter;
  filter.raw.type = arv::world::worldgen::PlacementFilterConfig::Type::BlockBelow;
  filter.blocks.push_back(grass);
  add_structure_set(
      *dimension,
      "arvane:worldgen/structure_set/filtered_ruins",
      feature_index,
      847,
      1,
      0,
      {filter});

  arv::world::worldgen::StructureCandidateSearchStats stats;
  const auto candidates = arv::world::worldgen::collect_structure_candidates_for_chunk(
      *dimension,
      9191ULL,
      {0, 0},
      0,
      1,
      &stats);
  EXPECT_GT(stats.inspected_cells, 0U);
  ASSERT_FALSE(candidates.empty());
  const auto generated =
      arv::world::generate_chunk(make_resolved_request(dimension, 9191ULL, {0, 0}));
  ASSERT_TRUE(generated) << generated.error().message;
  const auto pos = first_solid_blueprint_block_in_bounds(
      dimension->configured_features[candidates.front().feature_index],
      candidates.front(),
      arv::world::worldgen::chunk_column_bounds({0, 0}, 0, 1));
  ASSERT_TRUE(pos.has_value());
  EXPECT_EQ(generated_block_at(generated.value(), pos.value()), stone);
}

TEST(WorldGenerationTests, OverlappingStructureCandidatesResolveInSortedOrder) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto dimension = std::make_shared<arv::world::worldgen::ResolvedDimension>(
      make_flat_structure_dimension(registry));
  const arv::voxel::BlockVariantID dirt = registry.find_variant("arvane:dirt").value();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  const arv::usize dirt_feature = add_blueprint_feature(
      *dimension,
      registry,
      "arvane:worldgen/configured_feature/a_dirt_square",
      make_filled_blueprint("a_dirt_square", {{16, 1, 16}}, {{0, 0, 0}}, "arvane:dirt[]"),
      dirt);
  const arv::usize stone_feature = add_blueprint_feature(
      *dimension,
      registry,
      "arvane:worldgen/configured_feature/b_stone_square",
      make_filled_blueprint("b_stone_square", {{16, 1, 16}}, {{0, 0, 0}}, "arvane:stone[]"),
      stone);
  add_structure_set(*dimension, "arvane:worldgen/structure_set/a_dirt_squares", dirt_feature, 100);
  add_structure_set(*dimension, "arvane:worldgen/structure_set/b_stone_squares", stone_feature, 101);

  const arv::u64 seed = 2026ULL;
  const auto candidates = arv::world::worldgen::collect_structure_candidates_for_chunk(
      *dimension,
      seed,
      {0, 0},
      0,
      1);
  ASSERT_GE(candidates.size(), 2U);
  const arv::world::worldgen::BlockAabb bounds =
      arv::world::worldgen::chunk_column_bounds({0, 0}, 0, 1);

  std::optional<arv::BlockPos> overlap_pos;
  arv::voxel::BlockVariantID expected = arv::voxel::AirBlockVariantID;
  for (arv::i32 z = bounds.min.value.z; z <= bounds.max.value.z && !overlap_pos.has_value(); ++z) {
    for (arv::i32 x = bounds.min.value.x; x <= bounds.max.value.x && !overlap_pos.has_value(); ++x) {
      const arv::BlockPos pos{{x, 9, z}};
      arv::u32 contributors = 0;
      arv::voxel::BlockVariantID final_block = arv::voxel::AirBlockVariantID;
      for (const auto& candidate : candidates) {
        const auto& feature = dimension->configured_features[candidate.feature_index];
        const auto local = arv::world::worldgen::blueprint_world_to_local(
            feature.blueprint,
            candidate.origin,
            pos,
            candidate.rotation);
        if (!local.has_value()) {
          continue;
        }
        ++contributors;
        final_block = feature.blueprint_palette[1];
      }
      if (contributors >= 2U) {
        overlap_pos = pos;
        expected = final_block;
      }
    }
  }
  ASSERT_TRUE(overlap_pos.has_value());

  const auto generated =
      arv::world::generate_chunk(make_resolved_request(dimension, seed, {0, 0}));
  ASSERT_TRUE(generated) << generated.error().message;
  EXPECT_EQ(generated_block_at(generated.value(), overlap_pos.value()), expected);
}

TEST(WorldGenerationTests, PackOverworldScanFindsMultipleBiomeFamilies) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::WorldGenerationConfig config = make_pack_worldgen_config(registry);
  ASSERT_NE(config.resolved_dimension, nullptr);

  const std::array<arv::u64, 3> seeds{
      7390150598859ULL,
      32187121430964ULL,
      91827364554637ULL,
  };
  std::unordered_set<std::string> seen;
  bool saw_ocean = false;
  bool saw_coast = false;
  bool saw_grassy = false;
  bool saw_forest = false;
  bool saw_dry = false;
  bool saw_rough = false;
  for (const arv::u64 seed : seeds) {
    for (arv::i32 z = -160; z <= 160; z += 8) {
      for (arv::i32 x = -160; x <= 160; x += 8) {
        const arv::world::worldgen::TerrainProfile profile =
            arv::world::worldgen::generate_terrain_profile(
                *config.resolved_dimension,
                seed,
                {x, z});
        for (const arv::usize biome_index : profile.biome_by_column) {
          ASSERT_LT(biome_index, config.resolved_dimension->biomes.size());
          const arv::world::worldgen::BiomeConfig& biome =
              config.resolved_dimension->biomes[biome_index].raw;
          seen.insert(biome.id);
          saw_ocean = saw_ocean ||
                      arv::world::worldgen::biome_has_tag(biome, "arvane:biome/ocean");
          saw_coast = saw_coast ||
                      arv::world::worldgen::biome_has_tag(biome, "arvane:biome/coast");
          saw_grassy = saw_grassy ||
                       arv::world::worldgen::biome_has_tag(biome, "arvane:biome/grassy");
          saw_forest = saw_forest ||
                       arv::world::worldgen::biome_has_tag(biome, "arvane:biome/forested");
          saw_dry = saw_dry ||
                    arv::world::worldgen::biome_has_tag(biome, "arvane:biome/dry");
          saw_rough = saw_rough ||
                      arv::world::worldgen::biome_has_tag(biome, "arvane:biome/mountain") ||
                      arv::world::worldgen::biome_has_tag(biome, "arvane:biome/highlands") ||
                      arv::world::worldgen::biome_has_tag(biome, "arvane:biome/cliff");
        }
      }
    }
  }

  std::string seen_text;
  for (const std::string& id : seen) {
    if (!seen_text.empty()) {
      seen_text += ", ";
    }
    seen_text += id;
  }
  EXPECT_GE(seen.size(), 8U) << "seen=" << seen_text;
  EXPECT_TRUE(saw_ocean) << "seen=" << seen_text;
  EXPECT_TRUE(saw_coast) << "seen=" << seen_text;
  EXPECT_TRUE(saw_grassy) << "seen=" << seen_text;
  EXPECT_TRUE(saw_forest) << "seen=" << seen_text;
  EXPECT_TRUE(saw_dry) << "seen=" << seen_text;
  EXPECT_TRUE(saw_rough) << "seen=" << seen_text;
}

TEST(WorldGenerationTests, PackBiomeSourceDoesNotLeaveLongDryBiomeRunsUnbroken) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::WorldGenerationConfig config = make_pack_worldgen_config(registry);
  ASSERT_NE(config.resolved_dimension, nullptr);

  const std::array<arv::u64, 3> seeds{
      7390150598859ULL,
      32187121430964ULL,
      91827364554637ULL,
  };
  std::vector<arv::ChunkCoord> dry_origins;
  std::vector<arv::u64> dry_origin_seeds;
  auto is_dry = [&](arv::usize biome_index) {
    const arv::world::worldgen::BiomeConfig& biome =
        config.resolved_dimension->biomes[biome_index].raw;
    return arv::world::worldgen::biome_has_tag(biome, "arvane:biome/dry");
  };

  for (const arv::u64 seed : seeds) {
    for (arv::i32 z = -128; z <= 128 && dry_origins.size() < 6U; z += 8) {
      for (arv::i32 x = -128; x <= 128 && dry_origins.size() < 6U; x += 8) {
        const arv::world::worldgen::TerrainProfile profile =
            arv::world::worldgen::generate_terrain_profile(
                *config.resolved_dimension,
                seed,
                {x, z});
        if (std::any_of(
                profile.biome_by_column.begin(),
                profile.biome_by_column.end(),
                is_dry)) {
          dry_origins.push_back({x, z});
          dry_origin_seeds.push_back(seed);
        }
      }
    }
  }
  ASSERT_FALSE(dry_origins.empty());

  for (arv::usize origin_index = 0; origin_index < dry_origins.size(); ++origin_index) {
    const arv::ChunkCoord origin = dry_origins[origin_index];
    const arv::u64 seed = dry_origin_seeds[origin_index];
    bool found_non_dry = false;
    for (arv::i32 dz = -24; dz <= 24 && !found_non_dry; dz += 4) {
      for (arv::i32 dx = -24; dx <= 24 && !found_non_dry; dx += 4) {
        const arv::world::worldgen::TerrainProfile profile =
            arv::world::worldgen::generate_terrain_profile(
                *config.resolved_dimension,
                seed,
                {origin.x + dx, origin.z + dz});
        found_non_dry = std::any_of(
            profile.biome_by_column.begin(),
            profile.biome_by_column.end(),
            [&](arv::usize biome_index) { return !is_dry(biome_index); });
      }
    }
    EXPECT_TRUE(found_non_dry) << "origin=" << origin.x << "," << origin.z;
  }
}

TEST(WorldGenerationTests, PackBiomeSourceBreaksUpKnownSinglePlayerDesertRegion) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::WorldGenerationConfig config = make_pack_worldgen_config(registry);
  ASSERT_NE(config.resolved_dimension, nullptr);

  constexpr arv::u64 seed = 1737496690682ULL;
  constexpr arv::ChunkCoord observed_desert_chunk{-48, 163};
  std::unordered_set<std::string> seen;
  bool saw_dry_transition = false;
  bool saw_non_dry_land = false;
  bool saw_elevated_land = false;
  for (arv::i32 dz = -32; dz <= 32; dz += 4) {
    for (arv::i32 dx = -32; dx <= 32; dx += 4) {
      const arv::world::worldgen::TerrainProfile profile =
          arv::world::worldgen::generate_terrain_profile(
              *config.resolved_dimension,
              seed,
              {observed_desert_chunk.x + dx, observed_desert_chunk.z + dz});
      for (const arv::usize biome_index : profile.biome_by_column) {
        ASSERT_LT(biome_index, config.resolved_dimension->biomes.size());
        const arv::world::worldgen::BiomeConfig& biome =
            config.resolved_dimension->biomes[biome_index].raw;
        seen.insert(biome.id);
        const bool dry = arv::world::worldgen::biome_has_tag(biome, "arvane:biome/dry");
        const bool land = arv::world::worldgen::biome_has_tag(biome, "arvane:biome/land");
        saw_dry_transition =
            saw_dry_transition ||
            biome.id == "arvane:worldgen/biome/scrubland" ||
            biome.id == "arvane:worldgen/biome/savanna";
        saw_non_dry_land = saw_non_dry_land || (land && !dry);
        saw_elevated_land =
            saw_elevated_land ||
            arv::world::worldgen::biome_has_tag(biome, "arvane:biome/mountain") ||
            arv::world::worldgen::biome_has_tag(biome, "arvane:biome/highlands") ||
            arv::world::worldgen::biome_has_tag(biome, "arvane:biome/hills");
      }
    }
  }

  std::string seen_text;
  for (const std::string& id : seen) {
    if (!seen_text.empty()) {
      seen_text += ", ";
    }
    seen_text += id;
  }
  EXPECT_GE(seen.size(), 3U) << "seen=" << seen_text;
  EXPECT_TRUE(saw_dry_transition) << "seen=" << seen_text;
  EXPECT_TRUE(saw_non_dry_land) << "seen=" << seen_text;
  EXPECT_TRUE(saw_elevated_land) << "seen=" << seen_text;
}

TEST(WorldGenerationTests, PackTerrainCapsKnownDesertCliffArtifactSteps) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::WorldGenerationConfig config = make_pack_worldgen_config(registry);
  ASSERT_NE(config.resolved_dimension, nullptr);
  const arv::i32 max_step = config.resolved_dimension->terrain.shape.max_column_step;
  ASSERT_GT(max_step, 0);

  constexpr arv::u64 seed = 1737496690682ULL;
  constexpr arv::ChunkCoord observed_desert_chunk{-48, 163};
  std::unordered_map<arv::i64, arv::world::worldgen::TerrainProfile> profiles;
  auto profile_key = [](arv::ChunkCoord coord) {
    return static_cast<arv::i64>(
        (static_cast<arv::u64>(static_cast<arv::u32>(coord.x)) << 32) |
        static_cast<arv::u32>(coord.z));
  };
  auto get_profile = [&](arv::ChunkCoord coord) -> const arv::world::worldgen::TerrainProfile& {
    const arv::i64 key = profile_key(coord);
    auto [iter, inserted] = profiles.emplace(key, arv::world::worldgen::TerrainProfile{});
    if (inserted) {
      iter->second = arv::world::worldgen::generate_terrain_profile(
          *config.resolved_dimension,
          seed,
          coord);
    }
    return iter->second;
  };

  for (arv::i32 dz = -2; dz <= 2; ++dz) {
    for (arv::i32 dx = -2; dx <= 2; ++dx) {
      const arv::ChunkCoord coord{observed_desert_chunk.x + dx, observed_desert_chunk.z + dz};
      const arv::world::worldgen::TerrainProfile& profile = get_profile(coord);
      for (arv::i32 z = 0; z < arv::ChunkSize; ++z) {
        for (arv::i32 x = 0; x < arv::ChunkSize; ++x) {
          const arv::usize column = arv::voxel::local_column_index({x, z});
          if (x + 1 < arv::ChunkSize) {
            const arv::usize east = arv::voxel::local_column_index({x + 1, z});
            EXPECT_LE(
                std::abs(profile.surface_y_by_column[column] -
                         profile.surface_y_by_column[east]),
                max_step)
                << "coord=" << coord.x << "," << coord.z << " local=" << x << "," << z;
          } else {
            const arv::world::worldgen::TerrainProfile& east_profile =
                get_profile({coord.x + 1, coord.z});
            const arv::usize east = arv::voxel::local_column_index({0, z});
            EXPECT_LE(
                std::abs(profile.surface_y_by_column[column] -
                         east_profile.surface_y_by_column[east]),
                max_step)
                << "east border coord=" << coord.x << "," << coord.z << " z=" << z;
          }
          if (z + 1 < arv::ChunkSize) {
            const arv::usize south = arv::voxel::local_column_index({x, z + 1});
            EXPECT_LE(
                std::abs(profile.surface_y_by_column[column] -
                         profile.surface_y_by_column[south]),
                max_step)
                << "coord=" << coord.x << "," << coord.z << " local=" << x << "," << z;
          } else {
            const arv::world::worldgen::TerrainProfile& south_profile =
                get_profile({coord.x, coord.z + 1});
            const arv::usize south = arv::voxel::local_column_index({x, 0});
            EXPECT_LE(
                std::abs(profile.surface_y_by_column[column] -
                         south_profile.surface_y_by_column[south]),
                max_step)
                << "south border coord=" << coord.x << "," << coord.z << " x=" << x;
          }
        }
      }
    }
  }
}

TEST(WorldGenerationTests, PackCarversProduceSurfaceRavinesAndDeepCaves) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::WorldGenerationConfig config = make_pack_worldgen_config(registry);
  ASSERT_NE(config.resolved_dimension, nullptr);
  const auto palette = arv::world::resolve_world_generation_palette(config, registry);
  ASSERT_TRUE(palette) << palette.error().message;

  constexpr arv::u64 seed = 1737496690682ULL;
  const std::array<arv::ChunkCoord, 3> search_centers{
      arv::ChunkCoord{-48, 163},
      arv::ChunkCoord{0, 0},
      arv::ChunkCoord{32, -32},
  };
  bool found_surface_ravine = false;
  bool found_deep_cave_network = false;
  std::unordered_set<arv::i32> underground_air_y_levels;
  arv::usize underground_air_blocks = 0;

  for (const arv::ChunkCoord center : search_centers) {
    for (arv::i32 dz = -8; dz <= 8 && !(found_surface_ravine && found_deep_cave_network); ++dz) {
      for (arv::i32 dx = -8; dx <= 8 && !(found_surface_ravine && found_deep_cave_network); ++dx) {
        const arv::ChunkCoord coord{center.x + dx, center.z + dz};
        arv::world::ChunkGenerationRequest request;
        request.world_id = arv::WorldID{1};
        request.seed = seed;
        request.coord = coord;
        request.min_chunk_y = arv::floor_div(config.min_y, arv::ChunkSize);
        request.max_chunk_y = arv::floor_div(config.max_y, arv::ChunkSize);
        request.config = config;
        request.palette = palette.value();

        const arv::world::worldgen::TerrainProfile profile =
            arv::world::worldgen::generate_terrain_profile(
                *config.resolved_dimension,
                seed,
                coord);
        const auto generated = arv::world::generate_chunk(request);
        ASSERT_TRUE(generated) << generated.error().message;

        for (arv::usize column = 0;
             column < arv::voxel::ChunkArea && !found_surface_ravine;
             ++column) {
          const arv::i32 surface_y = profile.surface_y_by_column[column];
          if (surface_y <= config.resolved_dimension->raw.vertical.sea_level + 1) {
            continue;
          }
          const arv::ChunkLocalColumn local = arv::voxel::local_column(column);
          const arv::BlockPos surface{{
              coord.x * arv::ChunkSize + local.x,
              surface_y,
              coord.z * arv::ChunkSize + local.z}};
          found_surface_ravine =
              generated_block_at(generated.value(), surface) == arv::voxel::AirBlockVariantID;
        }

        for (const arv::world::GeneratedChunkSection& section : generated.value().sections) {
          for (arv::usize index = 0; index < arv::voxel::ChunkVolume; ++index) {
            if (section.blocks[index] != arv::voxel::AirBlockVariantID) {
              continue;
            }
            const arv::SectionLocalPos local = arv::voxel::local_pos(index);
            const arv::usize column =
                arv::voxel::local_column_index({local.value.x, local.value.z});
            const arv::i32 world_y = section.coord.value.y * arv::ChunkSize + local.value.y;
            if (world_y <= profile.surface_y_by_column[column] - 8 &&
                world_y >= config.resolved_dimension->raw.vertical.min_y + 8) {
              ++underground_air_blocks;
              underground_air_y_levels.insert(world_y);
            }
          }
        }
        found_deep_cave_network =
            underground_air_blocks > 256U && underground_air_y_levels.size() >= 12U;
      }
    }
  }

  EXPECT_TRUE(found_surface_ravine);
  EXPECT_TRUE(found_deep_cave_network)
      << "underground_air=" << underground_air_blocks
      << " y_levels=" << underground_air_y_levels.size();
}

TEST(WorldGenerationTests, PackTerrainCategoriesHaveDistinctProfiles) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::WorldGenerationConfig config = make_pack_worldgen_config(registry);
  ASSERT_NE(config.resolved_dimension, nullptr);

  struct TerrainStats {
    arv::usize count{};
    arv::i64 surface_sum{};
    arv::f32 slope_sum{};
    arv::i32 min_surface{std::numeric_limits<arv::i32>::max()};
    arv::i32 max_surface{std::numeric_limits<arv::i32>::min()};
  };
  std::unordered_map<std::string, TerrainStats> stats_by_biome;
  const std::array<arv::u64, 3> seeds{
      7390150598859ULL,
      32187121430964ULL,
      91827364554637ULL,
  };
  for (const arv::u64 seed : seeds) {
    for (arv::i32 z = -160; z <= 160; z += 8) {
      for (arv::i32 x = -160; x <= 160; x += 8) {
        const arv::world::worldgen::TerrainProfile profile =
            arv::world::worldgen::generate_terrain_profile(
                *config.resolved_dimension,
                seed,
                {x, z});
        for (arv::usize column = 0; column < arv::voxel::ChunkArea; ++column) {
          const arv::usize biome_index = profile.biome_by_column[column];
          ASSERT_LT(biome_index, config.resolved_dimension->biomes.size());
          TerrainStats& stats =
              stats_by_biome[config.resolved_dimension->biomes[biome_index].raw.id];
          ++stats.count;
          stats.surface_sum += profile.surface_y_by_column[column];
          stats.slope_sum += profile.slope_by_column[column];
          stats.min_surface = std::min(stats.min_surface, profile.surface_y_by_column[column]);
          stats.max_surface = std::max(stats.max_surface, profile.surface_y_by_column[column]);
        }
      }
    }
  }

  auto require_stats = [&](std::string_view id) -> const TerrainStats& {
    const auto found = stats_by_biome.find(std::string{id});
    EXPECT_NE(found, stats_by_biome.end()) << id;
    EXPECT_GT(found == stats_by_biome.end() ? 0U : found->second.count, 0U) << id;
    static const TerrainStats empty{};
    return found == stats_by_biome.end() ? empty : found->second;
  };
  auto average_height = [](const TerrainStats& stats) {
    return static_cast<arv::f32>(stats.surface_sum) /
           static_cast<arv::f32>(std::max<arv::usize>(1U, stats.count));
  };
  auto average_slope = [](const TerrainStats& stats) {
    return stats.slope_sum / static_cast<arv::f32>(std::max<arv::usize>(1U, stats.count));
  };

  const TerrainStats& plains = require_stats("arvane:worldgen/biome/plains");
  const TerrainStats& ocean = require_stats("arvane:worldgen/biome/ocean");
  const TerrainStats& deep_ocean = require_stats("arvane:worldgen/biome/deep_ocean");
  const TerrainStats& mountains = require_stats("arvane:worldgen/biome/mountains");
  const TerrainStats& highlands = require_stats("arvane:worldgen/biome/highlands");
  const TerrainStats& cliffs = require_stats("arvane:worldgen/biome/cliffs");

  const arv::f32 plains_height = average_height(plains);
  EXPECT_LT(average_height(ocean), plains_height - 8.0F);
  EXPECT_LT(average_height(deep_ocean), average_height(ocean) - 5.0F);
  EXPECT_GT(average_height(mountains), plains_height + 14.0F);
  EXPECT_GT(average_height(highlands), plains_height + 6.0F);
  EXPECT_GT(
      std::max({average_slope(mountains), average_slope(highlands), average_slope(cliffs)}),
      average_slope(plains) + 0.002F);
  EXPECT_GT(
      std::max({mountains.max_surface, highlands.max_surface, cliffs.max_surface}),
      plains.max_surface + 10);
}

TEST(WorldGenerationTests, PackSlopeAwareSurfacesExposeStoneOnSteepTerrain) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::WorldGenerationConfig config = make_pack_worldgen_config(registry);
  ASSERT_NE(config.resolved_dimension, nullptr);
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();

  bool checked_steep_surface = false;
  const std::array<arv::u64, 3> seeds{
      7390150598859ULL,
      32187121430964ULL,
      91827364554637ULL,
  };
  for (const arv::u64 seed : seeds) {
    for (arv::i32 chunk_z = -160; chunk_z <= 160 && !checked_steep_surface; chunk_z += 8) {
      for (arv::i32 chunk_x = -160; chunk_x <= 160 && !checked_steep_surface; chunk_x += 8) {
        const arv::ChunkCoord coord{chunk_x, chunk_z};
        const arv::world::worldgen::TerrainProfile profile =
            arv::world::worldgen::generate_terrain_profile(
                *config.resolved_dimension,
                seed,
                coord);
        for (arv::usize column = 0; column < arv::voxel::ChunkArea && !checked_steep_surface;
             ++column) {
          const arv::usize biome_index = profile.biome_by_column[column];
          const arv::world::worldgen::BiomeConfig& biome =
              config.resolved_dimension->biomes[biome_index].raw;
          const bool rocky = arv::world::worldgen::biome_has_tag(biome, "arvane:biome/rocky") ||
                             arv::world::worldgen::biome_has_tag(biome, "arvane:biome/mountain") ||
                             arv::world::worldgen::biome_has_tag(biome, "arvane:biome/cliff");
          const bool surface_rule_exposes_stone =
              (biome.id == "arvane:worldgen/biome/highlands" &&
               profile.slope_by_column[column] > 0.28F) ||
              (biome.id == "arvane:worldgen/biome/cliffs" &&
               profile.slope_by_column[column] > 0.27F) ||
              (biome.id == "arvane:worldgen/biome/rocky_coast" &&
               profile.slope_by_column[column] > 0.24F) ||
              (biome.id == "arvane:worldgen/biome/mountains" &&
               profile.slope_by_column[column] > 0.30F);
          if (!rocky || !surface_rule_exposes_stone) {
            continue;
          }
          const arv::ChunkLocalColumn local = arv::voxel::local_column(column);
          const arv::BlockPos surface{{
              coord.x * arv::ChunkSize + local.x,
              profile.surface_y_by_column[column],
              coord.z * arv::ChunkSize + local.z}};
          EXPECT_EQ(
              arv::world::worldgen::terrain_block_at(
                  *config.resolved_dimension,
                  profile,
                  surface),
              stone);
          checked_steep_surface = true;
        }
      }
    }
  }
  EXPECT_TRUE(checked_steep_surface);
}

TEST(WorldGenerationTests, BiomeBlendWeightsNormalizeAndIncludeNeighbors) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::WorldGenerationConfig config = make_pack_worldgen_config(registry);
  ASSERT_NE(config.resolved_dimension, nullptr);

  bool saw_neighbor_weight = false;
  for (arv::i32 z = -12; z <= 12; z += 2) {
    for (arv::i32 x = -12; x <= 12; x += 2) {
      const arv::world::worldgen::TerrainProfile profile =
          arv::world::worldgen::generate_terrain_profile(
              *config.resolved_dimension,
              7390150598859ULL,
              {x, z});
      for (const arv::world::worldgen::BiomeSample& sample :
           profile.biome_samples_by_column) {
        ASSERT_GT(sample.weight_count, 0U);
        ASSERT_LE(sample.weight_count, arv::world::worldgen::BiomeBlendWeightCount);
        arv::f32 total = 0.0F;
        for (arv::usize index = 0; index < sample.weight_count; ++index) {
          ASSERT_LT(sample.weights[index].biome_index, config.resolved_dimension->biomes.size());
          ASSERT_GE(sample.weights[index].weight, 0.0F);
          total += sample.weights[index].weight;
          saw_neighbor_weight =
              saw_neighbor_weight ||
              (sample.weights[index].biome_index != sample.dominant_biome &&
               sample.weights[index].weight > 0.05F);
        }
        EXPECT_NEAR(total, 1.0F, 0.001F);
      }
    }
  }

  EXPECT_TRUE(saw_neighbor_weight);
}

TEST(WorldGenerationTests, BlendedBiomeTerrainAvoidsHardBoundaryCliffs) {
  arv::world::worldgen::ResolvedDimension dimension = make_two_biome_blend_dimension();

  bool found_boundary = false;
  for (arv::i32 chunk_z = -16; chunk_z <= 16 && !found_boundary; ++chunk_z) {
    for (arv::i32 chunk_x = -16; chunk_x <= 16 && !found_boundary; ++chunk_x) {
      const arv::world::worldgen::TerrainProfile profile =
          arv::world::worldgen::generate_terrain_profile(dimension, 424242ULL, {chunk_x, chunk_z});
      for (arv::i32 z = 0; z < arv::ChunkSize && !found_boundary; ++z) {
        for (arv::i32 x = 0; x < arv::ChunkSize - 1 && !found_boundary; ++x) {
          const arv::usize lhs = arv::voxel::local_column_index({x, z});
          const arv::usize rhs = arv::voxel::local_column_index({x + 1, z});
          if (profile.biome_by_column[lhs] == profile.biome_by_column[rhs]) {
            continue;
          }
          found_boundary = true;
          EXPECT_LE(
              std::abs(profile.surface_y_by_column[lhs] -
                       profile.surface_y_by_column[rhs]),
              8);
          EXPECT_GT(profile.biome_samples_by_column[lhs].weight_count, 1U);
          EXPECT_GT(profile.biome_samples_by_column[rhs].weight_count, 1U);
        }
      }
    }
  }

  EXPECT_TRUE(found_boundary);
}

TEST(WorldGenerationTests, BiomeSampleScaleChangesClimateLookupDeterministically) {
  arv::world::worldgen::ResolvedDimension fine = make_two_biome_blend_dimension();
  arv::world::worldgen::ResolvedDimension coarse = fine;
  fine.biome_source.raw.sample_scale_xz = 1;
  coarse.biome_source.raw.sample_scale_xz = 16;

  bool found_scale_difference = false;
  for (arv::i32 chunk_z = -16; chunk_z <= 16 && !found_scale_difference; ++chunk_z) {
    for (arv::i32 chunk_x = -16; chunk_x <= 16 && !found_scale_difference; ++chunk_x) {
      const arv::ChunkCoord coord{chunk_x, chunk_z};
      const arv::world::worldgen::TerrainProfile first =
          arv::world::worldgen::generate_terrain_profile(fine, 8675309ULL, coord);
      const arv::world::worldgen::TerrainProfile second =
          arv::world::worldgen::generate_terrain_profile(fine, 8675309ULL, coord);
      const arv::world::worldgen::TerrainProfile coarser =
          arv::world::worldgen::generate_terrain_profile(coarse, 8675309ULL, coord);

      EXPECT_EQ(first.biome_by_column, second.biome_by_column);
      EXPECT_EQ(first.surface_y_by_column, second.surface_y_by_column);
      found_scale_difference =
          first.biome_by_column != coarser.biome_by_column ||
          first.surface_y_by_column != coarser.surface_y_by_column;
    }
  }

  EXPECT_TRUE(found_scale_difference);
}

TEST(WorldGenerationTests, DifferentSeedsCanChangeChunkOutput) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();

  auto first = arv::world::generate_chunk(make_request(registry, 1));
  auto second = arv::world::generate_chunk(make_request(registry, 2));

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_NE(first.value().sections, second.value().sections);
}

TEST(WorldGenerationTests, NoiseVariesTerrainHeightAcrossWorldSpace) {
  arv::world::WorldGenerationConfig config = make_worldgen_config();
  config.terrain_frequency = 0.035F;
  config.height_scale = 18;

  const arv::i32 first = arv::world::terrain_height_at(config, 77, 0, 0);
  bool varied = false;
  for (arv::i32 z = 0; z <= 128 && !varied; z += 16) {
    for (arv::i32 x = 0; x <= 128 && !varied; x += 16) {
      varied = arv::world::terrain_height_at(config, 77, x, z) != first;
    }
  }

  EXPECT_TRUE(varied);
}

TEST(WorldGenerationTests, SurfaceProfileMatchesScalarTerrainHeights) {
  arv::world::WorldGenerationConfig config = make_worldgen_config();
  config.terrain_frequency = 0.035F;
  config.height_scale = 18;
  const arv::ChunkCoord coord{-2, 3};

  const arv::world::GenerationSurfaceProfile profile =
      arv::world::terrain_height_profile_at(config, 77, coord);

  for (arv::i32 z = 0; z < arv::ChunkSize; ++z) {
    for (arv::i32 x = 0; x < arv::ChunkSize; ++x) {
      EXPECT_EQ(
          profile[static_cast<arv::usize>(x) +
                  arv::voxel::ChunkSide * static_cast<arv::usize>(z)],
          arv::world::terrain_height_at(
              config,
              77,
              coord.x * arv::ChunkSize + x,
              coord.z * arv::ChunkSize + z));
    }
  }
}

TEST(WorldGenerationTests, GeneratedChunkCarriesSurfaceProfile) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  arv::world::ChunkGenerationRequest request = make_request(registry, 99, {{-1, 0, 1}});

  auto generated = arv::world::generate_chunk(request);

  ASSERT_TRUE(generated);
  EXPECT_EQ(
      generated.value().surface_y_by_column,
      arv::world::terrain_height_profile_at(request.config, request.seed, request.coord));
}

TEST(WorldGenerationTests, GeneratedBlocksAreValidRegistryVariants) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto generated = arv::world::generate_chunk(make_request(registry, 99, {{-1, 0, 1}}));

  ASSERT_TRUE(generated);
  for (const arv::world::GeneratedChunkSection& section : generated.value().sections) {
    for (const arv::voxel::BlockVariantID variant : section.blocks) {
      EXPECT_LT(variant.value, registry.variant_count());
    }
  }
}

TEST(WorldGenerationTests, ColumnGenerationIncludesNegativeConfiguredSections) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  arv::world::ChunkGenerationRequest request = make_request(registry, 99, {{-1, 0, 1}});
  request.min_chunk_y = -2;
  request.max_chunk_y = 1;

  auto generated = arv::world::generate_chunk(request);

  ASSERT_TRUE(generated);
  ASSERT_EQ(generated.value().sections.size(), 4U);
  EXPECT_EQ(generated.value().sections.front().coord, (arv::ChunkSectionCoord{{-1, -2, 1}}));
  EXPECT_EQ(generated.value().sections.back().coord, (arv::ChunkSectionCoord{{-1, 1, 1}}));
}

TEST(WorldGenerationTests, SectionsAboveConfiguredTerrainRemainAir) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  arv::world::ChunkGenerationRequest request = make_request(registry, 99, {{0, 0, 0}});
  request.min_chunk_y = arv::floor_div(request.config.max_y, arv::ChunkSize) + 1;
  request.max_chunk_y = request.min_chunk_y;

  auto generated = arv::world::generate_chunk(request);

  ASSERT_TRUE(generated);
  ASSERT_EQ(generated.value().sections.size(), 1U);
  for (const arv::voxel::BlockVariantID variant : generated.value().sections.front().blocks) {
    EXPECT_EQ(variant, arv::voxel::AirBlockVariantID);
  }
}

TEST(WorldGenerationTests, WorldPublishesGeneratedChunksOnlyForItsWorldId) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto generated = arv::world::generate_chunk(make_request(registry, 99));
  ASSERT_TRUE(generated);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.configure_height(
      {
          generated.value().sections.front().coord.value.y,
          generated.value().sections.back().coord.value.y,
      });
  world.request_chunk_load({{0, 0, 0}});
  EXPECT_TRUE(world.publish_generated_chunk(generated.value()));
  const arv::voxel::ChunkSection* chunk = world.find_chunk({{0, 0, 0}});
  ASSERT_NE(chunk, nullptr);
  EXPECT_EQ(chunk->lifecycle_state(), arv::voxel::ChunkLifecycleState::Loaded);

  generated.value().world_id = arv::WorldID{2};
  EXPECT_FALSE(world.publish_generated_chunk(generated.value()));
}

TEST(WorldGenerationTests, ColumnGenerationPreservesLoadedSavedSections) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto generated = arv::world::generate_chunk(make_request(registry, 99));
  ASSERT_TRUE(generated);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.configure_height(
      {
          generated.value().sections.front().coord.value.y,
          generated.value().sections.back().coord.value.y,
      });

  const arv::voxel::BlockVariantID dirt = registry.find_variant("arvane:dirt").value();
  std::array<arv::voxel::BlockVariantID, arv::voxel::ChunkVolume> saved_blocks{};
  saved_blocks.fill(dirt);
  world.apply_chunk_snapshot({{0, 0, 0}}, saved_blocks, std::nullopt);
  world.request_chunk_load(generated.value().sections.back().coord);

  ASSERT_TRUE(world.publish_generated_chunk(generated.value()));

  const arv::voxel::ChunkSection* saved_section = world.find_chunk({{0, 0, 0}});
  ASSERT_NE(saved_section, nullptr);
  EXPECT_EQ(saved_section->lifecycle_state(), arv::voxel::ChunkLifecycleState::Loaded);
  EXPECT_EQ(saved_section->blocks(), saved_blocks);

  const arv::voxel::ChunkSection* filled_missing_section =
      world.find_chunk(generated.value().sections.back().coord);
  ASSERT_NE(filled_missing_section, nullptr);
  EXPECT_EQ(
      filled_missing_section->lifecycle_state(),
      arv::voxel::ChunkLifecycleState::Loaded);
}
