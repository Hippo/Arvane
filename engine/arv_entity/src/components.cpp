#include "arv/entity/components.hpp"

#include "arv/io/data_codecs.hpp"

#include <algorithm>
#include <cmath>
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
  return Expected<Vec3f, EntityRegistryError>::success({static_cast<f32>((*array)[0].as_f64_or(0.0)),
                                                        static_cast<f32>((*array)[1].as_f64_or(0.0)),
                                                        static_cast<f32>((*array)[2].as_f64_or(0.0))});
}

[[nodiscard]] bool is_number(const io::DataValue& value) noexcept {
  return value.kind() == io::DataKind::I64 || value.kind() == io::DataKind::U64 ||
         value.kind() == io::DataKind::F64;
}

[[nodiscard]] Expected<f32, EntityRegistryError> read_required_f32(
    const io::DataValue& object,
    std::string_view field,
    std::string_view path) {
  const io::DataValue* value = object.get(field);
  if (value == nullptr || !is_number(*value)) {
    return Expected<f32, EntityRegistryError>::failure(
        {std::string{path} + "." + std::string{field} + " must be a number"});
  }
  const f32 number = static_cast<f32>(value->as_f64_or(0.0));
  if (!std::isfinite(number)) {
    return Expected<f32, EntityRegistryError>::failure(
        {std::string{path} + "." + std::string{field} + " must be finite"});
  }
  return Expected<f32, EntityRegistryError>::success(number);
}

[[nodiscard]] Expected<u32, EntityRegistryError> read_optional_u32(
    const io::DataValue& object,
    std::string_view field,
    std::string_view path,
    u32 fallback) {
  const io::DataValue* value = object.get(field);
  if (value == nullptr) {
    return Expected<u32, EntityRegistryError>::success(fallback);
  }
  if (value->kind() != io::DataKind::I64 && value->kind() != io::DataKind::U64) {
    return Expected<u32, EntityRegistryError>::failure(
        {std::string{path} + "." + std::string{field} + " must be an integer"});
  }
  return Expected<u32, EntityRegistryError>::success(
      static_cast<u32>(value->as_u64_or(fallback)));
}

[[nodiscard]] Expected<bool, EntityRegistryError> read_optional_bool(
    const io::DataValue& object,
    std::string_view field,
    std::string_view path,
    bool fallback) {
  const io::DataValue* value = object.get(field);
  if (value == nullptr) {
    return Expected<bool, EntityRegistryError>::success(fallback);
  }
  if (value->kind() != io::DataKind::Bool) {
    return Expected<bool, EntityRegistryError>::failure(
        {std::string{path} + "." + std::string{field} + " must be a boolean"});
  }
  return Expected<bool, EntityRegistryError>::success(value->as_bool_or(fallback));
}

[[nodiscard]] Expected<bool, EntityRegistryError> read_required_bool(
    const io::DataValue& object,
    std::string_view field,
    std::string_view path) {
  const io::DataValue* value = object.get(field);
  if (value == nullptr || value->kind() != io::DataKind::Bool) {
    return Expected<bool, EntityRegistryError>::failure(
        {std::string{path} + "." + std::string{field} + " must be a boolean"});
  }
  return Expected<bool, EntityRegistryError>::success(value->as_bool_or(false));
}

[[nodiscard]] Expected<std::string, EntityRegistryError> read_required_string(
    const io::DataValue& object,
    std::string_view field,
    std::string_view path) {
  const io::DataValue* value = object.get(field);
  if (value == nullptr || value->kind() != io::DataKind::String) {
    return Expected<std::string, EntityRegistryError>::failure(
        {std::string{path} + "." + std::string{field} + " must be a string"});
  }
  std::string text{value->as_string_or({})};
  if (text.empty()) {
    return Expected<std::string, EntityRegistryError>::failure(
        {std::string{path} + "." + std::string{field} + " must not be empty"});
  }
  return Expected<std::string, EntityRegistryError>::success(std::move(text));
}

[[nodiscard]] Expected<Quatf, EntityRegistryError> read_quat(
    const io::DataValue& node,
    std::string_view field) {
  const io::DataValue::Array* array = node.array_if();
  if (array == nullptr || array->size() != 4U) {
    return Expected<Quatf, EntityRegistryError>::failure(
        {std::string{field} + " must be a 4-element array"});
  }
  Quatf value{
      static_cast<f32>((*array)[0].as_f64_or(0.0)),
      static_cast<f32>((*array)[1].as_f64_or(0.0)),
      static_cast<f32>((*array)[2].as_f64_or(0.0)),
      static_cast<f32>((*array)[3].as_f64_or(1.0))};
  if (!std::isfinite(value.x) || !std::isfinite(value.y) ||
      !std::isfinite(value.z) || !std::isfinite(value.w)) {
    return Expected<Quatf, EntityRegistryError>::failure(
        {std::string{field} + " must contain finite numbers"});
  }
  return Expected<Quatf, EntityRegistryError>::success(normalize_quat(value));
}

