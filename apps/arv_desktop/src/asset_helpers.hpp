#pragma once

#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/entity/components.hpp"
#include "arv/player/item.hpp"
#include "arv/render/entity_model.hpp"
#include "arv/render/render_types.hpp"
#include "arv/ui/ui.hpp"

#include <array>
#include <span>
#include <vector>

namespace arv::desktop {

inline constexpr usize BreakStageTextureCount = 10;

struct BlockOutlineRenderConfig {
  Vec3f color{0.02F, 0.02F, 0.02F};
  f32 thickness{0.55F};
};

struct EntityRenderAssets {
  asset::TextureAtlas texture_atlas{};
  std::vector<render::EntityModelAsset> models{};
  HashMap<EntityTypeID, render::EntityModelID> model_by_type{};
  HashMap<player::ItemID, render::EntityModelID> model_by_item{};
  std::vector<std::array<render::EntityModelID, BreakStageTextureCount>> break_overlay_model_by_variant{};
  std::vector<render::EntityModelID> outline_model_by_variant{};
};

using EntityRenderAssetsResult = Expected<EntityRenderAssets, asset::AssetError>;

[[nodiscard]] render::TextureUploadData texture_upload_from_image(const asset::Image& image);

[[nodiscard]] std::array<Vec2f, 4> normalized_uvs(
    const std::array<Vec2i, 4>& pixels,
    u32 width,
    u32 height);

[[nodiscard]] ui::UiImageData image_data_from_atlas_entry(
    const asset::TextureAtlasEntry& entry,
    u32 page_width,
    u32 page_height,
    render::UiTextureSource source);

[[nodiscard]] asset::TextureAtlasResult build_item_texture_atlas(
    asset::ResourceManager& resources,
    const player::ItemRegistry& item_registry);

[[nodiscard]] EntityRenderAssetsResult build_entity_render_assets(
    asset::ResourceManager& resources,
    const entity::EntityRegistry& entity_registry,
    const player::ItemRegistry& item_registry,
    const voxel::BlockRegistry& block_registry,
    std::span<const asset::BlockModelAssetData> block_models = {},
    BlockOutlineRenderConfig outline_config = {});

} // namespace arv::desktop
