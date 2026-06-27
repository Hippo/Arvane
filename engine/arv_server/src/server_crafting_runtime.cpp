#include "server_runtime_internal.hpp"

#include <algorithm>
#include <utility>

namespace arv::server::detail {
namespace {

[[nodiscard]] protocol::CraftRecipeResult protocol_result_from_failure(
    crafting::CraftingFailureReason reason) noexcept {
  switch (reason) {
    case crafting::CraftingFailureReason::None:
      return protocol::CraftRecipeResult::Success;
    case crafting::CraftingFailureReason::UnknownRecipe:
      return protocol::CraftRecipeResult::UnknownRecipe;
    case crafting::CraftingFailureReason::RecipeNotUnlocked:
      return protocol::CraftRecipeResult::RecipeNotUnlocked;
    case crafting::CraftingFailureReason::MissingIngredients:
      return protocol::CraftRecipeResult::MissingIngredients;
    case crafting::CraftingFailureReason::InventoryFull:
      return protocol::CraftRecipeResult::InventoryFull;
    case crafting::CraftingFailureReason::InvalidRequest:
      return protocol::CraftRecipeResult::InvalidRequest;
    case crafting::CraftingFailureReason::StationRequired:
      return protocol::CraftRecipeResult::StationRequired;
  }
  return protocol::CraftRecipeResult::InvalidRequest;
}

void append_unique_unlocks(
    std::vector<std::string>& out,
    std::vector<std::string> values) {
  for (std::string& value : values) {
    if (std::find(out.begin(), out.end(), value) == out.end()) {
      out.push_back(std::move(value));
    }
  }
}

} // namespace

void ServerRuntimeImpl::send_recipe_book_snapshot(const ServerSession& session) {
  const world::WorldRuntime* runtime = world_for_session(session);
  const player::PlayerState* player_state =
      runtime == nullptr ? nullptr : runtime->find_player(session.player_id);
  if (player_state == nullptr) {
    return;
  }

  protocol::ClientboundRecipeBookSnapshot snapshot;
  snapshot.unlocked_recipe_ids = player_state->unlocked_recipe_ids;
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      session.connection_id,
      protocol::MessageType::ClientboundRecipeBookSnapshot,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_clientbound_recipe_book_snapshot(snapshot));
  log_send_failure_if_dedicated(mode_, session.connection_id, send_result);
}

void ServerRuntimeImpl::send_recipe_unlocks(
    const ServerSession& session,
    std::span<const std::string> unlocked_recipe_ids) {
  if (unlocked_recipe_ids.empty()) {
    return;
  }

  protocol::ClientboundRecipeUnlocks unlocks;
  unlocks.unlocked_recipe_ids.assign(
      unlocked_recipe_ids.begin(),
      unlocked_recipe_ids.end());
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      session.connection_id,
      protocol::MessageType::ClientboundRecipeUnlocks,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_clientbound_recipe_unlocks(unlocks));
  log_send_failure_if_dedicated(mode_, session.connection_id, send_result);
}

void ServerRuntimeImpl::send_craft_recipe_result(
    const ServerSession& session,
    std::string_view recipe_id,
    u16 requested_count,
    u16 crafted_count,
    protocol::CraftRecipeResult result) {
  protocol::ClientboundCraftRecipeResult packet;
  packet.recipe_id = std::string{recipe_id};
  packet.requested_count = requested_count;
  packet.crafted_count = crafted_count;
  packet.result = result;
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      session.connection_id,
      protocol::MessageType::ClientboundCraftRecipeResult,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_clientbound_craft_recipe_result(packet));
  log_send_failure_if_dedicated(mode_, session.connection_id, send_result);
}

void ServerRuntimeImpl::normalize_player_recipe_book(
    player::PlayerState& player_state) const {
  if (crafting_registry_ == nullptr) {
    player_state.unlocked_recipe_ids.clear();
    return;
  }
  player_state.unlocked_recipe_ids = crafting::normalized_recipe_unlocks(
      std::span<const std::string>{
          player_state.unlocked_recipe_ids.data(),
          player_state.unlocked_recipe_ids.size()},
      *crafting_registry_);
}

std::vector<std::string> ServerRuntimeImpl::unlock_recipes_for_collected_stack(
    player::PlayerState& player_state,
    const player::ItemStack& stack) const {
  if (crafting_registry_ == nullptr || stack.count == 0U) {
    return {};
  }
  return crafting::unlock_recipes_for_collected_item(
      player_state.unlocked_recipe_ids,
      stack.item,
      *crafting_registry_);
}

void ServerRuntimeImpl::publish_recipe_unlocks_for_collected_stack(
    const ServerSession& session,
    player::PlayerState& player_state,
    const player::ItemStack& stack) {
  const std::vector<std::string> unlocked =
      unlock_recipes_for_collected_stack(player_state, stack);
  send_recipe_unlocks(session, unlocked);
}

void ServerRuntimeImpl::handle_craft_recipe(
    net::ConnectionID connection_id,
    const protocol::ServerboundCraftRecipe& request) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }

  world::WorldRuntime* runtime = world_for_session(*session);
  player::PlayerState* player_state =
      runtime == nullptr ? nullptr : runtime->find_player(session->player_id);
  if (player_state == nullptr || item_registry_ == nullptr ||
      crafting_registry_ == nullptr || request.count == 0U) {
    send_craft_recipe_result(
        *session,
        request.recipe_id,
        request.count,
        0U,
        protocol::CraftRecipeResult::InvalidRequest);
    return;
  }

  normalize_player_recipe_book(*player_state);
  const crafting::CraftingRecipe* recipe =
      crafting_registry_->recipe(request.recipe_id);
  if (recipe == nullptr) {
    send_craft_recipe_result(
        *session,
        request.recipe_id,
        request.count,
        0U,
        protocol::CraftRecipeResult::UnknownRecipe);
    return;
  }
  if (!crafting::recipe_is_unlocked(
          std::span<const std::string>{
              player_state->unlocked_recipe_ids.data(),
              player_state->unlocked_recipe_ids.size()},
          recipe->id)) {
    send_craft_recipe_result(
        *session,
        request.recipe_id,
        request.count,
        0U,
        protocol::CraftRecipeResult::RecipeNotUnlocked);
    return;
  }

  u16 crafted_count{};
  protocol::CraftRecipeResult result = protocol::CraftRecipeResult::Success;
  std::vector<std::string> unlocked_by_results;
  for (u16 attempt = 0; attempt < request.count; ++attempt) {
    const crafting::CraftingAttemptResult craft_result =
        crafting::craft_recipe_once(
            player_state->inventory,
            *recipe,
            *item_registry_);
    if (!craft_result.success()) {
      result = protocol_result_from_failure(craft_result.reason);
      break;
    }
    ++crafted_count;
    append_unique_unlocks(
        unlocked_by_results,
        unlock_recipes_for_collected_stack(
            *player_state,
            player::ItemStack{recipe->result.item, recipe->result.count}));
  }

  if (!unlocked_by_results.empty()) {
    send_recipe_unlocks(*session, unlocked_by_results);
  }
  send_craft_recipe_result(
      *session,
      request.recipe_id,
      request.count,
      crafted_count,
      result);
}

} // namespace arv::server::detail
