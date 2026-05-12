#include "arv/base/base_module.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/world/entity_goal_registry.hpp"

#include <cassert>
#include <utility>

namespace arv::base {
namespace {

void merge_pack_entity_definition(
    entity::EntityDefinition& target,
    entity::EntityDefinition source) {
  if (!source.display_name.empty()) {
    target.display_name = std::move(source.display_name);
  }
  if (!source.category.empty()) {
    target.category = std::move(source.category);
  }
  if (!source.model_asset.empty()) {
    target.model_asset = std::move(source.model_asset);
  }
  if (!source.animation_asset.empty()) {
    target.animation_asset = std::move(source.animation_asset);
  }
  if (!source.components.component_ids.empty()) {
    target.components = std::move(source.components);
  }
  target.spawnable = source.spawnable;
  target.persistent_by_default = source.persistent_by_default;
  target.replicated_by_default = source.replicated_by_default;

  if (target.name == ids::PlayerEntityTypeName) {
    target.spawnable = false;
    target.persistent_by_default = true;
    target.replicated_by_default = true;
  }
}

} // namespace

std::vector<entity::EntityDefinition> base_entity_definitions() {
  entity::EntityDefinition player;
  player.name = std::string{ids::PlayerEntityTypeName};
  player.display_name = "Player";
  player.category = "builtin";
  player.model_asset = std::string{ids::PlayerModelAssetID};
  player.animation_asset = std::string{ids::PlayerAnimationAssetID};
  player.spawnable = false;
  player.persistent_by_default = true;
  player.replicated_by_default = true;
  return {std::move(player)};
}

entity::EntityRegistry::Result build_base_entity_registry(
    std::vector<entity::EntityDefinition> pack_definitions,
    const world::EntityGoalRegistry* goal_registry) {
  std::vector<entity::EntityDefinition> definitions = base_entity_definitions();

  HashMap<std::string, usize> definition_indices;
  definition_indices.reserve(definitions.size() + pack_definitions.size());
  for (usize index = 0; index < definitions.size(); ++index) {
    definition_indices.emplace(definitions[index].name, index);
  }

  HashSet<std::string> pack_names;
  for (entity::EntityDefinition& definition : pack_definitions) {
    if (definition.name.empty()) {
      return entity::EntityRegistry::Result::failure(
          {"entity definition has empty name"});
    }
    if (!pack_names.insert(definition.name).second) {
      return entity::EntityRegistry::Result::failure(
          {"duplicate explicit entity definition: " + definition.name});
    }
    if (auto existing = definition_indices.find(definition.name);
        existing == definition_indices.end()) {
      definition_indices.emplace(definition.name, definitions.size());
      definitions.push_back(std::move(definition));
    } else {
      merge_pack_entity_definition(
          definitions[existing->second],
          std::move(definition));
    }
  }

  world::EntityGoalRegistry fallback_goal_registry;
  if (goal_registry == nullptr) {
    register_base_entity_goals(fallback_goal_registry);
    goal_registry = &fallback_goal_registry;
  }
  const auto goal_exists = [goal_registry](std::string_view id) {
    return goal_registry->contains(id);
  };
  return entity::EntityRegistry::build(
      std::move(definitions),
      {.goal_exists = FunctionRef<bool(std::string_view)>{goal_exists}});
}

} // namespace arv::base
