#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/primitives.hpp"
#include "arv/protocol/message_writer.hpp"

#include <span>
#include <string>
#include <vector>

namespace arv::protocol {

inline constexpr u32 MaxCraftingRecipeIDLength = 128;
inline constexpr u32 MaxRecipeBookUnlocks = 4096;

enum class CraftRecipeResult : u8 {
  Success = 0,
  UnknownRecipe = 1,
  RecipeNotUnlocked = 2,
  MissingIngredients = 3,
  InventoryFull = 4,
  InvalidRequest = 5,
  StationRequired = 6,
};

struct ServerboundCraftRecipe {
  std::string recipe_id{};
  u16 count{1};
  u32 client_action_id{};
};

struct ClientboundCraftRecipeResult {
  std::string recipe_id{};
  u16 requested_count{};
  u16 crafted_count{};
  CraftRecipeResult result{CraftRecipeResult::Success};
};

struct ClientboundRecipeBookSnapshot {
  std::vector<std::string> unlocked_recipe_ids{};
};

struct ClientboundRecipeUnlocks {
  std::vector<std::string> unlocked_recipe_ids{};
};

std::vector<u8> serialize_serverbound_craft_recipe(
    const ServerboundCraftRecipe& request);
Expected<ServerboundCraftRecipe, ProtocolError> deserialize_serverbound_craft_recipe(
    std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_craft_recipe_result(
    const ClientboundCraftRecipeResult& result);
Expected<ClientboundCraftRecipeResult, ProtocolError>
deserialize_clientbound_craft_recipe_result(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_recipe_book_snapshot(
    const ClientboundRecipeBookSnapshot& snapshot);
Expected<ClientboundRecipeBookSnapshot, ProtocolError>
deserialize_clientbound_recipe_book_snapshot(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_recipe_unlocks(
    const ClientboundRecipeUnlocks& unlocks);
Expected<ClientboundRecipeUnlocks, ProtocolError> deserialize_clientbound_recipe_unlocks(
    std::span<const u8> bytes);

} // namespace arv::protocol
