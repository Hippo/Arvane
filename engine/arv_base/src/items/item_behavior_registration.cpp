#include "arv/base/base_module.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/player/item.hpp"

#include <cassert>
#include <string>
#include <string_view>

namespace arv::base {
namespace {

void register_item_behavior(
    player::ItemBehaviorRegistry& registry,
    std::string_view id,
    const char* assertion_message) {
  auto result = registry.register_handler(std::string{id});
  assert(result && assertion_message);
  (void)result;
}

} // namespace

void register_base_item_behaviors(player::ItemBehaviorRegistry& registry) {
  register_item_behavior(
      registry,
      ids::BlockItemBehaviorID,
      "base block item behavior registration failed");
  register_item_behavior(
      registry,
      ids::ToolItemBehaviorID,
      "base tool item behavior registration failed");
  register_item_behavior(
      registry,
      ids::EmptyFluidContainerItemBehaviorID,
      "base empty fluid container item behavior registration failed");
  register_item_behavior(
      registry,
      ids::FilledFluidContainerItemBehaviorID,
      "base filled fluid container item behavior registration failed");
}

} // namespace arv::base
