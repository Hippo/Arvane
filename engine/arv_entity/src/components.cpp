#include "arv/entity/components.hpp"

#include "arv/io/data_codecs.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

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

[[nodiscard]] bool has_animation_asset_kind(std::string_view animation) noexcept {
  const usize separator = animation.find(':');
  if (separator == std::string_view::npos) {
    return true;
  }
  const std::string_view path = animation.substr(separator + 1U);
  return path.rfind("animation/", 0U) == 0U && path.size() > std::string_view{"animation/"}.size();
}

[[nodiscard]] Expected<Vec3f, EntityRegistryError> read_vec3(
    const io::DataValue& node,
    std::string_view field) {
  const io::DataValue::Array* array = node.array_if();
  if (array == nullptr || array->size() != 3U) {
    return Expected<Vec3f, EntityRegistryError>::failure(
        {std::string{field} + " must be a 3-element array"});
  }
  return Expected<Vec3f, EntityRegistryError>::success({
      static_cast<f32>((*array)[0].as_f64_or(0.0)),
      static_cast<f32>((*array)[1].as_f64_or(0.0)),
      static_cast<f32>((*array)[2].as_f64_or(0.0))});
}

[[nodiscard]] Expected<AuthoredGoalDefinition, EntityRegistryError> read_goal_definition(
    const io::DataValue& goal_json) {
  if (goal_json.kind() != io::DataKind::Object) {
    return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(
        {"brain goal entry must be an object"});
  }
  const io::DataValue* type = goal_json.get("type");
  if (type == nullptr || type->kind() != io::DataKind::String) {
    return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(
        {"brain goal is missing string field type"});
  }

  AuthoredGoalDefinition goal;
  goal.type = std::string{type->as_string_or({})};
  if (const io::DataValue* value = goal_json.get("priority"); value != nullptr) {
    goal.priority = static_cast<i32>(value->as_i64_or(goal.priority));
  }
  if (const io::DataValue* value = goal_json.get("radius"); value != nullptr) {
    goal.radius = static_cast<f32>(value->as_f64_or(goal.radius));
  }
  if (const io::DataValue* value = goal_json.get("speed_multiplier"); value != nullptr) {
    goal.speed_multiplier = static_cast<f32>(value->as_f64_or(goal.speed_multiplier));
  }
  if (const io::DataValue* value = goal_json.get("duration_ticks"); value != nullptr) {
    goal.duration_ticks = static_cast<u32>(value->as_u64_or(goal.duration_ticks));
  }
  if (const io::DataValue* value = goal_json.get("cooldown_ticks"); value != nullptr) {
    goal.cooldown_ticks = static_cast<u32>(value->as_u64_or(goal.cooldown_ticks));
  }
  if (const io::DataValue* value = goal_json.get("range"); value != nullptr) {
    goal.range = static_cast<f32>(value->as_f64_or(goal.range));
  }
  if (const io::DataValue* value = goal_json.get("target");
      value != nullptr && value->kind() == io::DataKind::String) {
    goal.target = std::string{value->as_string_or({})};
  }
  if (const io::DataValue* value = goal_json.get("animation");
      value != nullptr && value->kind() == io::DataKind::String) {
    goal.animation = std::string{value->as_string_or({})};
  }
  return Expected<AuthoredGoalDefinition, EntityRegistryError>::success(std::move(goal));
}