[[nodiscard]] Expected<SeatRole, EntityRegistryError> seat_role_from_string(
    std::string_view value,
    std::string_view path) {
  if (value == "driver") {
    return Expected<SeatRole, EntityRegistryError>::success(SeatRole::Driver);
  }
  if (value == "passenger") {
    return Expected<SeatRole, EntityRegistryError>::success(SeatRole::Passenger);
  }
  return Expected<SeatRole, EntityRegistryError>::failure(
      {std::string{path} + " must be driver or passenger"});
}

[[nodiscard]] Expected<CameraMode, EntityRegistryError> camera_mode_from_string(
    std::string_view value,
    std::string_view path) {
  if (value == "mounted_third_person") {
    return Expected<CameraMode, EntityRegistryError>::success(CameraMode::MountedThirdPerson);
  }
  if (value == "third_person_follow") {
    return Expected<CameraMode, EntityRegistryError>::success(CameraMode::ThirdPersonFollow);
  }
  return Expected<CameraMode, EntityRegistryError>::failure(
      {std::string{path} + " has unknown camera mode: " + std::string{value}});
}

[[nodiscard]] Expected<VehicleKind, EntityRegistryError> vehicle_kind_from_string(
    std::string_view value,
    std::string_view path) {
  if (value == "mount") {
    return Expected<VehicleKind, EntityRegistryError>::success(VehicleKind::Mount);
  }
  if (value == "vehicle") {
    return Expected<VehicleKind, EntityRegistryError>::success(VehicleKind::Vehicle);
  }
  return Expected<VehicleKind, EntityRegistryError>::failure(
      {std::string{path} + " must be mount or vehicle"});
}

