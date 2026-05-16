#pragma once

#include "arv/blueprint/blueprint.hpp"
#include "arv/world/worldgen/common.hpp"
#include "arv/voxel/block.hpp"

#include <string>
#include <variant>
#include <vector>

namespace arv::world::worldgen {

struct ReplaceRuleConfig {
  bool specified{};
  bool only_air{};
  std::vector<std::string> blocks{};
};

struct BlockSupportConfig {
  std::vector<std::string> blocks{};
};

struct SimpleTreeFeatureConfig {
  std::string trunk_block{};
  IntRange trunk_height{4, 7};
  std::string leaves_block{};
  IntRange leaves_radius{2, 3};
};

struct PatchFeatureConfig {
  std::string block{};
  i32 tries{1};
  i32 radius_xz{1};
  i32 radius_y{};
  ReplaceRuleConfig replace{};
  BlockSupportConfig place_on{};
};

struct DiskFeatureConfig {
  std::string block{};
  IntRange radius{2, 5};
  i32 depth{1};
  ReplaceRuleConfig replace{};
};

struct OreVeinFeatureConfig {
  std::string block{};
  ReplaceRuleConfig replace{};
  IntRange vein_size{4, 9};
};

struct SingleBlockFeatureConfig {
  std::string block{};
  ReplaceRuleConfig replace{};
  BlockSupportConfig place_on{};
};

struct BlueprintFeatureConfig {
  ResourceRef blueprint{};
  bool random_y_rotation{};
  f32 integrity{1.0F};
  ReplaceRuleConfig replace{};
};

using ConfiguredFeatureVariant = std::variant<
    SimpleTreeFeatureConfig,
    PatchFeatureConfig,
    DiskFeatureConfig,
    OreVeinFeatureConfig,
    SingleBlockFeatureConfig,
    BlueprintFeatureConfig>;

struct ConfiguredFeatureConfig {
  std::string id{};
  u32 version{SupportedSchemaVersion};
  std::string type{};
  ConfiguredFeatureVariant config{};
};

struct PlacementCountConfig {
  enum class Type : u8 {
    ChancePerChunk,
    Uniform,
  };

  Type type{Type::ChancePerChunk};
  f32 chance{};
  i32 min{};
  i32 max{};
};

struct PlacementHeightConfig {
  enum class Type : u8 {
    Surface,
    Uniform,
    Trapezoid,
  };

  Type type{Type::Surface};
  i32 min_y{};
  i32 max_y{};
  i32 peak_y{};
};

struct PlacementFilterConfig {
  enum class Type : u8 {
    Biome,
    BiomeTag,
    BlockBelow,
    Replaceable,
    SlopeLessThan,
    SlopeGreaterThan,
    Underwater,
  };

  Type type{Type::Replaceable};
  ResourceRef biome{};
  ResourceRef tag{};
  std::vector<std::string> blocks{};
  f32 value{};
  bool bool_value{};
};

struct PlacedFeatureConfig {
  std::string id{};
  u32 version{SupportedSchemaVersion};
  ResourceRef feature{};
  u64 salt{};
  GenerationStage stage{GenerationStage::SurfaceDecoration};
  PlacementCountConfig count{};
  PlacementHeightConfig height{};
  std::vector<PlacementFilterConfig> filters{};
};

struct ResolvedReplaceRule {
  bool specified{};
  bool only_air{};
  std::vector<voxel::BlockVariantID> blocks{};
};

struct ResolvedConfiguredFeature {
  std::string id{};
  std::string type{};
  ConfiguredFeatureConfig raw{};
  std::vector<voxel::BlockVariantID> blocks{};
  std::vector<voxel::BlockVariantID> support_blocks{};
  ResolvedReplaceRule replace{};
  blueprint::Blueprint blueprint{};
  std::vector<voxel::BlockVariantID> blueprint_palette{};
  i32 radius_blocks{};
};

struct ResolvedPlacementFilter {
  PlacementFilterConfig raw{};
  std::vector<voxel::BlockVariantID> blocks{};
  std::optional<usize> biome_index{};
  std::optional<usize> biome_tag_index{};
};

struct ResolvedPlacedFeature {
  PlacedFeatureConfig raw{};
  usize feature_index{};
  std::vector<ResolvedPlacementFilter> filters{};
  i32 radius_blocks{};
};

} // namespace arv::world::worldgen
