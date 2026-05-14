#pragma once

#include "arv/asset/texture_atlas.hpp"
#include "arv/player/item.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/base/ui.hpp"

#include <optional>
#include <vector>

namespace arv::desktop {

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

[[nodiscard]] std::vector<ui::PlayerListEntryView> player_list_for_ui(
    const server::ServerRuntime& server);

} // namespace arv::desktop
