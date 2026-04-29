#pragma once

#include "arv/asset/texture_atlas.hpp"
#include "arv/client/ui.hpp"
#include "arv/player/item.hpp"
#include "arv/server/server_runtime.hpp"

#include <vector>

namespace arv::desktop {

[[nodiscard]] std::vector<client::UiItemStackView> inventory_slots_for_ui(
    const player::Inventory& inventory,
    const player::ItemRegistry& item_registry,
    const asset::TextureAtlas& item_atlas,
    const asset::BlockTextureAtlas& block_atlas);

[[nodiscard]] std::vector<client::PlayerListEntryView> player_list_for_ui(
    const server::ServerRuntime& server);

} // namespace arv::desktop
