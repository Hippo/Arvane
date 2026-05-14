#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/coordinates.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::command {

struct OnlinePlayerCommandTarget {
  SessionID session_id{};
  PlayerID player_id{};
  WorldID world_id{};
  std::string display_name{};
  Vec3f position{};
};

struct PlayerBlockSelection {
  std::optional<WorldID> world{};
  std::optional<BlockPos> a{};
  std::optional<BlockPos> b{};
};

using OnlinePlayerByIDFn =
    FunctionRef<std::optional<OnlinePlayerCommandTarget>(PlayerID)>;
using OnlinePlayerByNameFn =
    FunctionRef<std::optional<OnlinePlayerCommandTarget>(std::string_view)>;
using TeleportPlayerFn = FunctionRef<bool(PlayerID, Vec3f)>;
using PlayerSelectionByIDFn =
    FunctionRef<std::optional<PlayerBlockSelection>(PlayerID)>;
using SetPlayerSelectionFn = FunctionRef<bool(PlayerID, const PlayerBlockSelection&)>;
using WorldBlockReadFn = FunctionRef<std::optional<u32>(WorldID, BlockPos)>;
using WorldBlockWriteFn = FunctionRef<bool(WorldID, BlockPos, u32)>;
using BlockVariantNameFn = FunctionRef<std::optional<std::string>(u32)>;
using BlockVariantByNameFn = FunctionRef<std::optional<u32>(std::string_view)>;
using AirBlockVariantFn = FunctionRef<std::optional<u32>()>;
using BlueprintListFn = FunctionRef<std::vector<std::string>(WorldID)>;
using BlueprintLoadFn = FunctionRef<std::optional<std::string>(WorldID, std::string_view)>;
using BlueprintSaveFn = FunctionRef<bool(WorldID, std::string_view, std::string_view)>;

struct ServerCommandServices {
  OnlinePlayerByIDFn online_player_by_id{};
  OnlinePlayerByNameFn online_player_by_name{};
  TeleportPlayerFn teleport_player{};
  PlayerSelectionByIDFn player_selection_by_id{};
  SetPlayerSelectionFn set_player_selection{};
  WorldBlockReadFn read_block{};
  WorldBlockWriteFn write_block{};
  BlockVariantNameFn block_variant_name{};
  BlockVariantByNameFn block_variant_by_name{};
  AirBlockVariantFn air_block_variant{};
  BlueprintListFn list_blueprints{};
  BlueprintLoadFn load_blueprint{};
  BlueprintSaveFn save_blueprint{};
};

} // namespace arv::command
