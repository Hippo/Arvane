#include "arv/base/base_module.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/server/server_runtime.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
#include <string_view>

namespace arv::base {
namespace {

[[nodiscard]] bool block_has_mineable_tool_tag(
    const voxel::BlockRegistry& registry,
    voxel::BlockID block_id) {
  const voxel::BlockRecord* block = registry.block(block_id);
  if (block == nullptr) {
    return false;
  }
  return std::any_of(block->tags.begin(), block->tags.end(), [](const std::string& tag) {
    return std::string_view{tag}.starts_with("arvane:mineable/");
  });
}

[[nodiscard]] bool held_item_matches_block_tool(
    const server::BlockBreakBehaviorQuery& query) {
  if (!block_has_mineable_tool_tag(query.block_registry, query.target.block_id)) {
    return true;
  }

  const std::optional<player::ItemStack>& held =
      query.player_state.inventory.slot(query.player_state.selected_hotbar_slot);
  if (!held.has_value()) {
    return query.block_registry.has_tag(query.target.block_id, "arvane:mineable/hand");
  }

  const player::ItemRecord* item = query.item_registry.item(held->item);
  if (item == nullptr || item->tool_category.empty()) {
    return query.block_registry.has_tag(query.target.block_id, "arvane:mineable/hand");
  }

  return query.block_registry.has_tag(
      query.target.block_id,
      "arvane:mineable/" + item->tool_category);
}

[[nodiscard]] server::BlockBreakBehaviorEvaluation mineable_tags_behavior(
    const server::BlockBreakBehaviorQuery& query) {
  server::BlockBreakBehaviorEvaluation result;
  if (!block_has_mineable_tool_tag(query.block_registry, query.target.block_id)) {
    return result;
  }

  const std::optional<player::ItemStack>& held =
      query.player_state.inventory.slot(query.player_state.selected_hotbar_slot);
  result.held_item_suitable = held_item_matches_block_tool(query);
  result.drop_configured_item =
      query.target.properties.drops_with_wrong_tool || result.held_item_suitable;

  if (!held.has_value()) {
    result.speed_multiplier = query.config.hand_speed_multiplier;
    return result;
  }

  const player::ItemRecord* item = query.item_registry.item(held->item);
  if (item == nullptr || item->tool_category.empty()) {
    result.speed_multiplier = query.config.hand_speed_multiplier;
    return result;
  }

  result.speed_multiplier = result.held_item_suitable
                                ? item->tool_break_speed_multiplier
                                : query.config.wrong_tool_speed_multiplier;
  return result;
}

} // namespace

void register_base_block_break_behaviors(server::BlockBreakBehaviorRegistry& registry) {
  auto result = registry.register_handler(
      std::string{ids::MineableTagsBlockBreakBehaviorID},
      mineable_tags_behavior);
  assert(result && "mineable-tags block break behavior registration failed");
  (void)result;
}

} // namespace arv::base
