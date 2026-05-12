#include "arv/base/base_module.hpp"

#include <iterator>

namespace arv::base {

void register_base_module(BaseModuleRegistry& registry) {
  if (registry.game_modes != nullptr) {
    register_base_game_modes(*registry.game_modes);
  }
  if (registry.entity_behaviors != nullptr) {
    register_base_entity_behaviors(*registry.entity_behaviors);
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
