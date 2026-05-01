#include "arv/entity/components.hpp"

#include <algorithm>
#include <utility>

#include <nlohmann/json.hpp>

namespace arv::entity {
namespace {

bool entity_name_less(const EntityDefinition& lhs, const EntityDefinition& rhs) {
  if (lhs.name == PlayerEntityTypeName) {
    return true;
  }
  if (rhs.name == PlayerEntityTypeName) {
    return false;
  }
  return lhs.name < rhs.name;
}

void merge_explicit_definition(EntityDefinition& target, EntityDefinition source) {
  if (!source.display_name.empty()) {
    target.display_name = std::move(source.display_name);
  }
  if (!source.category.empty()) {
    target.category = std::move(source.category);
  }
  if (!source.model_asset.empty()) {
    target.model_asset = std::move(source.model_asset);
  }
  target.spawnable = source.spawnable;
  target.persistent_by_default = source.persistent_by_default;
  target.replicated_by_default = source.replicated_by_default;
  if (target.name == PlayerEntityTypeName) {
    target.spawnable = false;
    target.persistent_by_default = true;
    target.replicated_by_default = true;
  }
}

} // namespace

EntityRegistry::Result EntityRegistry::build(std::vector<EntityDefinition> definitions) {
  HashMap<std::string, usize> definition_names;
  for (usize index = 0; index < definitions.size(); ++index) {
    EntityDefinition& definition = definitions[index];
    if (definition.name.empty()) {
      return Result::failure({"entity definition has empty name"});
    }
    definition.model_asset = normalize_entity_model_asset(definition.model_asset);
    if (!definition_names.emplace(definition.name, index).second) {
      return Result::failure({"duplicate entity definition: " + definition.name});
    }
  }

  std::sort(definitions.begin(), definitions.end(), entity_name_less);

  EntityRegistry registry;
  registry.types_.clear();
  registry.types_.reserve(definitions.size() + 1U);
  registry.types_.push_back({});

  u32 next_type_id = 1;
  for (EntityDefinition& definition : definitions) {
    EntityTypeRecord record;
    record.id = EntityTypeID{next_type_id++};
    record.name = std::move(definition.name);
    record.display_name =
        definition.display_name.empty() ? record.name : std::move(definition.display_name);
    record.category = std::move(definition.category);
    record.model_asset = std::move(definition.model_asset);
    record.spawnable = definition.spawnable;
    record.persistent_by_default = definition.persistent_by_default;
    record.replicated_by_default = definition.replicated_by_default;

    if (record.name == PlayerEntityTypeName && record.id != PlayerEntityTypeID) {
      return Result::failure({"entity registry failed to preserve arvane:player at type ID 1"});
    }
    if (!registry.name_to_id_.emplace(record.name, record.id).second) {
      return Result::failure({"duplicate entity name during registry build: " + record.name});
    }
    registry.types_.push_back(std::move(record));
  }

  return Result::success(std::move(registry));
}

EntityRegistry::Result EntityRegistry::build_with_builtins(
    std::vector<EntityDefinition> explicit_definitions) {
  std::vector<EntityDefinition> definitions;
  definitions.reserve(explicit_definitions.size() + 1U);
  definitions.push_back(make_player_entity_definition());

  HashMap<std::string, usize> definition_indices;
  definition_indices.emplace(definitions.front().name, 0U);
  HashSet<std::string> explicit_names;
  for (EntityDefinition& definition : explicit_definitions) {
    if (definition.name.empty()) {
      return Result::failure({"entity definition has empty name"});
    }
    if (!explicit_names.insert(definition.name).second) {
      return Result::failure({"duplicate explicit entity definition: " + definition.name});
    }
    if (auto existing = definition_indices.find(definition.name);
        existing == definition_indices.end()) {
      definition_indices.emplace(definition.name, definitions.size());
      definitions.push_back(std::move(definition));
    } else {
      merge_explicit_definition(definitions[existing->second], std::move(definition));
    }
  }

  return build(std::move(definitions));
}

const EntityTypeRecord* EntityRegistry::type(EntityTypeID id) const noexcept {
  if (id.value == 0U || id.value >= types_.size()) {
    return nullptr;
  }
  return &types_[id.value];
}

std::optional<EntityTypeID> EntityRegistry::find_type(std::string_view name) const {
  const auto found = name_to_id_.find(std::string{name});
  return found == name_to_id_.end() ? std::nullopt : std::optional<EntityTypeID>{found->second};
}

EntityDefinition make_player_entity_definition() {
  EntityDefinition definition;
  definition.name = std::string{PlayerEntityTypeName};
  definition.display_name = "Player";
  definition.category = "builtin";
  definition.spawnable = false;
  definition.persistent_by_default = true;
  definition.replicated_by_default = true;
  return definition;
}

std::string normalize_entity_model_asset(std::string_view model) {
  if (model.empty()) {
    return {};
  }
  if (model.find(':') != std::string_view::npos) {
    return std::string{model};
  }
  return "arvane:model/entity/" + std::string{model};
}

Expected<std::vector<EntityDefinition>, EntityRegistryError> parse_entity_definitions_json(
    std::string_view text) {
  try {
    const nlohmann::json root = nlohmann::json::parse(text.begin(), text.end());
    const nlohmann::json* entities = &root;
    if (root.is_object()) {
      const auto found = root.find("entities");
      if (found != root.end()) {
        entities = &(*found);
      } else if (root.contains("name")) {
        entities = nullptr;
      } else {
        return Expected<std::vector<EntityDefinition>, EntityRegistryError>::failure(
            {"entity definition JSON object must contain entities array or a single entity object"});
      }
    }
    if (entities != nullptr && !entities->is_array()) {
      return Expected<std::vector<EntityDefinition>, EntityRegistryError>::failure(
          {"entity definitions must be a JSON array"});
    }

    auto read_entity = [](const nlohmann::json& entity)
        -> Expected<EntityDefinition, EntityRegistryError> {
      if (!entity.is_object()) {
        return Expected<EntityDefinition, EntityRegistryError>::failure(
            {"entity definition entry must be an object"});
      }
      const auto name_iter = entity.find("name");
      if (name_iter == entity.end() || !name_iter->is_string()) {
        return Expected<EntityDefinition, EntityRegistryError>::failure(
            {"entity definition is missing string field name"});
      }

      EntityDefinition definition;
      definition.name = name_iter->get<std::string>();
      if (const auto iter = entity.find("display_name"); iter != entity.end()) {
        definition.display_name = iter->get<std::string>();
      }
      if (const auto iter = entity.find("category"); iter != entity.end()) {
        definition.category = iter->get<std::string>();
      }
      if (const auto iter = entity.find("model"); iter != entity.end()) {
        definition.model_asset = iter->get<std::string>();
      }
      if (const auto iter = entity.find("spawnable"); iter != entity.end()) {
        definition.spawnable = iter->get<bool>();
      }
      if (const auto iter = entity.find("persistent_by_default"); iter != entity.end()) {
        definition.persistent_by_default = iter->get<bool>();
      }
      if (const auto iter = entity.find("replicated_by_default"); iter != entity.end()) {
        definition.replicated_by_default = iter->get<bool>();
      }
      return Expected<EntityDefinition, EntityRegistryError>::success(std::move(definition));
    };

    std::vector<EntityDefinition> definitions;
    if (entities == nullptr) {
      auto definition = read_entity(root);
      if (!definition) {
        return Expected<std::vector<EntityDefinition>, EntityRegistryError>::failure(definition.error());
      }
      definitions.push_back(std::move(definition.value()));
    } else {
      definitions.reserve(entities->size());
      for (const nlohmann::json& entity : *entities) {
        auto definition = read_entity(entity);
        if (!definition) {
          return Expected<std::vector<EntityDefinition>, EntityRegistryError>::failure(definition.error());
        }
        definitions.push_back(std::move(definition.value()));
      }
    }

    return Expected<std::vector<EntityDefinition>, EntityRegistryError>::success(std::move(definitions));
  } catch (const nlohmann::json::exception& ex) {
    return Expected<std::vector<EntityDefinition>, EntityRegistryError>::failure(
        {"failed to parse entity definitions JSON: " + std::string{ex.what()}});
  }
}

} // namespace arv::entity
