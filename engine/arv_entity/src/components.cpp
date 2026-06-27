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

[[nodiscard]] Expected<f32, EntityRegistryError> read_optional_f32(
    const io::DataValue& object,
    std::string_view field,
    std::string_view path,
    f32 fallback) {
  const io::DataValue* value = object.get(field);
  if (value == nullptr) {
    return Expected<f32, EntityRegistryError>::success(fallback);
  }
  if (!is_number(*value)) {
    return Expected<f32, EntityRegistryError>::failure(
        {std::string{path} + "." + std::string{field} + " must be a number"});
  }
  const f32 number = static_cast<f32>(value->as_f64_or(fallback));
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

[[nodiscard]] bool valid_resource_namespace_char(char value, bool first) noexcept {
  const bool alpha = value >= 'a' && value <= 'z';
  const bool digit = value >= '0' && value <= '9';
  return alpha || (!first && digit) || value == '_' || value == '-' || value == '.';
}

[[nodiscard]] bool valid_resource_path_char(char value) noexcept {
  const bool alpha = value >= 'a' && value <= 'z';
  const bool digit = value >= '0' && value <= '9';
  return alpha || digit || value == '_' || value == '-' || value == '.' || value == '/';
}

[[nodiscard]] bool resource_path_has_unsafe_segment(std::string_view path) noexcept {
  usize segment_start = 0U;
  while (segment_start <= path.size()) {
    const usize separator = path.find('/', segment_start);
    const usize segment_end = separator == std::string_view::npos ? path.size() : separator;
    const std::string_view segment =
        path.substr(segment_start, segment_end - segment_start);
    if (segment.empty() || segment == "." || segment == "..") {
      return true;
    }
    if (separator == std::string_view::npos) {
      break;
    }
    segment_start = separator + 1U;
  }
  return false;
}

[[nodiscard]] bool valid_resource_id_syntax(std::string_view value) noexcept {
  const usize separator = value.find(':');
  if (separator == std::string_view::npos || separator == 0U ||
      separator + 1U >= value.size() ||
      value.find(':', separator + 1U) != std::string_view::npos) {
    return false;
  }
  const std::string_view name_space = value.substr(0U, separator);
  const std::string_view path = value.substr(separator + 1U);
  for (usize index = 0U; index < name_space.size(); ++index) {
    if (!valid_resource_namespace_char(name_space[index], index == 0U)) {
      return false;
    }
  }
  return !resource_path_has_unsafe_segment(path) &&
         std::all_of(path.begin(), path.end(), valid_resource_path_char);
}

[[nodiscard]] Expected<std::vector<std::string>, EntityRegistryError>
read_optional_resource_id_array(
    const io::DataValue& object,
    std::string_view field,
    std::string_view path) {
  std::vector<std::string> out;
  const io::DataValue* value = object.get(field);
  if (value == nullptr) {
    return Expected<std::vector<std::string>, EntityRegistryError>::success(std::move(out));
  }
  const io::DataValue::Array* array = value->array_if();
  if (array == nullptr) {
    return Expected<std::vector<std::string>, EntityRegistryError>::failure(
        {std::string{path} + "." + std::string{field} + " must be an array"});
  }
  out.reserve(array->size());
  for (usize index = 0U; index < array->size(); ++index) {
    const io::DataValue& entry = (*array)[index];
    if (entry.kind() != io::DataKind::String) {
      return Expected<std::vector<std::string>, EntityRegistryError>::failure(
          {std::string{path} + "." + std::string{field} + "[" +
           std::to_string(index) + "] must be a string"});
    }
    std::string id{entry.as_string_or({})};
    if (!valid_resource_id_syntax(id)) {
      return Expected<std::vector<std::string>, EntityRegistryError>::failure(
          {std::string{path} + "." + std::string{field} + "[" +
           std::to_string(index) + "] must be a valid resource ID"});
    }
    out.push_back(std::move(id));
  }
  return Expected<std::vector<std::string>, EntityRegistryError>::success(std::move(out));
}

[[nodiscard]] Expected<AircraftMovementDefinition, EntityRegistryError>
read_aircraft_movement_definition(
    const io::DataValue& movement,
    const std::string& path) {
  if (movement.kind() != io::DataKind::Object) {
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(
        {path + " must be an object"});
  }
  auto min_speed = read_required_f32(movement, "min_speed", path);
  auto max_speed = read_required_f32(movement, "max_speed", path);
  auto acceleration = read_required_f32(movement, "acceleration", path);
  auto brake_deceleration = read_required_f32(movement, "brake_deceleration", path);
  auto drag = read_required_f32(movement, "drag", path);
  auto pitch_rate = read_required_f32(movement, "pitch_rate", path);
  auto yaw_rate = read_required_f32(movement, "yaw_rate", path);
  auto roll_rate = read_required_f32(movement, "roll_rate", path);
  auto roll_auto_level_rate = read_required_f32(movement, "roll_auto_level_rate", path);
  auto lift_strength = read_required_f32(movement, "lift_strength", path);
  auto stall_speed = read_required_f32(movement, "stall_speed", path);
  auto stall_gravity_scale = read_required_f32(movement, "stall_gravity_scale", path);
  auto gravity_scale = read_required_f32(movement, "gravity_scale", path);
  auto max_climb_rate = read_required_f32(movement, "max_climb_rate", path);
  auto max_descent_rate = read_required_f32(movement, "max_descent_rate", path);
  auto ground_takeoff_speed = read_required_f32(movement, "ground_takeoff_speed", path);
  auto ground_drag = read_required_f32(movement, "ground_drag", path);
  auto vertical_thrust = read_optional_bool(movement, "vertical_thrust", path, false);
  if (!min_speed)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(min_speed.error());
  if (!max_speed)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(max_speed.error());
  if (!acceleration)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(acceleration.error());
  if (!brake_deceleration)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(brake_deceleration.error());
  if (!drag)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(drag.error());
  if (!pitch_rate)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(pitch_rate.error());
  if (!yaw_rate)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(yaw_rate.error());
  if (!roll_rate)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(roll_rate.error());
  if (!roll_auto_level_rate)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(roll_auto_level_rate.error());
  if (!lift_strength)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(lift_strength.error());
  if (!stall_speed)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(stall_speed.error());
  if (!stall_gravity_scale)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(stall_gravity_scale.error());
  if (!gravity_scale)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(gravity_scale.error());
  if (!max_climb_rate)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(max_climb_rate.error());
  if (!max_descent_rate)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(max_descent_rate.error());
  if (!ground_takeoff_speed)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(ground_takeoff_speed.error());
  if (!ground_drag)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(ground_drag.error());
  if (!vertical_thrust)
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(vertical_thrust.error());
  if (min_speed.value() < 0.0F || max_speed.value() <= 0.0F ||
      min_speed.value() > max_speed.value()) {
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(
        {path + " must satisfy 0 <= min_speed <= max_speed"});
  }
  if (acceleration.value() < 0.0F || brake_deceleration.value() < 0.0F ||
      lift_strength.value() < 0.0F || stall_speed.value() < 0.0F ||
      stall_gravity_scale.value() < 0.0F || gravity_scale.value() < 0.0F ||
      max_climb_rate.value() < 0.0F || max_descent_rate.value() < 0.0F ||
      ground_takeoff_speed.value() < 0.0F) {
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(
        {path + " aircraft speeds and forces must be non-negative"});
  }
  if (drag.value() < 0.0F || drag.value() > 1.0F ||
      ground_drag.value() < 0.0F || ground_drag.value() > 1.0F) {
    return Expected<AircraftMovementDefinition, EntityRegistryError>::failure(
        {path + " aircraft drag values must be between 0 and 1"});
  }
  return Expected<AircraftMovementDefinition, EntityRegistryError>::success({min_speed.value(),
                                                                             max_speed.value(),
                                                                             acceleration.value(),
                                                                             brake_deceleration.value(),
                                                                             drag.value(),
                                                                             degrees_to_radians(pitch_rate.value()),
                                                                             degrees_to_radians(yaw_rate.value()),
                                                                             degrees_to_radians(roll_rate.value()),
                                                                             degrees_to_radians(roll_auto_level_rate.value()),
                                                                             lift_strength.value(),
                                                                             stall_speed.value(),
                                                                             stall_gravity_scale.value(),
                                                                             gravity_scale.value(),
                                                                             max_climb_rate.value(),
                                                                             max_descent_rate.value(),
                                                                             ground_takeoff_speed.value(),
                                                                             ground_drag.value(),
                                                                             vertical_thrust.value()});
}

[[nodiscard]] Expected<VehicleFuelDefinition, EntityRegistryError> read_vehicle_fuel_definition(
    const io::DataValue& root,
    const std::string& path) {
  if (root.kind() != io::DataKind::Object) {
    return Expected<VehicleFuelDefinition, EntityRegistryError>::failure(
        {path + " must be an object"});
  }
  VehicleFuelDefinition fuel;
  auto item = read_required_string(root, "item", path);
  if (!item) {
    return Expected<VehicleFuelDefinition, EntityRegistryError>::failure(item.error());
  }
  if (!valid_resource_id_syntax(item.value())) {
    return Expected<VehicleFuelDefinition, EntityRegistryError>::failure(
        {path + ".item must be a valid resource ID"});
  }
  fuel.item = std::move(item.value());
  auto capacity = read_required_f32(root, "capacity_seconds", path);
  if (!capacity) {
    capacity = read_required_f32(root, "max_fuel_seconds", path);
  }
  auto initial = read_optional_f32(root, "initial_seconds", path, -1.0F);
  auto burn = read_optional_f32(root, "burn_per_second", path, fuel.burn_per_second);
  auto min_launch =
      read_optional_f32(root, "min_fuel_for_launch_seconds", path, fuel.min_fuel_for_launch_seconds);
  if (!min_launch) {
    min_launch =
        read_optional_f32(root, "min_fuel_for_launch_secs", path, fuel.min_fuel_for_launch_seconds);
  }
  if (!capacity)
    return Expected<VehicleFuelDefinition, EntityRegistryError>::failure(capacity.error());
  if (!initial)
    return Expected<VehicleFuelDefinition, EntityRegistryError>::failure(initial.error());
  if (!burn)
    return Expected<VehicleFuelDefinition, EntityRegistryError>::failure(burn.error());
  if (!min_launch)
    return Expected<VehicleFuelDefinition, EntityRegistryError>::failure(min_launch.error());
  if (capacity.value() <= 0.0F || burn.value() <= 0.0F ||
      min_launch.value() < 0.0F) {
    return Expected<VehicleFuelDefinition, EntityRegistryError>::failure(
        {path + " capacity and burn rate must be positive, and launch fuel must be non-negative"});
  }
  fuel.capacity_seconds = capacity.value();
  fuel.initial_seconds = initial.value() < 0.0F ? fuel.capacity_seconds : initial.value();
  fuel.initial_seconds = std::clamp(fuel.initial_seconds, 0.0F, fuel.capacity_seconds);
  fuel.burn_per_second = burn.value();
  fuel.min_fuel_for_launch_seconds =
      std::min(min_launch.value(), fuel.capacity_seconds);
  return Expected<VehicleFuelDefinition, EntityRegistryError>::success(std::move(fuel));
}

[[nodiscard]] Expected<VehicleCargoDefinition, EntityRegistryError> read_vehicle_cargo_definition(
    const io::DataValue& root,
    const std::string& path) {
  if (root.kind() != io::DataKind::Object) {
    return Expected<VehicleCargoDefinition, EntityRegistryError>::failure(
        {path + " must be an object"});
  }
  auto slots = read_optional_u32(root, "slots", path, 0U);
  if (!slots) {
    return Expected<VehicleCargoDefinition, EntityRegistryError>::failure(slots.error());
  }
  if (slots.value() == 0U && root.get("slots") == nullptr && root.get("slot_count") != nullptr) {
    slots = read_optional_u32(root, "slot_count", path, 0U);
  }
  if (!slots) {
    return Expected<VehicleCargoDefinition, EntityRegistryError>::failure(slots.error());
  }
  if (slots.value() == 0U || slots.value() > MaxVehicleCargoSlots) {
    return Expected<VehicleCargoDefinition, EntityRegistryError>::failure(
        {path + ".slots must be between 1 and " + std::to_string(MaxVehicleCargoSlots)});
  }
  return Expected<VehicleCargoDefinition, EntityRegistryError>::success(
      VehicleCargoDefinition{static_cast<u16>(slots.value())});
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
  if (value == "vehicle_chase") {
    return Expected<CameraMode, EntityRegistryError>::success(CameraMode::VehicleChase);
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
  if (value == "aircraft") {
    return Expected<VehiclePhysicsKind, EntityRegistryError>::success(
        VehiclePhysicsKind::Aircraft);
  }
  if (value == "submersible") {
    return Expected<VehiclePhysicsKind, EntityRegistryError>::success(
        VehiclePhysicsKind::Submersible);
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
  if (goal.type.empty()) {
    return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(
        {"brain goal type must not be empty"});
  }
  if (const io::DataValue* value = goal_json.get("priority"); value != nullptr) {
    if (value->kind() != io::DataKind::I64 && value->kind() != io::DataKind::U64) {
      return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(
          {"brain goal priority must be an integer"});
    }
    goal.priority = static_cast<i32>(value->as_i64_or(goal.priority));
  }
  auto radius = read_optional_f32(goal_json, "radius", "brain goal", goal.radius);
  if (!radius) {
    return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(radius.error());
  }
  goal.radius = std::max(0.0F, radius.value());
  auto speed_multiplier =
      read_optional_f32(goal_json, "speed_multiplier", "brain goal", goal.speed_multiplier);
  if (!speed_multiplier) {
    return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(
        speed_multiplier.error());
  }
  goal.speed_multiplier = std::max(0.0F, speed_multiplier.value());
  auto duration_ticks =
      read_optional_u32(goal_json, "duration_ticks", "brain goal", goal.duration_ticks);
  if (!duration_ticks) {
    return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(
        duration_ticks.error());
  }
  goal.duration_ticks = duration_ticks.value();
  auto cooldown_ticks =
      read_optional_u32(goal_json, "cooldown_ticks", "brain goal", goal.cooldown_ticks);
  if (!cooldown_ticks) {
    return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(
        cooldown_ticks.error());
  }
  goal.cooldown_ticks = cooldown_ticks.value();
  auto range = read_optional_f32(goal_json, "range", "brain goal", goal.range);
  if (!range) {
    return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(range.error());
  }
  goal.range = std::max(0.0F, range.value());
  auto damage = read_optional_f32(goal_json, "damage", "brain goal", goal.damage);
  if (!damage) {
    return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(damage.error());
  }
  goal.damage = std::max(0.0F, damage.value());
  auto memory_ticks =
      read_optional_u32(goal_json, "memory_ticks", "brain goal", goal.memory_ticks);
  if (!memory_ticks) {
    return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(
        memory_ticks.error());
  }
  goal.memory_ticks = memory_ticks.value();
  auto require_line_of_sight = read_optional_bool(
      goal_json,
      "require_line_of_sight",
      "brain goal",
      goal.require_line_of_sight);
  if (!require_line_of_sight) {
    return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(
        require_line_of_sight.error());
  }
  goal.require_line_of_sight = require_line_of_sight.value();
  if (const io::DataValue* value = goal_json.get("target"); value != nullptr) {
    if (value->kind() != io::DataKind::String) {
      return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(
          {"brain goal target must be a string"});
    }
    goal.target = std::string{value->as_string_or({})};
  }
  if (const io::DataValue* value = goal_json.get("animation"); value != nullptr) {
    if (value->kind() != io::DataKind::String) {
      return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(
          {"brain goal animation must be a string"});
    }
    goal.animation = std::string{value->as_string_or({})};
  }
  if (const io::DataValue* value = goal_json.get("attack_animation"); value != nullptr) {
    if (value->kind() != io::DataKind::String) {
      return Expected<AuthoredGoalDefinition, EntityRegistryError>::failure(
          {"brain goal attack_animation must be a string"});
    }
    goal.attack_animation = std::string{value->as_string_or({})};
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

    HashSet<std::string> tag_ids;
    for (const std::string& tag : definition.tags) {
      if (!valid_resource_id_syntax(tag)) {
        return Result::failure({"vehicle " + definition.name + " has invalid tag " + tag});
      }
      if (!tag_ids.insert(tag).second) {
        return Result::failure({"vehicle " + definition.name + " has duplicate tag " + tag});
      }
    }

    if (definition.fuel.has_value()) {
      const VehicleFuelDefinition& fuel = definition.fuel.value();
      if (!valid_resource_id_syntax(fuel.item)) {
        return Result::failure({"vehicle " + definition.name + " has invalid fuel item"});
      }
      if (fuel.capacity_seconds <= 0.0F || !std::isfinite(fuel.capacity_seconds) ||
          fuel.initial_seconds < 0.0F || !std::isfinite(fuel.initial_seconds) ||
          fuel.burn_per_second <= 0.0F || !std::isfinite(fuel.burn_per_second) ||
          fuel.min_fuel_for_launch_seconds < 0.0F ||
          !std::isfinite(fuel.min_fuel_for_launch_seconds)) {
        return Result::failure({"vehicle " + definition.name + " has invalid fuel values"});
      }
      if (fuel.initial_seconds > fuel.capacity_seconds ||
          fuel.min_fuel_for_launch_seconds > fuel.capacity_seconds) {
        return Result::failure({"vehicle " + definition.name + " fuel values exceed capacity"});
      }
    }
    if (definition.cargo.has_value()) {
      if (definition.cargo->slots == 0U || definition.cargo->slots > MaxVehicleCargoSlots) {
        return Result::failure({"vehicle " + definition.name + " has invalid cargo slots"});
      }
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
    auto requires_swept_collision =
        read_optional_bool(root, "requires_swept_collision", path, false);
    auto min_water_occupancy =
        read_optional_f32(root, "min_water_occupancy", path, 0.0F);
    auto max_unloaded_chunk_entry_distance =
        read_optional_f32(root, "max_unloaded_chunk_entry_distance", path, -1.0F);
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
    if (!requires_swept_collision) {
      return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(
          requires_swept_collision.error());
    }
    if (!min_water_occupancy) {
      return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(
          min_water_occupancy.error());
    }
    if (!max_unloaded_chunk_entry_distance) {
      return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(
          max_unloaded_chunk_entry_distance.error());
    }
    if (!min_water_support) {
      return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(min_water_support.error());
    }
    authority.max_speed = max_speed.value();
    authority.max_acceleration = max_acceleration.value();
    authority.max_deceleration = max_deceleration.value();
    authority.max_angular_speed = degrees_to_radians(max_angular_speed.value());
    authority.max_vertical_speed = max_vertical_speed.value();
    authority.min_water_occupancy = min_water_occupancy.value();
    if (authority.min_water_occupancy < 0.0F || authority.min_water_occupancy > 1.0F) {
      return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(
          {std::string{path} + ".min_water_occupancy must be between 0 and 1"});
    }
    authority.max_unloaded_chunk_entry_distance = max_unloaded_chunk_entry_distance.value();
    if (const io::DataValue* max_step = root.get("max_step_height"); max_step != nullptr) {
      if (!is_number(*max_step)) {
        return Expected<VehicleAuthorityDefinition, EntityRegistryError>::failure(
            {std::string{path} + ".max_step_height must be a number"});
      }
      authority.max_step_height = static_cast<f32>(max_step->as_f64_or(0.0));
    }
    authority.requires_ground = requires_ground.value();
    authority.requires_water = requires_water.value();
    authority.requires_swept_collision = requires_swept_collision.value();
    authority.min_water_support_samples = min_water_support.value();
    return Expected<VehicleAuthorityDefinition, EntityRegistryError>::success(authority);
  };

  auto read_crash = [](const io::DataValue& root, std::string_view path)
      -> Expected<VehicleCrashDefinition, EntityRegistryError> {
    if (root.kind() != io::DataKind::Object) {
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
          {std::string{path} + " must be an object"});
    }

    VehicleCrashDefinition crash;
    crash.enabled = true;
    auto bump_threshold = read_optional_f32(root, "bump_threshold", path, 0.0F);
    auto hard_impact_threshold =
        read_optional_f32(root, "hard_impact_threshold", path, 0.0F);
    auto ejection_threshold =
        read_optional_f32(root, "ejection_threshold", path, 0.0F);
    auto destruction_threshold =
        read_optional_f32(root, "destruction_threshold", path, 0.0F);
    auto explosion_threshold =
        read_optional_f32(root, "explosion_threshold", path, 0.0F);
    auto min_direct_impact_dot =
        read_optional_f32(root, "min_direct_impact_dot", path, crash.min_direct_impact_dot);
    auto event_cooldown_ticks =
        read_optional_u32(root, "event_cooldown_ticks", path, crash.event_cooldown_ticks);
    auto velocity_retention =
        read_optional_f32(root, "velocity_retention", path, crash.velocity_retention);
    auto ejection_velocity_scale =
        read_optional_f32(root, "ejection_velocity_scale", path, crash.ejection_velocity_scale);
    auto ejection_upward_velocity =
        read_optional_f32(root, "ejection_upward_velocity", path, crash.ejection_upward_velocity);
    auto ejection_min_horizontal_speed =
        read_optional_f32(root, "ejection_min_horizontal_speed", path, crash.ejection_min_horizontal_speed);
    auto ejection_max_horizontal_speed =
        read_optional_f32(root, "ejection_max_horizontal_speed", path, crash.ejection_max_horizontal_speed);
    auto ejection_max_vertical_speed =
        read_optional_f32(root, "ejection_max_vertical_speed", path, crash.ejection_max_vertical_speed);
    auto remount_cooldown_ticks =
        read_optional_u32(root, "remount_cooldown_ticks", path, crash.remount_cooldown_ticks);
    auto destroy_on_destruction =
        read_optional_bool(root, "destroy_on_destruction", path, crash.destroy_on_destruction);
    auto explode_on_destruction =
        read_optional_bool(root, "explode_on_destruction", path, crash.explode_on_destruction);

    if (!bump_threshold)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(bump_threshold.error());
    if (!hard_impact_threshold)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
          hard_impact_threshold.error());
    if (!ejection_threshold)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(ejection_threshold.error());
    if (!destruction_threshold)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
          destruction_threshold.error());
    if (!explosion_threshold)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(explosion_threshold.error());
    if (!min_direct_impact_dot)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
          min_direct_impact_dot.error());
    if (!event_cooldown_ticks)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
          event_cooldown_ticks.error());
    if (!velocity_retention)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
          velocity_retention.error());
    if (!ejection_velocity_scale)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
          ejection_velocity_scale.error());
    if (!ejection_upward_velocity)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
          ejection_upward_velocity.error());
    if (!ejection_min_horizontal_speed)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
          ejection_min_horizontal_speed.error());
    if (!ejection_max_horizontal_speed)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
          ejection_max_horizontal_speed.error());
    if (!ejection_max_vertical_speed)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
          ejection_max_vertical_speed.error());
    if (!remount_cooldown_ticks)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
          remount_cooldown_ticks.error());
    if (!destroy_on_destruction)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
          destroy_on_destruction.error());
    if (!explode_on_destruction)
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
          explode_on_destruction.error());

    crash.bump_threshold = bump_threshold.value();
    crash.hard_impact_threshold = hard_impact_threshold.value();
    crash.ejection_threshold = ejection_threshold.value();
    crash.destruction_threshold = destruction_threshold.value();
    crash.explosion_threshold = explosion_threshold.value();
    crash.min_direct_impact_dot = min_direct_impact_dot.value();
    crash.event_cooldown_ticks = event_cooldown_ticks.value();
    crash.velocity_retention = velocity_retention.value();
    crash.ejection_velocity_scale = ejection_velocity_scale.value();
    crash.ejection_upward_velocity = ejection_upward_velocity.value();
    crash.ejection_min_horizontal_speed = ejection_min_horizontal_speed.value();
    crash.ejection_max_horizontal_speed = ejection_max_horizontal_speed.value();
    crash.ejection_max_vertical_speed = ejection_max_vertical_speed.value();
    crash.remount_cooldown_ticks = remount_cooldown_ticks.value();
    crash.destroy_on_destruction = destroy_on_destruction.value();
    crash.explode_on_destruction = explode_on_destruction.value();

    const auto reject = [&](std::string_view suffix, std::string_view message)
        -> Expected<VehicleCrashDefinition, EntityRegistryError> {
      return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
          {std::string{path} + "." + std::string{suffix} + " " + std::string{message}});
    };
    if (crash.bump_threshold < 0.0F || crash.hard_impact_threshold < 0.0F ||
        crash.ejection_threshold < 0.0F || crash.destruction_threshold < 0.0F ||
        crash.explosion_threshold < 0.0F) {
      return reject("thresholds", "must be non-negative");
    }
    if (crash.hard_impact_threshold > 0.0F &&
        crash.bump_threshold > crash.hard_impact_threshold) {
      return reject("hard_impact_threshold", "must be at least bump_threshold");
    }
    if (crash.ejection_threshold > 0.0F &&
        crash.hard_impact_threshold > crash.ejection_threshold) {
      return reject("ejection_threshold", "must be at least hard_impact_threshold");
    }
    if (crash.destruction_threshold > 0.0F &&
        crash.hard_impact_threshold > crash.destruction_threshold) {
      return reject("destruction_threshold", "must be at least hard_impact_threshold");
    }
    if (crash.destruction_threshold > 0.0F && crash.ejection_threshold > 0.0F &&
        crash.ejection_threshold > crash.destruction_threshold) {
      return reject("destruction_threshold", "must be at least ejection_threshold");
    }
    if (crash.explosion_threshold > 0.0F && crash.destruction_threshold > 0.0F &&
        crash.explosion_threshold < crash.destruction_threshold) {
      return reject("explosion_threshold", "must be at least destruction_threshold");
    }
    if (crash.min_direct_impact_dot < 0.0F || crash.min_direct_impact_dot > 1.0F) {
      return reject("min_direct_impact_dot", "must be between 0 and 1");
    }
    if (crash.velocity_retention < 0.0F || crash.velocity_retention > 1.0F) {
      return reject("velocity_retention", "must be between 0 and 1");
    }
    if (crash.ejection_velocity_scale < 0.0F ||
        crash.ejection_upward_velocity < 0.0F ||
        crash.ejection_min_horizontal_speed < 0.0F ||
        crash.ejection_max_horizontal_speed < 0.0F ||
        crash.ejection_max_vertical_speed < 0.0F) {
      return reject("ejection_velocity", "values must be non-negative");
    }
    if (crash.ejection_min_horizontal_speed > crash.ejection_max_horizontal_speed) {
      return reject("ejection_max_horizontal_speed", "must be at least ejection_min_horizontal_speed");
    }

    if (const io::DataValue* damage = root.get("damage"); damage != nullptr) {
      if (damage->kind() != io::DataKind::Object) {
        return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
            {std::string{path} + ".damage must be an object"});
      }
      auto bump_damage = read_optional_f32(*damage, "bump", std::string{path} + ".damage", 0.0F);
      auto hard_damage =
          read_optional_f32(*damage, "hard_impact", std::string{path} + ".damage", 0.0F);
      auto ejection_damage =
          read_optional_f32(*damage, "ejection", std::string{path} + ".damage", 0.0F);
      auto destruction_damage =
          read_optional_f32(*damage, "destruction", std::string{path} + ".damage", 0.0F);
      if (!bump_damage)
        return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(bump_damage.error());
      if (!hard_damage)
        return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(hard_damage.error());
      if (!ejection_damage)
        return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
            ejection_damage.error());
      if (!destruction_damage)
        return Expected<VehicleCrashDefinition, EntityRegistryError>::failure(
            destruction_damage.error());
      crash.bump_damage = bump_damage.value();
      crash.hard_impact_damage = hard_damage.value();
      crash.ejection_damage = ejection_damage.value();
      crash.destruction_damage = destruction_damage.value();
      if (crash.bump_damage < 0.0F || crash.hard_impact_damage < 0.0F ||
          crash.ejection_damage < 0.0F || crash.destruction_damage < 0.0F) {
        return reject("damage", "values must be non-negative");
      }
    }

    return Expected<VehicleCrashDefinition, EntityRegistryError>::success(crash);
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

    auto tags = read_optional_resource_id_array(vehicle_json, "tags", path);
    if (!tags) {
      return Expected<VehicleDefinition, EntityRegistryError>::failure(tags.error());
    }
    definition.tags = std::move(tags.value());

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
    } else if (definition.physics == VehiclePhysicsKind::GroundVehicle) {
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
    } else if (definition.physics == VehiclePhysicsKind::Aircraft) {
      auto parsed_aircraft =
          read_aircraft_movement_definition(*movement, path + ".movement");
      if (!parsed_aircraft) {
        return Expected<VehicleDefinition, EntityRegistryError>::failure(
            parsed_aircraft.error());
      }
      definition.aircraft = parsed_aircraft.value();
    } else if (definition.physics == VehiclePhysicsKind::Submersible) {
      auto max_forward_speed = read_required_f32(*movement, "max_forward_speed", path + ".movement");
      auto max_reverse_speed = read_required_f32(*movement, "max_reverse_speed", path + ".movement");
      auto acceleration = read_required_f32(*movement, "acceleration", path + ".movement");
      auto brake_deceleration = read_required_f32(*movement, "brake_deceleration", path + ".movement");
      auto turn_rate = read_required_f32(*movement, "turn_rate", path + ".movement");
      auto pitch_rate = read_required_f32(*movement, "pitch_rate", path + ".movement");
      auto ascend_rate = read_required_f32(*movement, "ascend_rate", path + ".movement");
      auto descend_rate = read_required_f32(*movement, "descend_rate", path + ".movement");
      auto water_drag = read_required_f32(*movement, "water_drag", path + ".movement");
      auto air_drag = read_required_f32(*movement, "air_drag", path + ".movement");
      auto gravity_scale_out_of_water =
          read_required_f32(*movement, "gravity_scale_out_of_water", path + ".movement");
      auto min_water_occupancy =
          read_required_f32(*movement, "min_water_occupancy", path + ".movement");
      auto preferred_water_occupancy =
          read_required_f32(*movement, "preferred_water_occupancy", path + ".movement");
      auto surface_float_offset =
          read_required_f32(*movement, "surface_float_offset", path + ".movement");
      if (!max_forward_speed)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(max_forward_speed.error());
      if (!max_reverse_speed)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(max_reverse_speed.error());
      if (!acceleration)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(acceleration.error());
      if (!brake_deceleration)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(brake_deceleration.error());
      if (!turn_rate)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(turn_rate.error());
      if (!pitch_rate)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(pitch_rate.error());
      if (!ascend_rate)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(ascend_rate.error());
      if (!descend_rate)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(descend_rate.error());
      if (!water_drag)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(water_drag.error());
      if (!air_drag)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(air_drag.error());
      if (!gravity_scale_out_of_water)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(gravity_scale_out_of_water.error());
      if (!min_water_occupancy)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(min_water_occupancy.error());
      if (!preferred_water_occupancy)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(preferred_water_occupancy.error());
      if (!surface_float_offset)
        return Expected<VehicleDefinition, EntityRegistryError>::failure(surface_float_offset.error());
      if (max_forward_speed.value() <= 0.0F || max_reverse_speed.value() < 0.0F ||
          acceleration.value() < 0.0F || brake_deceleration.value() < 0.0F ||
          ascend_rate.value() < 0.0F || descend_rate.value() < 0.0F ||
          gravity_scale_out_of_water.value() < 0.0F) {
        return Expected<VehicleDefinition, EntityRegistryError>::failure(
            {path + ".movement submersible speeds and forces must be non-negative"});
      }
      if (water_drag.value() < 0.0F || water_drag.value() > 1.0F ||
          air_drag.value() < 0.0F || air_drag.value() > 1.0F) {
        return Expected<VehicleDefinition, EntityRegistryError>::failure(
            {path + ".movement submersible drag values must be between 0 and 1"});
      }
      if (min_water_occupancy.value() < 0.0F || min_water_occupancy.value() > 1.0F ||
          preferred_water_occupancy.value() < 0.0F || preferred_water_occupancy.value() > 1.0F ||
          min_water_occupancy.value() > preferred_water_occupancy.value()) {
        return Expected<VehicleDefinition, EntityRegistryError>::failure(
            {path + ".movement must satisfy 0 <= min_water_occupancy <= preferred_water_occupancy <= 1"});
      }
      definition.submersible = {
          max_forward_speed.value(),
          max_reverse_speed.value(),
          acceleration.value(),
          brake_deceleration.value(),
          degrees_to_radians(turn_rate.value()),
          degrees_to_radians(pitch_rate.value()),
          ascend_rate.value(),
          descend_rate.value(),
          water_drag.value(),
          air_drag.value(),
          gravity_scale_out_of_water.value(),
          min_water_occupancy.value(),
          preferred_water_occupancy.value(),
          surface_float_offset.value()};
    }

    if (const io::DataValue* space_movement = vehicle_json.get("space_movement");
        space_movement != nullptr) {
      if (definition.physics != VehiclePhysicsKind::Aircraft) {
        return Expected<VehicleDefinition, EntityRegistryError>::failure(
            {path + ".space_movement is only supported for aircraft vehicles"});
      }
      auto parsed_space_aircraft =
          read_aircraft_movement_definition(*space_movement, path + ".space_movement");
      if (!parsed_space_aircraft) {
        return Expected<VehicleDefinition, EntityRegistryError>::failure(
            parsed_space_aircraft.error());
      }
      definition.space_aircraft = parsed_space_aircraft.value();
    }

    if (const io::DataValue* fuel = vehicle_json.get("fuel"); fuel != nullptr) {
      auto parsed_fuel = read_vehicle_fuel_definition(*fuel, path + ".fuel");
      if (!parsed_fuel) {
        return Expected<VehicleDefinition, EntityRegistryError>::failure(parsed_fuel.error());
      }
      definition.fuel = std::move(parsed_fuel.value());
    }

    if (const io::DataValue* cargo = vehicle_json.get("cargo"); cargo != nullptr) {
      auto parsed_cargo = read_vehicle_cargo_definition(*cargo, path + ".cargo");
      if (!parsed_cargo) {
        return Expected<VehicleDefinition, EntityRegistryError>::failure(parsed_cargo.error());
      }
      definition.cargo = parsed_cargo.value();
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

    if (const io::DataValue* crash = vehicle_json.get("crash"); crash != nullptr) {
      auto parsed_crash = read_crash(*crash, path + ".crash");
      if (!parsed_crash) {
        return Expected<VehicleDefinition, EntityRegistryError>::failure(parsed_crash.error());
      }
      definition.crash = parsed_crash.value();
    }
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
