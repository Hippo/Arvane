#include "arv/base/base_module.hpp"

#include <iterator>

namespace arv::base {

void register_base_module(BaseModuleRegistry& registry) {
  if (registry.game_modes != nullptr) {
    register_base_game_modes(*registry.game_modes);
  }
  if (registry.block_behaviors != nullptr) {
    register_base_block_behaviors(*registry.block_behaviors);
  }
  if (registry.item_behaviors != nullptr) {
    register_base_item_behaviors(*registry.item_behaviors);
  }
  if (registry.entity_goals != nullptr) {
    register_base_entity_goals(*registry.entity_goals);
  }
  if (registry.entity_definitions != nullptr) {
    std::vector<entity::EntityDefinition> definitions = base_entity_definitions();
    registry.entity_definitions->insert(
        registry.entity_definitions->end(),
        std::make_move_iterator(definitions.begin()),
        std::make_move_iterator(definitions.end()));
  }
}

} // namespace arv::base
