#pragma once

#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/base/ui/widgets/items.hpp"
#include "arv/player/inventory.hpp"
#include "arv/render/render_types.hpp"

#include <array>
#include <optional>
#include <vector>

namespace arv::client {

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
    const player::ItemRegistry& item_registry,
    asset::TextureAtlasBuildOptions options =
        {.max_page_size = 1024, .max_pages = 1, .padding = 2});

[[nodiscard]] std::vector<ui::UiItemStackView> inventory_slots_for_ui(
    const player::Inventory& inventory,
    const player::ItemRegistry& item_registry,
    const asset::TextureAtlas& item_atlas,
    const asset::BlockTextureAtlas& block_atlas);

[[nodiscard]] std::optional<ui::UiItemStackView> item_stack_for_ui(
    const std::optional<player::ItemStack>& stack,
    const player::ItemRegistry& item_registry,
    const asset::TextureAtlas& item_atlas,
    const asset::BlockTextureAtlas& block_atlas);

} // namespace arv::client