[[nodiscard]] Expected<EntityComponentSet, EntityRegistryError> read_components(
    const io::DataValue& components_json) {
  const io::DataValue::Object* components_object = components_json.object_if();
  if (components_object == nullptr) {
    return Expected<EntityComponentSet, EntityRegistryError>::failure(
        {"entity components must be an object"});
  }

  EntityComponentSet components;
  for (const auto& [component_id, data] : *components_object) {
    if (data.kind() != io::DataKind::Object) {
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
      if (const io::DataValue* box = data.get("box"); box != nullptr) {
        auto parsed_box = read_vec3(*box, "collision box");
        if (!parsed_box) {
          return Expected<EntityComponentSet, EntityRegistryError>::failure(parsed_box.error());
        }
        collision.box = parsed_box.value();
      }
      components.collision = collision;
    } else if (component_id == MovementComponentID) {
      AuthoredMovementComponent movement;
      if (const io::DataValue* walk_speed = data.get("walk_speed"); walk_speed != nullptr) {
        movement.walk_speed = static_cast<f32>(walk_speed->as_f64_or(0.0));
      }
      components.movement = movement;
    } else if (component_id == HealthComponentID) {
      AuthoredHealthComponent health;
      const io::DataValue* max = data.get("max");
      if (max != nullptr) {
        if (max->kind() != io::DataKind::I64 && max->kind() != io::DataKind::U64 &&
            max->kind() != io::DataKind::F64) {
          return Expected<EntityComponentSet, EntityRegistryError>::failure(
              {"health max must be a number"});
        }
        health.max = static_cast<f32>(max->as_f64_or(health.max));
      }
      const io::DataValue* current = data.get("current");
      if (current != nullptr) {
        if (current->kind() != io::DataKind::I64 && current->kind() != io::DataKind::U64 &&
            current->kind() != io::DataKind::F64) {
          return Expected<EntityComponentSet, EntityRegistryError>::failure(
              {"health current must be a number"});
        }
        health.current = static_cast<f32>(current->as_f64_or(health.current.value_or(health.max)));
      }
      const io::DataValue* invulnerable = data.get("invulnerable");
      if (invulnerable != nullptr) {
        if (invulnerable->kind() != io::DataKind::Bool) {
          return Expected<EntityComponentSet, EntityRegistryError>::failure(
              {"health invulnerable must be a boolean"});
        }
        health.invulnerable = invulnerable->as_bool_or(false);
      }
      components.health = health;
    } else if (component_id == KnockbackResistanceComponentID) {
      AuthoredKnockbackResistanceComponent resistance;
      const io::DataValue* amount = data.get("resistance");
      if (amount != nullptr) {
        if (amount->kind() != io::DataKind::I64 && amount->kind() != io::DataKind::U64 &&
            amount->kind() != io::DataKind::F64) {
          return Expected<EntityComponentSet, EntityRegistryError>::failure(
              {"knockback resistance must be a number"});
        }
        resistance.resistance = static_cast<f32>(amount->as_f64_or(resistance.resistance));
      }
      components.knockback_resistance = resistance;
    } else if (component_id == BrainComponentID) {
      AuthoredBrainComponent brain;
      if (const io::DataValue* behavior = data.get("behavior");
          behavior != nullptr && behavior->kind() == io::DataKind::String) {
        brain.behavior = std::string{behavior->as_string_or({})};
      }
      u32 tick_rate = 1U;
      if (const io::DataValue* rate = data.get("tick_rate"); rate != nullptr) {
        tick_rate = static_cast<u32>(rate->as_u64_or(tick_rate));
      } else if (const io::DataValue* interval = data.get("tick_interval"); interval != nullptr) {
        tick_rate = static_cast<u32>(interval->as_u64_or(tick_rate));
      }
      brain.tick_interval = std::max<u32>(
          1U,
          tick_rate);
      if (const io::DataValue* goals = data.get("goals"); goals != nullptr) {
        const io::DataValue::Array* goal_array = goals->array_if();
        if (goal_array == nullptr) {
          return Expected<EntityComponentSet, EntityRegistryError>::failure(
              {"brain goals must be an array"});
        }
        brain.goals.reserve(goal_array->size());
        for (const io::DataValue& goal_json : *goal_array) {
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
      if (const io::DataValue* initial = data.get("initial");
          initial != nullptr && initial->kind() == io::DataKind::String) {
        animation.initial = std::string{initial->as_string_or({})};
      }
      components.animation = std::move(animation);
    } else {
      return Expected<EntityComponentSet, EntityRegistryError>::failure(
          {"unknown entity component ID: " + component_id});
    }
  }

  return Expected<EntityComponentSet, EntityRegistryError>::success(std::move(components));
}

} // namespace

EntityRegistry::Result EntityRegistry::build(
    std::vector<EntityDefinition> definitions,
    EntityRegistryBuildOptions options) {
  HashMap<std::string, usize> definition_names;
  for (usize index = 0; index < definitions.size(); ++index) {
    EntityDefinition& definition = definitions[index];
    if (definition.name.empty()) {
      return Result::failure({"entity definition has empty name"});
    }
    if (options.goal_exists && definition.components.brain.has_value()) {
      for (const AuthoredGoalDefinition& goal : definition.components.brain->goals) {
        if (!options.goal_exists(goal.type)) {
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

void EntityRegistry::set_display_name(EntityTypeID id, std::string display_name) {
  if (id.value == 0U || id.value >= types_.size()) {
    return;
  }
  types_[id.value].display_name = std::move(display_name);
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
  auto decoded = io::decode_json_value(text);
  if (!decoded) {
    return Expected<std::vector<EntityDefinition>, EntityRegistryError>::failure(
        {"failed to parse entity definitions JSON: " + decoded.error().describe()});
  }
  const io::DataValue& root = decoded.value().root;
  const io::DataValue* entities = &root;
  if (root.kind() == io::DataKind::Object) {
    if (const io::DataValue* found = root.get("entities"); found != nullptr) {
      entities = found;
    } else if (root.get("name") != nullptr) {
      entities = nullptr;
    } else {
      return Expected<std::vector<EntityDefinition>, EntityRegistryError>::failure(
          {"entity definition JSON object must contain entities array or a single entity object"});
    }
  }
  if (entities != nullptr && entities->kind() != io::DataKind::Array) {
    return Expected<std::vector<EntityDefinition>, EntityRegistryError>::failure(
        {"entity definitions must be a JSON array"});
  }

  auto read_entity = [](const io::DataValue& entity)
      -> Expected<EntityDefinition, EntityRegistryError> {
      if (entity.kind() != io::DataKind::Object) {
        return Expected<EntityDefinition, EntityRegistryError>::failure(
            {"entity definition entry must be an object"});
      }
      const io::DataValue* name_iter = entity.get("name");
      if (name_iter == nullptr || name_iter->kind() != io::DataKind::String) {
        return Expected<EntityDefinition, EntityRegistryError>::failure(
            {"entity definition is missing string field name"});
      }

      EntityDefinition definition;
      definition.name = std::string{name_iter->as_string_or({})};
      if (const io::DataValue* iter = entity.get("display_name");
          iter != nullptr && iter->kind() == io::DataKind::String) {
        definition.display_name = std::string{iter->as_string_or({})};
      }
      if (const io::DataValue* iter = entity.get("category");
          iter != nullptr && iter->kind() == io::DataKind::String) {
        definition.category = std::string{iter->as_string_or({})};
      }
      if (const io::DataValue* presentation = entity.get("presentation");
          presentation != nullptr) {
        if (presentation->kind() != io::DataKind::Object) {
          return Expected<EntityDefinition, EntityRegistryError>::failure(
              {"entity presentation must be an object"});
        }
        if (const io::DataValue* model = presentation->get("model"); model != nullptr) {
          if (model->kind() != io::DataKind::String) {
            return Expected<EntityDefinition, EntityRegistryError>::failure(
                {"entity presentation.model must be a string"});
          }
          definition.model_asset = std::string{model->as_string_or({})};
        }
        if (const io::DataValue* animation = presentation->get("animation");
            animation != nullptr) {
          if (animation->kind() != io::DataKind::String) {
            return Expected<EntityDefinition, EntityRegistryError>::failure(
                {"entity presentation.animation must be a string"});
          }
          definition.animation_asset = std::string{animation->as_string_or({})};
          if (!definition.animation_asset.empty() &&
              !has_animation_asset_kind(definition.animation_asset)) {
            return Expected<EntityDefinition, EntityRegistryError>::failure(
                {"entity presentation.animation must reference arvane:animation/..."});
          }
        }
      }
      if (const io::DataValue* iter = entity.get("model");
          iter != nullptr && iter->kind() == io::DataKind::String) {
        definition.model_asset = std::string{iter->as_string_or({})};
      }
      if (const io::DataValue* iter = entity.get("components"); iter != nullptr) {
        auto components = read_components(*iter);
        if (!components) {
          return Expected<EntityDefinition, EntityRegistryError>::failure(components.error());
        }
        definition.components = std::move(components.value());
      }
      if (const io::DataValue* iter = entity.get("spawnable");
          iter != nullptr && iter->kind() == io::DataKind::Bool) {
        definition.spawnable = iter->as_bool_or(definition.spawnable);
      }
      if (const io::DataValue* iter = entity.get("persistent_by_default");
          iter != nullptr && iter->kind() == io::DataKind::Bool) {
        definition.persistent_by_default = iter->as_bool_or(definition.persistent_by_default);
      }
      if (const io::DataValue* iter = entity.get("replicated_by_default");
          iter != nullptr && iter->kind() == io::DataKind::Bool) {
        definition.replicated_by_default = iter->as_bool_or(definition.replicated_by_default);
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
      const io::DataValue::Array* entity_array = entities->array_if();
      definitions.reserve(entity_array == nullptr ? 0U : entity_array->size());
      for (const io::DataValue& entity : *entity_array) {
        auto definition = read_entity(entity);
        if (!definition) {
          return Expected<std::vector<EntityDefinition>, EntityRegistryError>::failure(definition.error());
        }
        definitions.push_back(std::move(definition.value()));
      }
    }

    return Expected<std::vector<EntityDefinition>, EntityRegistryError>::success(std::move(definitions));
}

} // namespace arv::entity