[[nodiscard]] Expected<VehiclePhysicsKind, EntityRegistryError> vehicle_physics_from_string(
    std::string_view value,
    std::string_view path) {
  if (value == "living_mount") {
    return Expected<VehiclePhysicsKind, EntityRegistryError>::success(
        VehiclePhysicsKind::LivingMount);
  }
  if (value == "surface_water_vehicle") {
    return Expected<VehiclePhysicsKind, EntityRegistryError>::success(
        VehiclePhysicsKind::SurfaceWaterVehicle);
  }
  if (value == "ground_vehicle") {
    return Expected<VehiclePhysicsKind, EntityRegistryError>::success(
        VehiclePhysicsKind::GroundVehicle);
  }
  return Expected<VehiclePhysicsKind, EntityRegistryError>::failure(
      {std::string{path} + " has unknown physics kind: " + std::string{value}});
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
    } else if (component_id == RideableComponentID) {
      AuthoredRideableComponent rideable;
      auto definition = read_required_string(data, "definition", component_id);
      if (!definition) {
        return Expected<EntityComponentSet, EntityRegistryError>::failure(definition.error());
      }
      rideable.definition = std::move(definition.value());
      components.rideable = std::move(rideable);
    } else if (component_id == VehiclePhysicsComponentID) {
      AuthoredVehiclePhysicsComponent vehicle;
      auto definition = read_required_string(data, "definition", component_id);
      if (!definition) {
        return Expected<EntityComponentSet, EntityRegistryError>::failure(definition.error());
      }
      vehicle.definition = std::move(definition.value());
      components.vehicle_physics = std::move(vehicle);
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

VehicleDefinitionRegistry::Result VehicleDefinitionRegistry::build(
    std::vector<VehicleDefinition> definitions) {
  std::sort(
      definitions.begin(),
      definitions.end(),
      [](const VehicleDefinition& lhs, const VehicleDefinition& rhs) {
        return lhs.name < rhs.name;
      });

  VehicleDefinitionRegistry registry;
  registry.definitions_.reserve(definitions.size());
  for (VehicleDefinition& definition : definitions) {
    if (definition.name.empty()) {
      return Result::failure({"vehicle definition has empty name"});
    }
    if (definition.collision_size.x <= 0.0F || definition.collision_size.y <= 0.0F ||
        definition.collision_size.z <= 0.0F || !finite(definition.collision_size)) {
      return Result::failure({"vehicle " + definition.name + " has invalid collision size"});
    }
    if (definition.seats.empty()) {
      return Result::failure({"vehicle " + definition.name + " must define at least one seat"});
    }

    HashSet<std::string> seat_ids;
    bool has_controlling_seat = false;
    for (const VehicleSeatDefinition& seat : definition.seats) {
      if (seat.id.empty()) {
        return Result::failure({"vehicle " + definition.name + " has an empty seat id"});
      }
      if (!seat_ids.insert(seat.id).second) {
        return Result::failure(
            {"vehicle " + definition.name + " has duplicate seat id " + seat.id});
      }
      has_controlling_seat = has_controlling_seat || seat.controls_vehicle;
    }
    if (!has_controlling_seat) {
      return Result::failure(
          {"vehicle " + definition.name + " must define a controlling seat"});
    }

    const usize index = registry.definitions_.size();
    if (!registry.name_to_index_.emplace(definition.name, index).second) {
      return Result::failure({"duplicate vehicle definition: " + definition.name});
    }
    registry.definitions_.push_back(std::move(definition));
  }

  return Result::success(std::move(registry));
}

const VehicleDefinition* VehicleDefinitionRegistry::find(std::string_view name) const noexcept {
  const auto found = name_to_index_.find(std::string{name});
  if (found == name_to_index_.end() || found->second >= definitions_.size()) {
    return nullptr;
  }
  return &definitions_[found->second];
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

Expected<std::vector<VehicleDefinition>, EntityRegistryError> parse_vehicle_definitions_json(
    std::string_view text) {
  auto decoded = io::decode_json_value(text);
  if (!decoded) {
    return Expected<std::vector<VehicleDefinition>, EntityRegistryError>::failure(
        {"failed to parse vehicle definitions JSON: " + decoded.error().describe()});
  }

  const io::DataValue& root = decoded.value().root;
  const io::DataValue* vehicles = &root;
  if (root.kind() == io::DataKind::Object) {
    if (const io::DataValue* found = root.get("vehicles"); found != nullptr) {
      vehicles = found;
    } else if (root.get("name") != nullptr) {
      vehicles = nullptr;
    } else {
      return Expected<std::vector<VehicleDefinition>, EntityRegistryError>::failure(
          {"vehicle definition JSON object must contain vehicles array or a single vehicle object"});
    }
  }
  if (vehicles != nullptr && vehicles->kind() != io::DataKind::Array) {
    return Expected<std::vector<VehicleDefinition>, EntityRegistryError>::failure(
        {"vehicle definitions must be a JSON array"});
  }

  auto read_authority = [](const io::DataValue& root, std::string_view path)
      -> Expected<VehicleAuthorityDefinition, EntityRegistryError> {
    if (root.kind() != io::DataKind::Object) {
      return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(
          {std::string{path} + " must be an object"});
    }
    VehicleAuthorityDefinition authority;
    auto max_speed = read_required_f32(root, "max_speed", path);
    auto max_acceleration = read_required_f32(root, "max_acceleration", path);
    auto max_deceleration = read_required_f32(root, "max_deceleration", path);
    auto max_angular_speed = read_required_f32(root, "max_angular_speed", path);
    auto max_vertical_speed = read_required_f32(root, "max_vertical_speed", path);
    auto requires_ground = read_required_bool(root, "requires_ground", path);
    auto requires_water = read_required_bool(root, "requires_water", path);
    auto min_water_support =
        read_optional_u32(root, "min_water_support_samples", path, 0U);
    if (!max_speed) {
      return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(max_speed.error());
    }
    if (!max_acceleration) {
      return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(max_acceleration.error());
    }
    if (!max_deceleration) {
      return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(max_deceleration.error());
    }
    if (!max_angular_speed) {
      return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(max_angular_speed.error());
    }
    if (!max_vertical_speed) {
      return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(max_vertical_speed.error());
    }
    if (!requires_ground) {
      return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(requires_ground.error());
    }
    if (!requires_water) {
      return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(requires_water.error());
    }
    if (!min_water_support) {
      return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(min_water_support.error());
    }
    authority.max_speed = max_speed.value();
    authority.max_acceleration = max_acceleration.value();
    authority.max_deceleration = max_deceleration.value();
    authority.max_angular_speed = degrees_to_radians(max_angular_speed.value());
    authority.max_vertical_speed = max_vertical_speed.value();
    if (const io::DataValue* max_step = root.get("max_step_height"); max_step != nullptr) {
      if (!is_number(*max_step)) {
        return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(
            {std::string{path} + ".max_step_height must be a number"});
      }
      authority.max_step_height = static_cast<f32>(max_step->as_f64_or(0.0));
    }
    authority.requires_ground = requires_ground.value();
    authority.requires_water = requires_water.value();
    authority.min_water_support_samples = min_water_support.value();
    return Expected<VehicleAuthorityDefinition, EntityRegistryError>::success(authority);
  };

  auto read_seat = [](const io::DataValue& root, std::string_view path)
      -> Expected<VehicleSeatDefinition, EntityRegistryError> {
    if (root.kind() != io::DataKind::Object) {
      return Expected<VehicleSeatDefinition, EntityRegistryError>::failure(
          {std::string{path} + " must be an object"});
    }
    VehicleSeatDefinition seat;
    auto id = read_required_string(root, "id", path);
    if (!id) {
      return Expected<VehicleSeatDefinition, EntityRegistryError>::failure(id.error());
    }
    seat.id = std::move(id.value());
    seat.name = seat.id;

    const io::DataValue* role = root.get("role");
    if (role == nullptr || role->kind() != io::DataKind::String) {
      return Expected<VehicleSeatDefinition, EntityRegistryError>::failure(
          {std::string{path} + ".role must be a string"});
    }
    auto parsed_role = seat_role_from_string(role->as_string_or({}), std::string{path} + ".role");
    if (!parsed_role) {
      return Expected<VehicleSeatDefinition, EntityRegistryError>::failure(parsed_role.error());
    }
    seat.role = parsed_role.value();

    const io::DataValue* position = root.get("local_position");
    if (position == nullptr) {
      return Expected<VehicleSeatDefinition, EntityRegistryError>::failure(
          {std::string{path} + ".local_position is required"});
    }
    auto parsed_position = read_vec3(*position, std::string{path} + ".local_position");
    if (!parsed_position) {
      return Expected<VehicleSeatDefinition, EntityRegistryError>::failure(parsed_position.error());
    }
    seat.local_position = parsed_position.value();

    const io::DataValue* rotation = root.get("local_rotation");
    if (rotation == nullptr) {
      return Expected<VehicleSeatDefinition, EntityRegistryError>::failure(
          {std::string{path} + ".local_rotation is required"});
    }
    auto parsed_rotation = read_quat(*rotation, std::string{path} + ".local_rotation");
    if (!parsed_rotation) {
      return Expected<VehicleSeatDefinition, EntityRegistryError>::failure(parsed_rotation.error());
    }
    seat.local_rotation = parsed_rotation.value();

    auto controls = read_required_bool(root, "controls_vehicle", path);
    if (!controls) {
      return Expected<VehicleSeatDefinition, EntityRegistryError>::failure(controls.error());
    }
    seat.controls_vehicle = controls.value();

    const io::DataValue* camera = root.get("camera_mode");
    if (camera == nullptr || camera->kind() != io::DataKind::String) {
      return Expected<VehicleSeatDefinition, EntityRegistryError>::failure(
          {std::string{path} + ".camera_mode must be a string"});
    }
    auto parsed_camera =
        camera_mode_from_string(camera->as_string_or({}), std::string{path} + ".camera_mode");
    if (!parsed_camera) {
      return Expected<VehicleSeatDefinition, EntityRegistryError>::failure(parsed_camera.error());
    }
    seat.camera_mode = parsed_camera.value();
    return Expected<VehicleSeatDefinition, EntityRegistryError>::success(std::move(seat));
  };

  auto read_vehicle = [&](const io::DataValue& vehicle_json)
      -> Expected<VehicleDefinition, EntityRegistryError> {
    if (vehicle_json.kind() != io::DataKind::Object) {
      return Expected<VehicleDefinition, EntityRegistryError>::failure(
          {"vehicle definition entry must be an object"});
    }
    VehicleDefinition definition;
    auto name = read_required_string(vehicle_json, "name", "vehicle");
    if (!name) {
      return Expected<VehicleDefinition, EntityRegistryError>::failure(name.error());
    }
    definition.name = std::move(name.value());
    const std::string path = "vehicle " + definition.name;

    const io::DataValue* kind = vehicle_json.get("kind");
    if (kind == nullptr || kind->kind() != io::DataKind::String) {
      return Expected<VehicleDefinition, EntityRegistryError>::failure(
          {path + ".kind must be a string"});
    }
    auto parsed_kind = vehicle_kind_from_string(kind->as_string_or({}), path + ".kind");
    if (!parsed_kind) {
      return Expected<VehicleDefinition, EntityRegistryError>::failure(parsed_kind.error());
    }
    definition.kind = parsed_kind.value();

    const io::DataValue* physics = vehicle_json.get("physics");
    if (physics == nullptr || physics->kind() != io::DataKind::String) {
      return Expected<VehicleDefinition, EntityRegistryError>::failure(
          {path + ".physics must be a string"});
    }
    auto parsed_physics =
        vehicle_physics_from_string(physics->as_string_or({}), path + ".physics");
    if (!parsed_physics) {
      return Expected<VehicleDefinition, EntityRegistryError>::failure(parsed_physics.error());
    }
    definition.physics = parsed_physics.value();

    const io::DataValue* collision = vehicle_json.get("collision");
    if (collision == nullptr || collision->kind() != io::DataKind::Object) {
      return Expected<VehicleDefinition, EntityRegistryError>::failure(
          {path + ".collision must be an object"});
    }
    if (const io::DataValue* type = collision->get("type"); type != nullptr) {
      if (type->kind() != io::DataKind::String || type->as_string_or({}) != "aabb") {
        return Expected<VehicleDefinition, EntityRegistryError>::failure(
            {path + ".collision.type must be aabb"});
      }
    }
    const io::DataValue* size = collision->get("size");
    if (size == nullptr) {
      return Expected<VehicleDefinition, EntityRegistryError>::failure(
          {path + ".collision.size is required"});
    }
    auto parsed_size = read_vec3(*size, path + ".collision.size");
    if (!parsed_size) {
      return Expected<VehicleDefinition, EntityRegistryError>::failure(parsed_size.error());
    }
    definition.collision_size = parsed_size.value();
    if (definition.collision_size.x <= 0.0F || definition.collision_size.y <= 0.0F ||
        definition.collision_size.z <= 0.0F || !finite(definition.collision_size)) {
      return Expected<VehicleDefinition, EntityRegistryError>::failure(
          {path + ".collision.size must contain positive finite numbers"});
    }

    const io::DataValue* seats = vehicle_json.get("seats");
    const io::DataValue::Array* seat_array = seats == nullptr ? nullptr : seats->array_if();
    if (seat_array == nullptr || seat_array->empty()) {
      return Expected<VehicleDefinition, EntityRegistryError>::failure(
          {path + ".seats must be a non-empty array"});
    }
    definition.seats.reserve(seat_array->size());
    for (usize index = 0; index < seat_array->size(); ++index) {
      auto seat = read_seat((*seat_array)[index], path + ".seats[" + std::to_string(index) + "]");
      if (!seat) {
        return Expected<VehicleDefinition, EntityRegistryError>::failure(seat.error());
      }
      definition.seats.push_back(std::move(seat.value()));
    }

    const io::DataValue* movement = vehicle_json.get("movement");
    if (movement == nullptr || movement->kind() != io::DataKind::Object) {
      return Expected<VehicleDefinition, EntityRegistryError>::failure(
          {path + ".movement must be an object"});
    }
    if (definition.physics == VehiclePhysicsKind::LivingMount) {
      auto walk_speed = read_required_f32(*movement, "walk_speed", path + ".movement");
      auto sprint_speed = read_required_f32(*movement, "sprint_speed", path + ".movement");
      auto acceleration = read_required_f32(*movement, "acceleration", path + ".movement");
      auto turn_rate = read_required_f32(*movement, "turn_rate", path + ".movement");
      auto jump_strength = read_required_f32(*movement, "jump_strength", path + ".movement");
      auto gravity_scale = read_required_f32(*movement, "gravity_scale", path + ".movement");
      auto step_height = read_required_f32(*movement, "step_height", path + ".movement");
      auto friction = read_required_f32(*movement, "friction", path + ".movement");
      if (!walk_speed)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(walk_speed.error());
      if (!sprint_speed)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(sprint_speed.error());
      if (!acceleration)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(acceleration.error());
      if (!turn_rate)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(turn_rate.error());
      if (!jump_strength)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(jump_strength.error());
      if (!gravity_scale)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(gravity_scale.error());
      if (!step_height)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(step_height.error());
      if (!friction)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(friction.error());
      definition.living_mount = {
          walk_speed.value(),
          sprint_speed.value(),
          acceleration.value(),
          degrees_to_radians(turn_rate.value()),
          jump_strength.value(),
          gravity_scale.value(),
          step_height.value(),
          friction.value()};
    } else if (definition.physics == VehiclePhysicsKind::SurfaceWaterVehicle) {
      auto acceleration = read_required_f32(*movement, "acceleration", path + ".movement");
      auto max_speed = read_required_f32(*movement, "max_speed", path + ".movement");
      auto turn_rate = read_required_f32(*movement, "turn_rate", path + ".movement");
      auto water_drag = read_required_f32(*movement, "water_drag", path + ".movement");
      auto land_drag = read_required_f32(*movement, "land_drag", path + ".movement");
      auto float_height_offset = read_required_f32(*movement, "float_height_offset", path + ".movement");
      auto buoyancy_snap_speed = read_required_f32(*movement, "buoyancy_snap_speed", path + ".movement");
      if (!acceleration)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(acceleration.error());
      if (!max_speed)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(max_speed.error());
      if (!turn_rate)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(turn_rate.error());
      if (!water_drag)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(water_drag.error());
      if (!land_drag)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(land_drag.error());
      if (!float_height_offset)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(float_height_offset.error());
      if (!buoyancy_snap_speed)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(buoyancy_snap_speed.error());
      definition.surface_water = {
          acceleration.value(),
          max_speed.value(),
          degrees_to_radians(turn_rate.value()),
          water_drag.value(),
          land_drag.value(),
          float_height_offset.value(),
          buoyancy_snap_speed.value()};
    } else {
      auto max_forward_speed = read_required_f32(*movement, "max_forward_speed", path + ".movement");
      auto max_reverse_speed = read_required_f32(*movement, "max_reverse_speed", path + ".movement");
      auto acceleration = read_required_f32(*movement, "acceleration", path + ".movement");
      auto brake_force = read_required_f32(*movement, "brake_force", path + ".movement");
      auto drag = read_required_f32(*movement, "drag", path + ".movement");
      auto turn_rate = read_required_f32(*movement, "turn_rate", path + ".movement");
      auto lateral_grip = read_required_f32(*movement, "lateral_grip", path + ".movement");
      auto gravity_scale = read_required_f32(*movement, "gravity_scale", path + ".movement");
      auto max_step_height = read_required_f32(*movement, "max_step_height", path + ".movement");
      if (!max_forward_speed)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(max_forward_speed.error());
      if (!max_reverse_speed)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(max_reverse_speed.error());
      if (!acceleration)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(acceleration.error());
      if (!brake_force)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(brake_force.error());
      if (!drag)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(drag.error());
      if (!turn_rate)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(turn_rate.error());
      if (!lateral_grip)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(lateral_grip.error());
      if (!gravity_scale)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(gravity_scale.error());
      if (!max_step_height)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(max_step_height.error());
      definition.ground = {
          max_forward_speed.value(),
          max_reverse_speed.value(),
          acceleration.value(),
          brake_force.value(),
          drag.value(),
          degrees_to_radians(turn_rate.value()),
          lateral_grip.value(),
          gravity_scale.value(),
          max_step_height.value()};
    }

    const io::DataValue* authority = vehicle_json.get("authority");
    if (authority == nullptr) {
      return Expected<VehicleDefinition, EntityRegistryError>::failure(
          {path + ".authority is required"});
    }
    auto parsed_authority = read_authority(*authority, path + ".authority");
    if (!parsed_authority) {
      return Expected<VehicleDefinition, EntityRegistryError>::failure(parsed_authority.error());
    }
    definition.authority = parsed_authority.value();
    return Expected<VehicleDefinition, EntityRegistryError>::success(std::move(definition));
  };

  std::vector<VehicleDefinition> definitions;
  if (vehicles == nullptr) {
    auto definition = read_vehicle(root);
    if (!definition) {
      return Expected<std::vector<VehicleDefinition>, EntityRegistryError>::failure(definition.error());
    }
    definitions.push_back(std::move(definition.value()));
  } else {
    const io::DataValue::Array* vehicle_array = vehicles->array_if();
    definitions.reserve(vehicle_array == nullptr ? 0U : vehicle_array->size());
    for (const io::DataValue& vehicle : *vehicle_array) {
      auto definition = read_vehicle(vehicle);
      if (!definition) {
        return Expected<std::vector<VehicleDefinition>, EntityRegistryError>::failure(definition.error());
      }
      definitions.push_back(std::move(definition.value()));
    }
  }
  return Expected<std::vector<VehicleDefinition>, EntityRegistryError>::success(std::move(definitions));
}

} // namespace arv::entity
