#pragma once

#include "arv/persistence/save.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/world/world_runtime.hpp"

#include <string_view>
#include <vector>

namespace arv::desktop {

[[nodiscard]] std::vector<persistence::SavedChunkSection> capture_saved_chunks(
    const world::WorldRuntime& world);

[[nodiscard]] std::vector<persistence::PlayerSaveData> capture_player_saves(
    const server::ServerRuntime& server,
    std::string_view world_name);

[[nodiscard]] std::vector<entity::PersistentEntityRecord> capture_saved_entities(
    const world::WorldRuntime& world);

void apply_loaded_player_saves(
    server::ServerRuntime& server,
    std::string_view world_name,
    const std::vector<persistence::PlayerSaveData>& loaded_players);

} // namespace arv::desktop
