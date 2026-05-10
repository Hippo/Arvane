#include "arv/entity/components.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace arv::entity {
namespace {

using json = nlohmann::json;

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
  if (!source.animation_asset.empty()) {
    target.animation_asset = std::move(source.animation_asset);
  }
  if (!source.components.component_ids.empty()) {
    target.components = std::move(source.components);
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

[[nodiscard]] bool is_builtin_goal_type(std::string_view type) noexcept {
  return type == "arvane:idle" ||
         type == "arvane:wander" ||
         type == "arvane:float_in_fluid";
}

[[nodiscard]] bool has_animation_asset_kind(std::string_view animation) noexcept {
  const usize separator = animation.find(':');
  if (separator == std::string_view::npos) {
    return true;
  }
  const std::string_view path = animation.substr(separator + 1U);
  return path.rfind("animation/", 0U) == 0U && path.size() > std::string_view{"animation/"}.size();
}

[[nodiscard]] Expected<Vec3f, EntityRegistryError> read_vec3(
    const json& node,
    std::string_view field) {
  if (!node.is_array() || node.size() != 3U) {
    return Expected<Vec3f, EntityRegistryError>::failure(
        {std::string{field} + " must be a 3-element array"});
  }
  return Expected<Vec3f, EntityRegistryError>::success({
      node.at(0).get<f32>(),
      node.at(1).get<f32>(),
      node.at(2).get<f32>()});
}

[[nodiscard]] Expected<AuthoredGoalDefinition, EntityRegistryError> read_goal_definition(
    const json& goal_json) {
  if (!goal_json.is_object()) {
    return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(
        {"brain goal entry must be an object"});
  }
  const auto type = goal_json.find("type");
  if (type == goal_json.end() || !type->is_string()) {
    return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(
        {"brain goal is missing string field type"});
  }

  AuthoredGoalDefinition goal;
  goal.type = type->get<std::string>();
  if (!is_builtin_goal_type(goal.type)) {
    return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(
        {"unknown behavior goal ID: " + goal.type});
  }
  goal.priority = goal_json.value("priority", 0);
  goal.radius = goal_json.value("radius", 0.0F);
  goal.speed_multiplier = goal_json.value("speed_multiplier", 1.0F);
  goal.duration_ticks = goal_json.value("duration_ticks", 60U);
  goal.cooldown_ticks = goal_json.value("cooldown_ticks", 0U);
  goal.range = goal_json.value("range", 0.0F);
  goal.target = goal_json.value("target", std::string{});
  goal.animation = goal_json.value("animation", std::string{});
  return Expected<AuthoredGoalDefinition, EntityRegistryError>::success(std::move(goal));
}

[[nodiscard]] Expected<EntityComponentSet, EntityRegistryError> read_components(
    const json& components_json) {
  if (!components_json.is_object()) {
    return Expected<EntityComponentSet, EntityRegistryError>::failure(
        {"entity components must be an object"});
  }

  EntityComponentSet components;
  for (auto iter = components_json.begin(); iter != components_json.end(); ++iter) {
    const std::string& component_id = iter.key();
    const json& data = iter.value();
    if (!data.is_object()) {
      return Expected<EntityComponentSet, EntityRegistryError>::failure(
          {"component " + component_id + " data must be an object"});
    }

    components.component_ids.push_back(component_id);
    if (component_id == TransformComponentID) {
      components.transform = true;
    } else if (component_id == VelocityComponentID) {
      components.velocity = true;
    } else if (component_id == CollisionComponentID) {
      AuthoredCollisionComponent collision;
      if (const auto box = data.find("box"); box != data.end()) {
        auto parsed_box = read_vec3(*box, "collision box");
        if (!parsed_box) {
          return Expected<EntityComponentSet, EntityRegistryError>::failure(parsed_box.error());
        }
        collision.box = parsed_box.value();
      }
      components.collision = collision;
    } else if (component_id == MovementComponentID) {
      AuthoredMovementComponent movement;
      movement.walk_speed = data.value("walk_speed", 0.0F);
      components.movement = movement;
    } else if (component_id == HealthComponentID) {
      AuthoredHealthComponent health;
      const auto max = data.find("max");
      if (max != data.end()) {
        if (!max->is_number()) {
          return Expected<EntityComponentSet, EntityRegistryError>::failure(
              {"health max must be a number"});
        }
        health.max = max->get<f32>();
      }
      const auto current = data.find("current");
      if (current != data.end()) {
        if (!current->is_number()) {
          return Expected<EntityComponentSet, EntityRegistryError>::failure(
              {"health current must be a number"});
        }
        health.current = current->get<f32>();
      }
      const auto invulnerable = data.find("invulnerable");
      if (invulnerable != data.end()) {
        if (!invulnerable->is_boolean()) {
          return Expected<EntityComponentSet, EntityRegistryError>::failure(
              {"health invulnerable must be a boolean"});
        }
        health.invulnerable = invulnerable->get<bool>();
      }
      components.health = health;
    } else if (component_id == KnockbackResistanceComponentID) {
      AuthoredKnockbackResistanceComponent resistance;
      const auto amount = data.find("resistance");
      if (amount != data.end()) {
        if (!amount->is_number()) {
          return Expected<EntityComponentSet, EntityRegistryError>::failure(
              {"knockback resistance must be a number"});
        }
        resistance.resistance = amount->get<f32>();
      }
      components.knockback_resistance = resistance;
    } else if (component_id == BrainComponentID) {
      AuthoredBrainComponent brain;
      brain.behavior = data.value("behavior", std::string{});
      brain.tick_interval = std::max<u32>(
          1U,
          data.value("tick_rate", data.value("tick_interval", 1U)));
      if (const auto goals = data.find("goals"); goals != data.end()) {
        if (!goals->is_array()) {
          return Expected<EntityComponentSet, EntityRegistryError>::failure(
              {"brain goals must be an array"});
        }
        brain.goals.reserve(goals->size());
        for (const json& goal_json : *goals) {
          auto goal = read_goal_definition(goal_json);
          if (!goal) {
            return Expected<EntityComponentSet, EntityRegistryError>::failure(goal.error());
          }
          brain.goals.push_back(std::move(goal.value()));
        }
      }
      components.brain = std::move(brain);
    } else if (component_id == AnimationComponentID) {
      AuthoredAnimationComponent animation;
      animation.initial = data.value("initial", std::string{});
      components.animation = std::move(animation);
    } else {
      return Expected<EntityComponentSet, EntityRegistryError>::failure(
          {"unknown entity component ID: " + component_id});
    }
  }

  return Expected<EntityComponentSet, EntityRegistryError>::success(std::move(components));
}

} // namespace

EntityRegistry::Result EntityRegistry::build(std::vector<EntityDefinition> definitions) {
  HashMap<std::string, usize> definition_names;
  for (usize index = 0; index < definitions.size(); ++index) {
    EntityDefinition& definition = definitions[index];
    if (definition.name.empty()) {
      return Result::failure({"entity definition has empty name"});
    }
    if (definition.components.brain.has_value()) {
      for (const AuthoredGoalDefinition& goal : definition.components.brain->goals) {
        if (!is_builtin_goal_type(goal.type)) {
          return Result::failure({"unknown behavior goal ID: " + goal.type});
        }
      }
    }
    definition.model_asset = normalize_entity_model_asset(definition.model_asset);
    if (!definition.animation_asset.empty()) {
      if (!has_animation_asset_kind(definition.animation_asset)) {
        return Result::failure(
            {"presentation.animation must reference arvane:animation/... for " + definition.name});
      }
      definition.animation_asset = normalize_entity_animation_asset(definition.animation_asset);
    }
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
    record.animation_asset = std::move(definition.animation_asset);
    record.components = std::move(definition.components);
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
  definition.model_asset = "arvane:model/entity/player";
  definition.animation_asset = "arvane:animation/player";
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

std::string normalize_entity_animation_asset(std::string_view animation) {
  if (animation.empty()) {
    return {};
  }
  if (animation.find(':') != std::string_view::npos) {
    return std::string{animation};
  }
  return "arvane:animation/" + std::string{animation};
}

Expected<std::vector<EntityDefinition>, EntityRegistryError> parse_entity_definitions_json(
    std::string_view text) {
  try {
    const json root = json::parse(text.begin(), text.end());
    const json* entities = &root;
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

    auto read_entity = [](const json& entity)
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
      if (const auto presentation = entity.find("presentation");
          presentation != entity.end()) {
        if (!presentation->is_object()) {
          return Expected<EntityDefinition, EntityRegistryError>::failure(
              {"entity presentation must be an object"});
        }
        if (const auto model = presentation->find("model"); model != presentation->end()) {
          if (!model->is_string()) {
            return Expected<EntityDefinition, EntityRegistryError>::failure(
                {"entity presentation.model must be a string"});
          }
          definition.model_asset = model->get<std::string>();
        }
        if (const auto animation = presentation->find("animation");
            animation != presentation->end()) {
          if (!animation->is_string()) {
            return Expected<EntityDefinition, EntityRegistryError>::failure(
                {"entity presentation.animation must be a string"});
          }
          definition.animation_asset = animation->get<std::string>();
          if (!definition.animation_asset.empty() &&
              !has_animation_asset_kind(definition.animation_asset)) {
            return Expected<EntityDefinition, EntityRegistryError>::failure(
                {"entity presentation.animation must reference arvane:animation/..."});
          }
        }
      }
      if (const auto iter = entity.find("model"); iter != entity.end()) {
        definition.model_asset = iter->get<std::string>();
      }
      if (const auto iter = entity.find("components"); iter != entity.end()) {
        auto components = read_components(*iter);
        if (!components) {
          return Expected<EntityDefinition, EntityRegistryError>::failure(components.error());
        }
        definition.components = std::move(components.value());
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
      for (const json& entity : *entities) {
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
