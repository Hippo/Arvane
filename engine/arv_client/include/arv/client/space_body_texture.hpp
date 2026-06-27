#pragma once

#include "arv/asset/pack.hpp"
#include "arv/asset/resource.hpp"
#include "arv/client/entity_render_assets.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/space_definition.hpp"
#include "arv/world/worldgen/registry.hpp"

#include <string>
#include <vector>

namespace arv::client {

struct SpaceBodyTextureBuildOptions {
  u32 width{128};
  u32 height{64};
  i32 sample_span_blocks{512};
};

struct SpaceBodyTextureAssets {
  std::vector<GeneratedTextureAsset> generated_textures{};
  std::vector<std::string> texture_assets{};
};

[[nodiscard]] SpaceBodyTextureAssets build_space_body_texture_assets(
    asset::ResourceManager& resources,
    const world::SpaceDefinitionRegistry& space_definitions,
    const world::worldgen::WorldGenRegistry& worldgen_registry,
    const voxel::BlockRegistry& block_registry,
    SpaceBodyTextureBuildOptions options = {});

[[nodiscard]] SpaceBodyTextureAssets load_space_body_texture_assets_from_packs(
    const asset::PackManager& packs,
    asset::ResourceManager& resources,
    const voxel::BlockRegistry& block_registry,
    SpaceBodyTextureBuildOptions options = {});

} // namespace arv::client
