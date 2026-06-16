#pragma once

#include "arv/asset/pack.hpp"
#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/entity/components.hpp"
#include "arv/player/item.hpp"
#include "arv/render/entity_model.hpp"
#include "arv/render/render_types.hpp"
#include "arv/voxel/block.hpp"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::client {

inline constexpr usize BreakStageTextureCount = 10;

struct BlockOutlineRenderConfig {
  Vec3f color{0.02F, 0.02F, 0.02F};
  f32 thickness{0.55F};
};

struct EntityRenderAssets {
  asset::TextureAtlas texture_atlas{};
  std::vector<render::EntityModelAsset> models{};
  std::vector<render::EntityAnimationAsset> animations{};
  HashMap<EntityTypeID, render::EntityModelID> model_by_type{};
  HashMap<EntityTypeID, render::EntityAnimationID> animation_by_type{};
  render::EntityModelID player_skin_model{};
  render::EntityModelID player_slim_model{};
  render::EntityModelID player_slim_skin_model{};
  render::EntityModelID first_person_player_model{};
  render::EntityModelID first_person_player_skin_model{};
  render::EntityModelID first_person_player_slim_model{};
  render::EntityModelID first_person_player_slim_skin_model{};
  render::EntityModelID chunk_bounds_model{};
  HashMap<player::ItemID, render::EntityModelID> model_by_item{};
  std::vector<std::array<render::EntityModelID, BreakStageTextureCount>> break_overlay_model_by_variant{};
  std::vector<render::EntityModelID> outline_model_by_variant{};
  std::vector<render::EntityModelID> block_attached_model_by_variant{};
  std::vector<render::EntityAnimationID> block_attached_animation_by_variant{};
  std::vector<Quatf> block_attached_rotation_by_variant{};
};

struct ClientEntityRenderAssets {
  player::ItemRegistry item_registry{};
  entity::EntityRegistry entity_registry{};
  entity::VehicleDefinitionRegistry vehicle_registry{};
  EntityRenderAssets render_assets{};
};

struct ClientEntityRenderAssetLoadOptions {
  FunctionRef<bool(std::string_view)> item_behavior_exists{};
  FunctionRef<bool(std::string_view)> entity_goal_exists{};
  std::string default_block_item_behavior_id{"arvane:item_behavior/block"};
  bool validate_entity_goal_ids{};
  BlockOutlineRenderConfig outline_config{};
  asset::TextureAtlasBuildOptions atlas_options{
      .max_page_size = 1024,
      .max_pages = 1,
      .padding = 2,
      .require_uniform_non_fallback_size = false};
};

struct ClientEntityRenderAssetLoadError {
  std::string category{};
  std::string message{};
};

using EntityRenderAssetsResult = Expected<EntityRenderAssets, asset::AssetError>;
using ClientEntityRenderAssetsResult =
    Expected<ClientEntityRenderAssets, ClientEntityRenderAssetLoadError>;

[[nodiscard]] EntityRenderAssetsResult build_entity_render_assets(
    asset::ResourceManager& resources,
    const entity::EntityRegistry& entity_registry,
    const player::ItemRegistry& item_registry,
    const voxel::BlockRegistry& block_registry,
    const voxel::BlockRenderData& block_render_data,
    std::span<const asset::BlockModelAssetData> block_models = {},
    BlockOutlineRenderConfig outline_config = {},
    asset::TextureAtlasBuildOptions atlas_options =
        {.max_page_size = 1024,
         .max_pages = 1,
         .padding = 2,
         .require_uniform_non_fallback_size = false});

[[nodiscard]] ClientEntityRenderAssetsResult load_client_entity_render_assets(
    const asset::PackManager& packs,
    asset::ResourceManager& resources,
    const voxel::BlockRegistry& block_registry,
    const voxel::BlockRenderData& block_render_data,
    std::span<const asset::BlockModelAssetData> block_models,
    ClientEntityRenderAssetLoadOptions options = {});

} // namespace arv::client
