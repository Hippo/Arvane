#include "arv/protocol/crafting.hpp"

#include "arv/protocol/message_primitives.hpp"

#include <algorithm>
#include <utility>

namespace arv::protocol {
namespace {

[[nodiscard]] bool valid_recipe_result(CraftRecipeResult result) noexcept {
  switch (result) {
    case CraftRecipeResult::Success:
    case CraftRecipeResult::UnknownRecipe:
    case CraftRecipeResult::RecipeNotUnlocked:
    case CraftRecipeResult::MissingIngredients:
    case CraftRecipeResult::InventoryFull:
    case CraftRecipeResult::InvalidRequest:
    case CraftRecipeResult::StationRequired:
      return true;
  }
  return false;
}

[[nodiscard]] Expected<std::string, ProtocolError> read_recipe_id(
    MessageReader& reader,
    std::string_view field) {
  auto value = reader.read_string();
  if (!value) {
    return Expected<std::string, ProtocolError>::failure(
        ProtocolError{"failed to decode " + std::string{field}});
  }
  if (value.value().empty()) {
    return Expected<std::string, ProtocolError>::failure(
        ProtocolError{std::string{field} + " must be non-empty"});
  }
  if (value.value().size() > MaxCraftingRecipeIDLength) {
    return Expected<std::string, ProtocolError>::failure(
        ProtocolError{std::string{field} + " is too long"});
  }
  return Expected<std::string, ProtocolError>::success(std::move(value.value()));
}

[[nodiscard]] Expected<std::vector<std::string>, ProtocolError> read_unlock_list(
    MessageReader& reader) {
  auto count = reader.read<u32>();
  if (!count) {
    return Expected<std::vector<std::string>, ProtocolError>::failure(
        ProtocolError{"failed to decode recipe unlock count"});
  }
  if (count.value() > MaxRecipeBookUnlocks) {
    return Expected<std::vector<std::string>, ProtocolError>::failure(
        ProtocolError{"too many recipe unlocks"});
  }
  std::vector<std::string> ids;
  ids.reserve(count.value());
  for (u32 index = 0; index < count.value(); ++index) {
    auto id = read_recipe_id(reader, "recipe unlock id");
    if (!id) {
      return Expected<std::vector<std::string>, ProtocolError>::failure(id.error());
    }
    ids.push_back(std::move(id.value()));
  }
  return Expected<std::vector<std::string>, ProtocolError>::success(std::move(ids));
}

void write_unlock_list(MessageWriter& writer, std::span<const std::string> ids) {
  writer.write<u32>(static_cast<u32>(ids.size()));
  for (const std::string& id : ids) {
    writer.write_string(id);
  }
}

} // namespace

std::vector<u8> serialize_serverbound_craft_recipe(
    const ServerboundCraftRecipe& request) {
  MessageWriter writer;
  writer.write_string(request.recipe_id);
  writer.write<u16>(request.count);
  writer.write<u32>(request.client_action_id);
  return std::move(writer).take_bytes();
}

Expected<ServerboundCraftRecipe, ProtocolError> deserialize_serverbound_craft_recipe(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto recipe_id = read_recipe_id(reader, "craft recipe id");
  auto count = reader.read<u16>();
  auto client_action_id = reader.read<u32>();
  if (!recipe_id || !count || !client_action_id) {
    return Expected<ServerboundCraftRecipe, ProtocolError>::failure(
        recipe_id ? ProtocolError{"failed to decode craft request"} : recipe_id.error());
  }
  if (count.value() == 0U) {
    return Expected<ServerboundCraftRecipe, ProtocolError>::failure(
        ProtocolError{"craft count must be non-zero"});
  }
  auto trailing = require_no_trailing_bytes(reader, "craft request");
  if (!trailing) {
    return Expected<ServerboundCraftRecipe, ProtocolError>::failure(trailing.error());
  }

  ServerboundCraftRecipe request;
  request.recipe_id = std::move(recipe_id.value());
  request.count = count.value();
  request.client_action_id = client_action_id.value();
  return Expected<ServerboundCraftRecipe, ProtocolError>::success(std::move(request));
}

std::vector<u8> serialize_clientbound_craft_recipe_result(
    const ClientboundCraftRecipeResult& result) {
  MessageWriter writer;
  writer.write_string(result.recipe_id);
  writer.write<u16>(result.requested_count);
  writer.write<u16>(result.crafted_count);
  writer.write<CraftRecipeResult>(result.result);
  return std::move(writer).take_bytes();
}

Expected<ClientboundCraftRecipeResult, ProtocolError>
deserialize_clientbound_craft_recipe_result(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto recipe_id = read_recipe_id(reader, "craft recipe id");
  auto requested_count = reader.read<u16>();
  auto crafted_count = reader.read<u16>();
  auto result = reader.read<CraftRecipeResult>();
  if (!recipe_id || !requested_count || !crafted_count || !result) {
    return Expected<ClientboundCraftRecipeResult, ProtocolError>::failure(
        recipe_id ? ProtocolError{"failed to decode craft result"} : recipe_id.error());
  }
  if (!valid_recipe_result(result.value())) {
    return Expected<ClientboundCraftRecipeResult, ProtocolError>::failure(
        ProtocolError{"invalid craft result enum value"});
  }
  auto trailing = require_no_trailing_bytes(reader, "craft result");
  if (!trailing) {
    return Expected<ClientboundCraftRecipeResult, ProtocolError>::failure(trailing.error());
  }

  ClientboundCraftRecipeResult packet;
  packet.recipe_id = std::move(recipe_id.value());
  packet.requested_count = requested_count.value();
  packet.crafted_count = crafted_count.value();
  packet.result = result.value();
  return Expected<ClientboundCraftRecipeResult, ProtocolError>::success(std::move(packet));
}

std::vector<u8> serialize_clientbound_recipe_book_snapshot(
    const ClientboundRecipeBookSnapshot& snapshot) {
  MessageWriter writer;
  write_unlock_list(writer, snapshot.unlocked_recipe_ids);
  return std::move(writer).take_bytes();
}

Expected<ClientboundRecipeBookSnapshot, ProtocolError>
deserialize_clientbound_recipe_book_snapshot(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto ids = read_unlock_list(reader);
  if (!ids) {
    return Expected<ClientboundRecipeBookSnapshot, ProtocolError>::failure(ids.error());
  }
  auto trailing = require_no_trailing_bytes(reader, "recipe book snapshot");
  if (!trailing) {
    return Expected<ClientboundRecipeBookSnapshot, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundRecipeBookSnapshot, ProtocolError>::success({std::move(ids.value())});
}

std::vector<u8> serialize_clientbound_recipe_unlocks(
    const ClientboundRecipeUnlocks& unlocks) {
  MessageWriter writer;
  write_unlock_list(writer, unlocks.unlocked_recipe_ids);
  return std::move(writer).take_bytes();
}

Expected<ClientboundRecipeUnlocks, ProtocolError> deserialize_clientbound_recipe_unlocks(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto ids = read_unlock_list(reader);
  if (!ids) {
    return Expected<ClientboundRecipeUnlocks, ProtocolError>::failure(ids.error());
  }
  auto trailing = require_no_trailing_bytes(reader, "recipe unlocks");
  if (!trailing) {
    return Expected<ClientboundRecipeUnlocks, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundRecipeUnlocks, ProtocolError>::success({std::move(ids.value())});
}

} // namespace arv::protocol
