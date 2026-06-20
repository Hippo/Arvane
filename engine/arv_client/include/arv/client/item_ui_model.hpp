#pragma once

#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/base/ui/widgets/items.hpp"
#include "arv/player/inventory.hpp"
#include "arv/render/render_types.hpp"

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace arv::client {

inline constexpr std::string_view GameplayCrosshairTextureAsset =
    "arvane:texture/ui/crosshair";

struct InventoryUiProjectionInput {
  bool has_authoritative_inventory{};
  bool container_open{};
  const player::Inventory& inventory;
  const player::Inventory& container_inventory;
  const std::optional<player::ItemStack>& carried_stack;
  const player::ItemRegistry& item_registry;
  const asset::TextureAtlas& item_atlas;
  const asset::BlockTextureAtlas& block_atlas;
};

struct InventoryUiProjection {
  std::vector<ui::UiItemStackView> inventory_slots{};
  std::vector<ui::UiItemStackView> container_slots{};
  std::optional<ui::UiItemStackView> carried_stack{};
};

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
        {.max_page_size = 1024, .max_pages = 1, .padding = 2},
    std::span<const asset::AssetID> extra_texture_assets = {});

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

[[nodiscard]] InventoryUiProjection project_inventory_ui(
    const InventoryUiProjectionInput& input);

} // namespace arv::client
