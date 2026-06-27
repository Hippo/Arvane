#include "server_runtime_internal.hpp"

#include "arv/core/log.hpp"
#include "arv/core/profiling.hpp"
#include "arv/io/data_codecs.hpp"
#include "arv/io/files.hpp"
#include "arv/persistence/save.hpp"
#include "arv/text/chat_message.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace arv::server::detail {
namespace {

struct WorldRegistryIndexEntry {
  std::string name{};
  WorldID id{};
  bool load_on_start{};
  bool is_default{};
  std::string world_type{"default"};
  std::string created_at{};
};

[[nodiscard]] std::filesystem::path worlds_root_path(
    const std::filesystem::path& server_save_root) {
  return server_save_root / "worlds";
}

[[nodiscard]] std::filesystem::path world_root_path(
    const std::filesystem::path& server_save_root,
    std::string_view name) {
  return worlds_root_path(server_save_root) / std::string{name};
}

[[nodiscard]] std::filesystem::path registry_index_path(
    const std::filesystem::path& server_save_root) {
  return worlds_root_path(server_save_root) / "registry.json";
}

[[nodiscard]] const io::DataValue* member(
    const io::DataValue& object,
    std::string_view key) noexcept {
  return object.get(key);
}

[[nodiscard]] std::string string_value_or(
    const io::DataValue& object,
    std::string_view key,
    std::string fallback) {
  const io::DataValue* value = member(object, key);
  return value != nullptr && value->kind() == io::DataKind::String
             ? std::string{value->as_string_or({})}
             : std::move(fallback);
}

[[nodiscard]] bool bool_value_or(
    const io::DataValue& object,
    std::string_view key,
    bool fallback) {
  const io::DataValue* value = member(object, key);
  return value != nullptr && value->kind() == io::DataKind::Bool
             ? value->as_bool_or(fallback)
             : fallback;
}

[[nodiscard]] u64 u64_value_or(
    const io::DataValue& object,
    std::string_view key,
    u64 fallback) {
  const io::DataValue* value = member(object, key);
  return value != nullptr && io::is_integer_value(*value)
             ? value->as_u64_or(fallback)
             : fallback;
}

[[nodiscard]] f64 f64_value_or(
    const io::DataValue& object,
    std::string_view key,
    f64 fallback) {
  const io::DataValue* value = member(object, key);
  return value != nullptr && io::is_number_value(*value)
             ? value->as_f64_or(fallback)
             : fallback;
}

[[nodiscard]] std::optional<Location> location_from_value(
    const io::DataValue& node) {
  if (node.kind() != io::DataKind::Object) {
    return std::nullopt;
  }
  Location location;
  location.x = static_cast<f32>(f64_value_or(node, "x", 0.0));
  location.y = static_cast<f32>(f64_value_or(node, "y", 0.0));
  location.z = static_cast<f32>(f64_value_or(node, "z", 0.0));
  location.yaw_radians =
      static_cast<f32>(f64_value_or(node, "yaw_radians", location.yaw_radians));
  location.pitch_radians =
      static_cast<f32>(f64_value_or(node, "pitch_radians", location.pitch_radians));
  if (const io::DataValue* world = member(node, "world");
      world != nullptr && io::is_integer_value(*world)) {
    location.world = WorldID{static_cast<WorldID::value_type>(world->as_u64_or(0U))};
  }
  return location;
}

[[nodiscard]] io::DataValue location_to_value(Location location) {
  io::DataValue node = io::DataValue::object();
  node.set("x", static_cast<f64>(location.x));
  node.set("y", static_cast<f64>(location.y));
  node.set("z", static_cast<f64>(location.z));
  node.set("yaw_radians", static_cast<f64>(location.yaw_radians));
  node.set("pitch_radians", static_cast<f64>(location.pitch_radians));
  return node;
}

[[nodiscard]] std::optional<io::DataValue> read_json_value(
    const std::filesystem::path& path) {
  auto text = io::read_text_file_contents(path);
  if (!text) {
    return std::nullopt;
  }
  auto decoded = io::decode_json_value(text.value());
  if (!decoded) {
    return std::nullopt;
  }
  return std::move(decoded.value().root);
}

[[nodiscard]] bool write_json_value(
    const std::filesystem::path& path,
    const io::DataValue& value) {
  auto encoded = io::encode_json_value(value);
  if (!encoded) {
    return false;
  }
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return false;
  }
  const std::filesystem::path temp = path.string() + ".tmp";
  {
    std::ofstream output(temp, std::ios::trunc);
    if (!output) {
      return false;
    }
    output << encoded.value();
    if (!output) {
      return false;
    }
  }
  std::filesystem::rename(temp, path, error);
  if (error) {
    std::error_code cleanup_error;
    std::filesystem::remove(temp, cleanup_error);
    return false;
  }
  return true;
}

[[nodiscard]] std::string generator_id_from_world_json(
    const io::DataValue& world_json) {
  const io::DataValue* generation = member(world_json, "generation_config");
  if (generation == nullptr || generation->kind() != io::DataKind::Object) {
    return {};
  }
  std::string id = string_value_or(*generation, "id", {});
  if (!id.empty()) {
    return id;
  }
  return string_value_or(*generation, "dimension_id", {});
}

[[nodiscard]] std::optional<WorldRecord> read_world_record_from_metadata(
    const std::filesystem::path& server_save_root,
    const std::filesystem::path& world_root) {
  const std::filesystem::path world_file = world_root / "world.json";
  std::optional<io::DataValue> world_json = read_json_value(world_file);
  if (!world_json.has_value() || world_json->kind() != io::DataKind::Object) {
    return std::nullopt;
  }

  WorldRecord record;
  record.name = string_value_or(*world_json, "name", world_root.filename().string());
  if (record.name.empty()) {
    record.name = world_root.filename().string();
  }
  record.path = world_root.string();
  record.seed = u64_value_or(*world_json, "seed", 0U);
  record.generator_id = generator_id_from_world_json(*world_json);
  record.world_type = string_value_or(*world_json, "type", "default");
  if (const io::DataValue* spawn = member(*world_json, "spawn"); spawn != nullptr) {
    record.spawn = location_from_value(*spawn);
  }
  if (record.spawn.has_value()) {
    record.spawn->world = record.id;
  }
  (void)server_save_root;
  return record;
}

[[nodiscard]] std::string current_timestamp_text() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto seconds =
      std::chrono::duration_cast<std::chrono::seconds>(now).count();
  return std::to_string(seconds);
}

[[nodiscard]] u64 generated_world_seed() noexcept {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return static_cast<u64>(now == 0 ? 1 : now);
}

[[nodiscard]] std::vector<WorldRegistryIndexEntry> load_registry_index(
    const std::filesystem::path& server_save_root,
    std::optional<std::string>& default_world_name) {
  std::vector<WorldRegistryIndexEntry> entries;
  const std::optional<io::DataValue> root =
      read_json_value(registry_index_path(server_save_root));
  if (!root.has_value() || root->kind() != io::DataKind::Object) {
    return entries;
  }
  const std::string default_name = string_value_or(*root, "default_world", {});
  if (!default_name.empty()) {
    default_world_name = default_name;
  }
  const io::DataValue* worlds = member(*root, "worlds");
  const io::DataValue::Array* array =
      worlds == nullptr ? nullptr : worlds->array_if();
  if (array == nullptr) {
    return entries;
  }
  entries.reserve(array->size());
  for (const io::DataValue& entry : *array) {
    if (entry.kind() != io::DataKind::Object) {
      continue;
    }
    WorldRegistryIndexEntry out;
    out.name = string_value_or(entry, "name", {});
    if (out.name.empty()) {
      continue;
    }
    out.id = WorldID{static_cast<WorldID::value_type>(u64_value_or(entry, "id", 0U))};
    out.load_on_start = bool_value_or(entry, "load_on_start", false);
    out.is_default = bool_value_or(entry, "default", false);
    out.world_type = string_value_or(entry, "type", "default");
    out.created_at = string_value_or(entry, "created_at", {});
    entries.push_back(std::move(out));
  }
  return entries;
}

[[nodiscard]] const WorldRegistryIndexEntry* find_index_entry(
    std::span<const WorldRegistryIndexEntry> entries,
    std::string_view name) noexcept {
  for (const WorldRegistryIndexEntry& entry : entries) {
    if (entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

[[nodiscard]] bool save_registry_index(
    const std::filesystem::path& server_save_root,
    const WorldRegistry& registry) {
  if (server_save_root.empty()) {
    return true;
  }
  io::DataValue root = io::DataValue::object();
  root.set("schema_version", static_cast<u64>(1U));
  const std::optional<std::string> default_name = registry.default_world_name();
  if (default_name.has_value()) {
    root.set("default_world", default_name.value());
  }
  io::DataValue worlds = io::DataValue::array();
  for (const WorldRecord& record : registry.records()) {
    io::DataValue entry = io::DataValue::object();
    entry.set("name", record.name);
    entry.set("id", static_cast<u64>(record.id.value));
    entry.set("load_on_start", record.load_on_start);
    entry.set("default", record.is_default);
    entry.set("type", record.world_type);
    if (!record.created_at.empty()) {
      entry.set("created_at", record.created_at);
    }
    worlds.push(std::move(entry));
  }
  root.set("worlds", std::move(worlds));
  return write_json_value(registry_index_path(server_save_root), root);
}

[[nodiscard]] bool path_is_within(
    const std::filesystem::path& child,
    const std::filesystem::path& root) {
  auto child_iter = child.begin();
  auto root_iter = root.begin();
  for (; root_iter != root.end(); ++root_iter, ++child_iter) {
    if (child_iter == child.end() || *child_iter != *root_iter) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<std::filesystem::path> canonical_world_path_if_safe(
    const std::filesystem::path& server_save_root,
    std::string_view name) {
  std::error_code error;
  std::filesystem::create_directories(worlds_root_path(server_save_root), error);
  if (error) {
    return std::nullopt;
  }
  const std::filesystem::path root =
      std::filesystem::weakly_canonical(worlds_root_path(server_save_root), error);
  if (error) {
    return std::nullopt;
  }
  error.clear();
  const std::filesystem::path target =
      std::filesystem::weakly_canonical(world_root_path(server_save_root, name), error);
  if (error || !path_is_within(target, root)) {
    return std::nullopt;
  }
  return target;
}

[[nodiscard]] std::string default_generator_id(
    const std::optional<ServerChunkGenerationSettings>& generation) {
  if (!generation.has_value()) {
    return "arvane:worldgen/dimension/overworld";
  }
  if (!generation->config.id.empty()) {
    return generation->config.id;
  }
  if (!generation->config.dimension_id.empty()) {
    return generation->config.dimension_id;
  }
  return "arvane:worldgen/dimension/overworld";
}

[[nodiscard]] bool is_space_world_type(std::string_view world_type) noexcept {
  return world_type == "space";
}

[[nodiscard]] bool is_void_world_generator(std::string_view generator_id) noexcept {
  return generator_id == world::VoidWorldGenerationID;
}

void apply_runtime_world_type_defaults(
    world::WorldRuntime& runtime,
    std::string_view world_type,
    const world::WorldGenerationConfig& generation) {
  if (!is_space_world_type(world_type) && !generation.void_world &&
      generation.id != world::VoidWorldGenerationID &&
      generation.dimension_id != world::VoidWorldGenerationID) {
    return;
  }
  (void)runtime.set_game_rule_bool("allowBlockPlacement", false);
  (void)runtime.set_game_rule_bool("doMobSpawning", false);
  (void)runtime.set_game_rule_bool("doDaylightCycle", false);
  (void)runtime.set_game_rule_bool("doWeatherCycle", false);
  world::WorldEnvironmentState environment = runtime.environment();
  environment.gravity_scale = 0.0F;
  environment.has_atmosphere = false;
  environment.has_oxygen = false;
  environment.sky = "space";
  environment.ambient_light = 0.02F;
  environment.fog = "none";
  runtime.set_environment(std::move(environment));
  (void)runtime.set_weather_id("clear");
}

[[nodiscard]] std::vector<std::string> world_definition_candidates(
    const WorldRecord& record) {
  std::vector<std::string> candidates;
  if (record.name.find(':') != std::string::npos) {
    candidates.push_back(record.name);
  }
  if (is_space_world_type(record.world_type)) {
    candidates.push_back("arvane:space/" + record.name);
  }
  candidates.push_back("arvane:world/" + record.name);
  if (record.id == DefaultWorldID && record.name != "overworld") {
    candidates.push_back("arvane:world/overworld");
  }
  return candidates;
}

[[nodiscard]] const world::WorldDefinition* find_world_definition_for_record(
    const world::SpaceDefinitionRegistry* registry,
    const WorldRecord& record) {
  if (registry == nullptr) {
    return nullptr;
  }
  for (const std::string& candidate : world_definition_candidates(record)) {
    const world::WorldDefinition* definition = registry->find_world(candidate);
    if (definition != nullptr) {
      return definition;
    }
  }
  return nullptr;
}

[[nodiscard]] std::string world_runtime_name_for_definition_id(
    std::string_view definition_id) {
  const usize slash = definition_id.find_last_of('/');
  if (slash != std::string_view::npos && slash + 1U < definition_id.size()) {
    return std::string{definition_id.substr(slash + 1U)};
  }
  const usize colon = definition_id.find(':');
  if (colon != std::string_view::npos && colon + 1U < definition_id.size()) {
    return std::string{definition_id.substr(colon + 1U)};
  }
  return std::string{definition_id};
}

[[nodiscard]] WorldRecord* find_world_record_for_definition_id(
    WorldRegistry& worlds,
    std::string_view definition_id) {
  if (WorldRecord* exact = worlds.find(definition_id); exact != nullptr) {
    return exact;
  }
  const std::string runtime_name = world_runtime_name_for_definition_id(definition_id);
  if (WorldRecord* named = worlds.find(runtime_name); named != nullptr) {
    return named;
  }
  if (definition_id == "arvane:world/overworld") {
    return worlds.find(DefaultWorldID);
  }
  return nullptr;
}

[[nodiscard]] const WorldRecord* find_world_record_for_definition_id(
    const WorldRegistry& worlds,
    std::string_view definition_id) {
  if (const WorldRecord* exact = worlds.find(definition_id); exact != nullptr) {
    return exact;
  }
  const std::string runtime_name = world_runtime_name_for_definition_id(definition_id);
  if (const WorldRecord* named = worlds.find(runtime_name); named != nullptr) {
    return named;
  }
  if (definition_id == "arvane:world/overworld") {
    return worlds.find(DefaultWorldID);
  }
  return nullptr;
}

void apply_definition_environment(
    world::WorldRuntime& runtime,
    const world::SpaceDefinitionRegistry* registry,
    const WorldRecord& record) {
  const world::WorldDefinition* definition =
      find_world_definition_for_record(registry, record);
  if (definition == nullptr) {
    return;
  }
  const world::EnvironmentDefinition* environment =
      registry->find_environment(definition->environment);
  if (environment == nullptr) {
    return;
  }
  runtime.set_environment({
      std::max(0.0F, environment->gravity_scale),
      environment->has_atmosphere,
      environment->has_oxygen,
      environment->sky,
      environment->ambient_light,
      environment->fog});
  if (definition->kind == world::WorldDefinitionKind::Space) {
    (void)runtime.set_game_rule_bool(
        "allowBlockPlacement",
        definition->rules.allow_block_placement);
    (void)runtime.set_game_rule_bool("doMobSpawning", false);
    (void)runtime.set_game_rule_bool("doWeatherCycle", false);
  }
}

[[nodiscard]] u32 player_count_for_world(
    const SessionRegistry& sessions,
    WorldID world_id) {
  u32 count = 0U;
  sessions.for_each_session([&](const ServerSession& session) {
    if (session.authenticated && !session.closing && session.world_id == world_id) {
      ++count;
    }
  });
  return count;
}

[[nodiscard]] command::WorldCommandInfo command_info_from_record(
    const WorldRecord& record,
    const SessionRegistry& sessions) {
  command::WorldCommandInfo info;
  info.id = record.id;
  info.name = record.name;
  info.path = record.path;
  info.loaded = record.state == WorldRuntimeState::Running ||
                record.state == WorldRuntimeState::Loading;
  info.is_default = record.is_default;
  info.load_on_start = record.load_on_start;
  info.player_count = player_count_for_world(sessions, record.id);
  info.seed = record.seed;
  info.seed_visible = true;
  info.generator_id = record.generator_id;
  info.world_type = record.world_type;
  info.spawn = record.spawn;
  info.created_at = record.created_at;
  return info;
}

[[nodiscard]] command::WorldOperationCommandResult op_failure(std::string message) {
  command::WorldOperationCommandResult result;
  result.message = std::move(message);
  return result;
}

[[nodiscard]] command::WorldOperationCommandResult op_success(std::string message) {
  command::WorldOperationCommandResult result;
  result.success = true;
  result.message = std::move(message);
  return result;
}

[[nodiscard]] WorldTransitionResult transition_failure(std::string message) {
  WorldTransitionResult result;
  result.message = std::move(message);
  return result;
}

[[nodiscard]] WorldTransitionResult transition_success(
    std::string message,
    std::vector<PlayerID> moved_players,
    EntityID moved_root_entity) {
  WorldTransitionResult result;
  result.success = true;
  result.message = std::move(message);
  result.moved_players = std::move(moved_players);
  result.moved_root_entity = moved_root_entity;
  return result;
}

[[nodiscard]] ChunkSectionCoord chunk_section_for_position(Vec3f position) noexcept {
  return {{
      floor_div(static_cast<i32>(std::floor(position.x)), ChunkSize),
      floor_div(static_cast<i32>(std::floor(position.y)), ChunkSize),
      floor_div(static_cast<i32>(std::floor(position.z)), ChunkSize),
  }};
}

[[nodiscard]] ChunkSectionCoord transition_load_section_for_position(
    const world::WorldRuntime& runtime,
    Vec3f position) noexcept {
  ChunkSectionCoord coord = chunk_section_for_position(position);
  const world::WorldHeightSettings height = runtime.height_settings();
  coord.value.y = std::clamp(coord.value.y, height.min_chunk_y, height.max_chunk_y);
  return coord;
}

[[nodiscard]] Vec3f clamp_planet_entry_position_to_world_height(
    const world::WorldRuntime& runtime,
    Vec3f position) noexcept {
  const world::WorldHeightSettings height = runtime.height_settings();
  const f32 min_y = static_cast<f32>(height.min_chunk_y * ChunkSize) + 1.0F;
  f32 max_y = static_cast<f32>(height.max_chunk_y * ChunkSize) - 4.0F;
  if (max_y < min_y) {
    max_y = min_y;
  }
  position.y = std::clamp(position.y, min_y, max_y);
  return position;
}

struct EntityTransferSnapshot {
  EntityID source_entity{};
  std::optional<EntityNetID> source_net_id{};
  world::EntityCreateInfo create_info{};
  std::optional<entity::VehiclePhysicsComponent> vehicle_physics{};
  std::optional<entity::VehicleCargoComponent> vehicle_cargo{};
};

struct PlayerTransitionMember {
  ServerSession* session{};
  EntityID source_entity{};
  player::PlayerState source_state{};
  std::optional<std::string> seat_id{};
};

struct TransitionGroup {
  EntityID root_entity{};
  bool root_is_vehicle{};
  std::optional<EntityTransferSnapshot> vehicle{};
  std::vector<PlayerTransitionMember> players{};
};

[[nodiscard]] ServerSession* find_session_for_player(
    SessionRegistry& sessions,
    PlayerID player_id) {
  for (const SessionID session_id : sessions.session_ids()) {
    ServerSession* session = sessions.find(session_id);
    if (session != nullptr && session->player_id == player_id) {
      return session;
    }
  }
  return nullptr;
}

[[nodiscard]] bool vehicle_definition_has_tag(
    const entity::VehicleDefinition& definition,
    std::string_view tag) noexcept {
  return std::find(
             definition.tags.begin(),
             definition.tags.end(),
             std::string{tag}) != definition.tags.end();
}

[[nodiscard]] bool vehicle_satisfies_required_tags(
    const world::WorldRuntime& runtime,
    EntityID vehicle_entity,
    const entity::VehicleDefinition& definition,
    std::span<const std::string> required_tags) noexcept {
  for (const std::string& tag : required_tags) {
    if (!vehicle_definition_has_tag(definition, tag) &&
        !runtime.entity_has_tag(vehicle_entity, tag)) {
      return false;
    }
  }
  return true;
}

void ensure_launch_vehicle_fuel_state(
    entity::VehiclePhysicsComponent& physics,
    const entity::VehicleDefinition& definition) noexcept {
  if (!definition.fuel.has_value() || physics.fuel_capacity_seconds > 0.0F) {
    return;
  }
  physics.fuel_item = definition.fuel->item;
  physics.fuel_capacity_seconds = definition.fuel->capacity_seconds;
  physics.fuel_seconds = definition.fuel->initial_seconds;
  physics.fuel_burn_per_second = definition.fuel->burn_per_second;
}

[[nodiscard]] bool vehicle_has_launch_fuel(
    entity::VehiclePhysicsComponent& physics,
    const entity::VehicleDefinition& definition) noexcept {
  if (!definition.fuel.has_value()) {
    return false;
  }
  ensure_launch_vehicle_fuel_state(physics, definition);
  return physics.fuel_seconds + 0.001F >=
         std::max(0.0F, definition.fuel->min_fuel_for_launch_seconds);
}

[[nodiscard]] Vec3f vehicle_launch_velocity(
    const world::WorldRuntime& runtime,
    EntityID vehicle_entity,
    const entity::VehiclePhysicsComponent& physics) noexcept {
  if (finite(physics.linear_velocity)) {
    return physics.linear_velocity;
  }
  if (const entity::EntityVelocity* velocity =
          runtime.find_entity_velocity(vehicle_entity);
      velocity != nullptr && finite(velocity->linear)) {
    return velocity->linear;
  }
  return {};
}

[[nodiscard]] std::optional<PlayerID> primary_player_for_occupied_vehicle(
    world::WorldRuntime& runtime,
    SessionRegistry& sessions,
    EntityID vehicle_entity) {
  const entity::RideableComponent* rideable =
      runtime.find_entity_rideable(vehicle_entity);
  if (rideable == nullptr) {
    return std::nullopt;
  }
  for (const entity::Seat& seat : rideable->seats) {
    if (seat.occupant.value == 0U) {
      continue;
    }
    const std::optional<PlayerID> player_id =
        runtime.player_for_entity(seat.occupant);
    if (!player_id.has_value()) {
      continue;
    }
    ServerSession* session = find_session_for_player(sessions, player_id.value());
    if (session != nullptr && session->authenticated &&
        session->world_id == runtime.id()) {
      return player_id;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<EntityTransferSnapshot> snapshot_entity_for_transition(
    world::WorldRuntime& source_world,
    EntityID entity_id,
    Vec3f target_position,
    Vec3f target_velocity,
    Quatf target_rotation,
    const entity::VehicleDefinitionRegistry* vehicle_registry) {
  const entity::EntityType* type = source_world.find_entity_type(entity_id);
  const entity::EntityTransform* transform = source_world.find_entity_transform(entity_id);
  if (type == nullptr || transform == nullptr) {
    return std::nullopt;
  }

  EntityTransferSnapshot snapshot;
  snapshot.source_entity = entity_id;
  snapshot.source_net_id = source_world.network_id_for_entity(entity_id);
  snapshot.create_info.type_id = type->type_id;
  snapshot.create_info.type_name = type->type_name;
  snapshot.create_info.position = target_position;
  snapshot.create_info.rotation = target_rotation;
  snapshot.create_info.velocity = target_velocity;
  snapshot.create_info.replicated = snapshot.source_net_id.has_value();
  snapshot.create_info.persistent = source_world.entity_is_persistent(entity_id);
  snapshot.create_info.persistent_uuid =
      source_world.persistent_entity_uuid(entity_id).value_or(std::string{});
  snapshot.create_info.vehicle_definitions = vehicle_registry;

  entity::EntityComponentSet components;
  bool has_components = false;
  if (const entity::EntityCollision* collision =
          source_world.find_entity_collision(entity_id);
      collision != nullptr) {
    components.collision = entity::AuthoredCollisionComponent{collision->box};
    has_components = true;
  }
  if (const entity::EntityMovement* movement =
          source_world.find_entity_movement(entity_id);
      movement != nullptr) {
    components.movement = entity::AuthoredMovementComponent{movement->walk_speed};
    has_components = true;
  }
  if (const entity::EntityHealth* health =
          source_world.find_entity_health(entity_id);
      health != nullptr) {
    components.health =
        entity::AuthoredHealthComponent{health->max, health->current, health->invulnerable};
    has_components = true;
  }
  if (const entity::KnockbackResistanceComponent* resistance =
          source_world.find_entity_knockback_resistance(entity_id);
      resistance != nullptr) {
    components.knockback_resistance =
        entity::AuthoredKnockbackResistanceComponent{resistance->resistance};
    has_components = true;
  }
  if (const entity::RideableComponent* rideable =
          source_world.find_entity_rideable(entity_id);
      rideable != nullptr && !rideable->definition.empty()) {
    components.rideable = entity::AuthoredRideableComponent{rideable->definition};
    has_components = true;
  }
  if (const entity::VehiclePhysicsComponent* vehicle_physics =
          source_world.find_entity_vehicle_physics(entity_id);
      vehicle_physics != nullptr && !vehicle_physics->definition.empty()) {
    components.vehicle_physics =
        entity::AuthoredVehiclePhysicsComponent{vehicle_physics->definition};
    snapshot.vehicle_physics = *vehicle_physics;
    has_components = true;
  }
  if (const entity::VehicleCargoComponent* vehicle_cargo =
          source_world.find_entity_vehicle_cargo(entity_id);
      vehicle_cargo != nullptr) {
    snapshot.vehicle_cargo = *vehicle_cargo;
  }
  if (const entity::DroppedItem* dropped = source_world.find_dropped_item(entity_id);
      dropped != nullptr) {
    snapshot.create_info.dropped_item_stack = dropped->stack;
    snapshot.create_info.dropped_item_pickup_delay_seconds =
        dropped->pickup_delay_seconds;
  }
  if (has_components) {
    snapshot.create_info.authored_components = std::move(components);
  }
  return snapshot;
}

[[nodiscard]] bool update_world_json_name(
    const std::filesystem::path& world_root,
    std::string_view name) {
  const std::filesystem::path world_file = world_root / "world.json";
  std::optional<io::DataValue> json = read_json_value(world_file);
  if (!json.has_value() || json->kind() != io::DataKind::Object) {
    return false;
  }
  json->set("name", std::string{name});
  return write_json_value(world_file, json.value());
}

[[nodiscard]] bool update_world_json_spawn(
    const std::filesystem::path& world_root,
    std::string_view name,
    u64 seed,
    std::string_view generator_id,
    Location spawn) {
  const std::filesystem::path world_file = world_root / "world.json";
  std::optional<io::DataValue> json = read_json_value(world_file);
  if (!json.has_value() || json->kind() != io::DataKind::Object) {
    json = io::DataValue::object();
    json->set("name", std::string{name});
    json->set("seed", seed);
    if (!generator_id.empty()) {
      io::DataValue generation = io::DataValue::object();
      generation.set("id", std::string{generator_id});
      generation.set("dimension_id", std::string{generator_id});
      json->set("generation_config", std::move(generation));
    }
  }
  json->set("spawn", location_to_value(spawn));
  return write_json_value(world_file, json.value());
}

[[nodiscard]] persistence::PlayerSaveData to_persistence_player_save(
    const ServerPlayerSaveState& player) {
  persistence::PlayerSaveData save;
  save.profile_id = player.profile_id;
  save.profile_name = player.profile_name;
  save.display_name = player.display_name;
  save.world_name = player.world_name;
  save.position = player.position;
  save.velocity = player.velocity;
  save.yaw_radians = player.yaw_radians;
  save.pitch_radians = player.pitch_radians;
  save.grounded = player.grounded;
  save.movement_mode = player.movement_mode;
  save.game_mode_id = player.game_mode_id;
  save.selected_skin_hash = player.selected_skin_hash;
  save.selected_skin_slim_model = player.selected_skin_slim_model;
  save.respawn_location = player.respawn_location;
  save.selected_hotbar_slot = player.selected_hotbar_slot;
  save.inventory = player.inventory;
  save.unlocked_recipe_ids = player.unlocked_recipe_ids;
  return save;
}

[[nodiscard]] persistence::PlayerSaveData to_persistence_player_save(
    const ServerSession& session,
    const player::PlayerState& player_state,
    std::string_view world_name) {
  persistence::PlayerSaveData save;
  save.profile_id = session.profile_id;
  save.profile_name = session.profile_name;
  save.display_name = session.display_name;
  save.world_name = std::string{world_name};
  save.position = player_state.position;
  save.velocity = player_state.velocity;
  save.yaw_radians = player_state.yaw_radians;
  save.pitch_radians = player_state.pitch_radians;
  save.grounded = player_state.grounded;
  save.movement_mode = player_state.movement_mode;
  save.game_mode_id = player_state.game_mode_id;
  save.respawn_location = player_state.respawn_location;
  save.selected_hotbar_slot = player_state.selected_hotbar_slot;
  save.inventory = player_state.inventory;
  save.unlocked_recipe_ids = player_state.unlocked_recipe_ids;
  return save;
}

[[nodiscard]] std::vector<persistence::SavedChunkSection> capture_saved_chunks(
    const world::WorldRuntime& world) {
  std::vector<ChunkSectionCoord> coords = world.loaded_chunk_coords();
  std::sort(coords.begin(), coords.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.value.x != rhs.value.x) {
      return lhs.value.x < rhs.value.x;
    }
    if (lhs.value.z != rhs.value.z) {
      return lhs.value.z < rhs.value.z;
    }
    return lhs.value.y < rhs.value.y;
  });

  std::vector<persistence::SavedChunkSection> chunks;
  chunks.reserve(coords.size());
  for (const ChunkSectionCoord coord : coords) {
    const voxel::ChunkSection* chunk = world.find_chunk(coord);
    if (chunk == nullptr) {
      continue;
    }
    persistence::SavedChunkSection saved;
    saved.coord = coord;
    saved.blocks = chunk->blocks();
    saved.lights = chunk->lights();
    saved.fluids = chunk->fluids();
    saved.lighting_valid = !chunk->is_dirty(voxel::ChunkDirtyFlags::Lighting);
    saved.block_containers = world.saved_block_containers(coord);
    chunks.push_back(std::move(saved));
  }
  return chunks;
}

[[nodiscard]] std::vector<persistence::PlayerSaveData> collect_player_saves(
    const ServerPlayerSaveCache& player_saves,
    const SessionRegistry& sessions,
    const WorldRegistry& worlds,
    const ServerRuntimeImpl& runtime,
    std::string_view fallback_world_name) {
  std::vector<persistence::PlayerSaveData> players;
  players.reserve(player_saves.states().size() + sessions.size());
  for (const ServerPlayerSaveState& saved : player_saves.states()) {
    persistence::upsert_player_save(players, to_persistence_player_save(saved));
  }
  for (const SessionID session_id : sessions.session_ids()) {
    const ServerSession* session = sessions.find(session_id);
    if (session == nullptr) {
      continue;
    }
    const world::WorldRuntime* session_world = runtime.world_for_session(*session);
    if (session_world == nullptr) {
      continue;
    }
    const player::PlayerState* player_state =
        session_world->find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }
    const WorldRecord* record = worlds.find(session->world_id);
    const std::string_view world_name =
        record == nullptr || record->name.empty()
            ? fallback_world_name
            : std::string_view{record->name};
    persistence::upsert_player_save(
        players,
        to_persistence_player_save(*session, *player_state, world_name));
  }
  std::sort(players.begin(), players.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.world_name != rhs.world_name) {
      return lhs.world_name < rhs.world_name;
    }
    if (lhs.profile_id != rhs.profile_id) {
      return lhs.profile_id < rhs.profile_id;
    }
    return lhs.profile_name < rhs.profile_name;
  });
  return players;
}

void restore_loaded_save_into_world(
    world::WorldRuntime& runtime,
    const persistence::LoadedSaveData& loaded_save,
    TickRate base_tps,
    const entity::EntityRegistry* entity_registry,
    const entity::VehicleDefinitionRegistry* vehicle_registry) {
  if (loaded_save.has_world_metadata) {
    runtime.set_clock(loaded_save.world.clock, base_tps);
  }
  for (const persistence::SavedChunkSection& chunk : loaded_save.chunks) {
    runtime.restore_saved_chunk(
        chunk.coord,
        chunk.blocks,
        chunk.lights,
        chunk.fluids,
        chunk.lighting_valid,
        chunk.block_containers);
  }
  runtime.restore_persistent_entities(
      loaded_save.entities,
      entity_registry,
      vehicle_registry);
}

} // namespace

void ServerRuntimeImpl::initialize_world_registry(const ServerStartInfo& start_info) {
  worlds_.clear();
  std::optional<std::string> indexed_default_name;
  const std::vector<WorldRegistryIndexEntry> index_entries =
      load_registry_index(server_save_root_, indexed_default_name);
  const std::string configured_default =
      world_name_.empty() ? std::string{"default"} : world_name_;
  const std::string default_name =
      indexed_default_name.value_or(configured_default);

  std::error_code error;
  const std::filesystem::path worlds_root = worlds_root_path(server_save_root_);
  if (!server_save_root_.empty() && std::filesystem::is_directory(worlds_root, error)) {
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(worlds_root, error)) {
      if (error || !entry.is_directory()) {
        error.clear();
        continue;
      }
      std::optional<WorldRecord> record =
          read_world_record_from_metadata(server_save_root_, entry.path());
      if (!record.has_value()) {
        continue;
      }
      const WorldRegistryIndexEntry* index =
          find_index_entry(index_entries, record->name);
      record->id = index == nullptr ? WorldID{} : index->id;
      if (record->name == configured_default || record->id == DefaultWorldID) {
        record->id = DefaultWorldID;
      }
      if (record->id.value == 0U || worlds_.find(record->id) != nullptr) {
        record->id = worlds_.next_available_id();
      }
      record->state =
          record->id == DefaultWorldID ? WorldRuntimeState::Running
                                       : WorldRuntimeState::Stopped;
      record->load_on_start = index == nullptr ? record->id == DefaultWorldID
                                               : index->load_on_start;
      record->is_default = record->name == default_name ||
                           (index != nullptr && index->is_default);
      if (index != nullptr) {
        record->world_type = index->world_type;
        record->created_at = index->created_at;
      }
      if (record->spawn.has_value()) {
        record->spawn->world = record->id;
      }
      (void)worlds_.register_world(std::move(record.value()));
    }
  }

  if (worlds_.find(DefaultWorldID) == nullptr) {
    WorldRecord active;
    active.id = DefaultWorldID;
    active.name = configured_default;
    active.state = WorldRuntimeState::Running;
    active.version = 1U;
    active.path = world_root_path(server_save_root_, configured_default).string();
    active.seed = chunk_generation_.has_value() ? chunk_generation_->seed : 0U;
    active.generator_id = default_generator_id(chunk_generation_);
    active.world_type = "default";
    active.spawn = default_world_.spawn_point();
    active.spawn->world = DefaultWorldID;
    active.load_on_start = true;
    active.is_default = configured_default == default_name;
    const WorldRegistryIndexEntry* index =
        find_index_entry(index_entries, configured_default);
    if (index != nullptr) {
      active.created_at = index->created_at;
    }
    (void)worlds_.register_world(std::move(active));
  }

  if (!worlds_.set_default(default_name)) {
    (void)worlds_.set_default(configured_default);
  }
  if (WorldRecord* active = worlds_.find(DefaultWorldID); active != nullptr) {
    active->state = WorldRuntimeState::Running;
    active->name = configured_default;
    active->path = world_root_path(server_save_root_, configured_default).string();
    active->seed = chunk_generation_.has_value() ? chunk_generation_->seed : active->seed;
    active->generator_id = default_generator_id(chunk_generation_);
    active->spawn = default_world_.spawn_point();
    active->spawn->world = DefaultWorldID;
  }
  if (!save_registry_index(server_save_root_, worlds_)) {
    core::warn("worlds", "failed to persist world registry index");
  }
  (void)start_info;
}

std::vector<command::WorldCommandInfo> ServerRuntimeImpl::list_worlds_for_command() const {
  std::vector<command::WorldCommandInfo> out;
  for (const WorldRecord& record : worlds_.records()) {
    out.push_back(command_info_from_record(record, sessions_));
  }
  return out;
}

std::optional<command::WorldCommandInfo> ServerRuntimeImpl::world_info_for_command(
    std::string_view name) const {
  const WorldRecord* record = worlds_.find(name);
  if (record == nullptr) {
    return std::nullopt;
  }
  return command_info_from_record(*record, sessions_);
}

std::optional<std::string> ServerRuntimeImpl::default_world_name_for_command() const {
  return worlds_.default_world_name();
}

command::SaveAllCommandResult ServerRuntimeImpl::save_all_worlds_from_command(bool flush) {
  command::SaveAllCommandResult out;
  out.flushed = flush;
  if (server_save_root_.empty()) {
    out.message = "Server save root is not configured.";
    return out;
  }
  if (block_registry_ == nullptr || item_registry_ == nullptr) {
    out.message = "Cannot save without block and item registries.";
    return out;
  }

  const std::vector<persistence::PlayerSaveData> players =
      collect_player_saves(
          player_saves_,
          sessions_,
          worlds_,
          *this,
          world_name_);
  const persistence::SaveMetadata save_metadata{
      persistence::CurrentSaveFormatVersion,
      std::string{persistence::CurrentGameVersion},
      persistence::hash_block_registry(*block_registry_),
      persistence::hash_item_registry(*item_registry_),
      server_save_display_name_};

  for (world::WorldRuntime* runtime : loaded_worlds()) {
    if (runtime == nullptr) {
      continue;
    }
    const WorldRecord* record = worlds_.find(runtime->id());
    if (record == nullptr) {
      out.message = "Cannot save an unregistered loaded world.";
      return out;
    }
    const std::vector<persistence::SavedChunkSection> chunks =
        capture_saved_chunks(*runtime);
    const std::vector<entity::PersistentEntityRecord> entities =
        runtime->persistent_entity_records();
    const world::WorldGenerationConfig* active_generation =
        runtime->generation_config();
    const world::WorldHeightSettings height = runtime->height_settings();
    persistence::WorldMetadata world_metadata{
        record->name,
        runtime->generation_seed(),
        runtime->tick_index(),
        active_generation == nullptr ? world::WorldGenerationConfig{}
                                     : *active_generation,
        height.min_chunk_y,
        height.max_chunk_y,
        persistence::hash_block_registry(*block_registry_),
        persistence::hash_item_registry(*item_registry_),
        runtime->clock(),
        {},
        true,
        runtime->spawn_point()};
    auto saved = persistence::save_server_root(
        server_save_root_,
        save_metadata,
        world_metadata,
        chunks,
        players,
        entities,
        *block_registry_,
        *item_registry_);
    if (!saved) {
      out.message = saved.error().message;
      return out;
    }
    out.saved_chunks += static_cast<u32>(chunks.size());
    out.saved_entities += static_cast<u32>(entities.size());
  }

  out.success = true;
  out.saved_players = static_cast<u32>(players.size());
  return out;
}

command::WorldOperationCommandResult ServerRuntimeImpl::create_world_from_command(
    const command::WorldCreateCommandRequest& request) {
  if (server_save_root_.empty()) {
    return op_failure("Server save root is not configured.");
  }
  if (block_registry_ == nullptr || item_registry_ == nullptr) {
    return op_failure("Cannot create a world without block and item registries.");
  }
  if (worlds_.find(request.name) != nullptr) {
    return op_failure("World '" + request.name + "' already exists.");
  }
  const std::optional<std::filesystem::path> safe_path =
      canonical_world_path_if_safe(server_save_root_, request.name);
  if (!safe_path.has_value()) {
    return op_failure("World path is not safely contained by the server worlds directory.");
  }
  std::error_code error;
  if (std::filesystem::exists(safe_path.value(), error)) {
    return op_failure("World directory already exists for '" + request.name + "'.");
  }

  const std::string active_generator = default_generator_id(chunk_generation_);
  const bool explicit_void_generator =
      request.generator_id.has_value() &&
      is_void_world_generator(request.generator_id.value());
  const std::string world_type =
      request.world_type.value_or(explicit_void_generator ? "space" : "default");
  const bool space_world = is_space_world_type(world_type);
  if (space_world && request.generator_id.has_value() && !explicit_void_generator) {
    return op_failure(
        "Space worlds use generator '" +
        std::string{world::VoidWorldGenerationID} + "'.");
  }
  if (request.generator_id.has_value() &&
      request.generator_id.value() != active_generator &&
      !explicit_void_generator) {
    return op_failure(
        "Unknown or unavailable world generator '" + request.generator_id.value() + "'.");
  }
  world::WorldGenerationConfig generation =
      space_world || explicit_void_generator
          ? world::make_void_world_generation_config()
          : (chunk_generation_.has_value() ? chunk_generation_->config
                                           : world::WorldGenerationConfig{});
  const std::string generator_id =
      space_world || explicit_void_generator ? std::string{world::VoidWorldGenerationID}
                                             : active_generator;
  const u64 seed = request.seed.value_or(generated_world_seed());
  const Location spawn = world::generate_world_spawn_location(generation, seed);

  persistence::SaveMetadata save_metadata{
      persistence::CurrentSaveFormatVersion,
      std::string{persistence::CurrentGameVersion},
      0U,
      0U,
      server_save_display_name_};
  persistence::WorldMetadata world_metadata;
  world_metadata.name = request.name;
  world_metadata.seed = seed;
  world_metadata.generation_config = generation;
  const world::WorldHeightSettings height =
      world::world_height_settings_from_generation_config(generation);
  world_metadata.min_chunk_y = height.min_chunk_y;
  world_metadata.max_chunk_y = height.max_chunk_y;
  world_metadata.spawn_point = spawn;
  auto saved = persistence::save_server_root(
      server_save_root_,
      save_metadata,
      world_metadata,
      std::span<const persistence::SavedChunkSection>{},
      std::span<const persistence::PlayerSaveData>{},
      std::span<const entity::PersistentEntityRecord>{},
      *block_registry_,
      *item_registry_);
  if (!saved) {
    return op_failure(saved.error().message);
  }

  WorldRecord record;
  record.id = worlds_.next_available_id();
  if (record.id == DefaultWorldID) {
    record.id = WorldID{DefaultWorldID.value + 1U};
  }
  record.name = request.name;
  record.state = WorldRuntimeState::Stopped;
  record.version = 1U;
  record.path = world_root_path(server_save_root_, request.name).string();
  record.seed = seed;
  record.generator_id = generator_id;
  record.world_type = world_type;
  record.spawn = spawn;
  record.spawn->world = record.id;
  record.load_on_start = false;
  record.is_default = false;
  record.created_at = current_timestamp_text();
  if (!worlds_.register_world(std::move(record))) {
    return op_failure("Unable to register world '" + request.name + "'.");
  }
  if (!save_registry_index(server_save_root_, worlds_)) {
    return op_failure("Created world, but failed to persist world registry metadata.");
  }
  return op_success("Created world '" + request.name + "'.");
}

command::WorldOperationCommandResult ServerRuntimeImpl::load_world_from_command(
    std::string_view name) {
  WorldRecord* record = worlds_.find(name);
  if (record == nullptr) {
    return op_failure("Unknown world '" + std::string{name} + "'. Use /world list to see available worlds.");
  }
  if (record->state == WorldRuntimeState::Running) {
    return op_failure("World '" + record->name + "' is already loaded.");
  }
  if (block_registry_ == nullptr || item_registry_ == nullptr) {
    return op_failure("Cannot load a world without block and item registries.");
  }
  const std::optional<std::filesystem::path> safe_path =
      canonical_world_path_if_safe(server_save_root_, record->name);
  if (!safe_path.has_value()) {
    return op_failure("World path is not safely contained by the server worlds directory.");
  }

  auto loaded_save_result = persistence::load_server_root(
      server_save_root_,
      record->name,
      *block_registry_,
      *item_registry_);
  if (!loaded_save_result) {
    return op_failure(loaded_save_result.error().message);
  }
  persistence::LoadedSaveData loaded_save = std::move(loaded_save_result.value());
  if (!loaded_save.has_world_metadata) {
    return op_failure("World metadata was not found for '" + record->name + "'.");
  }

  auto runtime = std::make_unique<world::WorldRuntime>(record->id);
  world::WorldGenerationConfig generation = loaded_save.world.generation_config;
  if (is_space_world_type(record->world_type) ||
      generation.void_world ||
      generation.id == world::VoidWorldGenerationID ||
      generation.dimension_id == world::VoidWorldGenerationID) {
    generation = world::make_void_world_generation_config();
  }
  if (chunk_generation_.has_value()) {
    generation = world::refresh_generation_config_for_current_dimension(
        generation,
        chunk_generation_->config);
  }
  const u64 seed = loaded_save.world.seed == 0U
                       ? (record->seed == 0U ? generated_world_seed() : record->seed)
                       : loaded_save.world.seed;
  runtime->set_generation_context(seed, generation);
  runtime->configure_height(world::world_height_settings_from_generation_config(generation));
  apply_runtime_world_type_defaults(*runtime, record->world_type, generation);
  apply_definition_environment(*runtime, space_definitions_, *record);
  Location spawn =
      loaded_save.world.spawn_point.value_or(
          record->spawn.value_or(
              world::generate_world_spawn_location(generation, seed)));
  spawn.world = record->id;
  runtime->set_spawn_point(spawn);
  runtime->set_tick_index(tick_index_);
  runtime->set_tick_policy(world_tick_policy_);
  restore_loaded_save_into_world(
      *runtime,
      loaded_save,
      base_tps_,
      entity_registry_,
      vehicle_registry_);
  runtime->set_tick_index(tick_index_);

  loaded_worlds_[record->id] = std::move(runtime);
  record->state = WorldRuntimeState::Running;
  record->load_on_start = true;
  record->seed = seed;
  record->spawn = spawn;
  record->path = world_root_path(server_save_root_, record->name).string();
  record->generator_id = generator_id_from_world_json(
      read_json_value(world_root_path(server_save_root_, record->name) / "world.json")
          .value_or(io::DataValue::object()));
  if (is_space_world_type(record->world_type) || generation.void_world) {
    record->generator_id = std::string{world::VoidWorldGenerationID};
  }
  if (record->generator_id.empty()) {
    record->generator_id = default_generator_id(chunk_generation_);
  }
  if (!save_registry_index(server_save_root_, worlds_)) {
    return op_failure("Loaded world, but failed to persist world registry metadata.");
  }
  core::info(
      "worlds",
      "loaded world '{}' id={} seed={}",
      record->name,
      record->id.value,
      seed);
  for (const SpaceBodyRuntimeView& body : space_bodies_for_world(record->id)) {
    core::info(
        "space",
        "body '{}' in world '{}' target='{}' pos=({}, {}, {}) visual_radius={} entry_radius={} entry_enabled={}",
        body.body_id,
        record->name,
        body.target_world_id,
        body.position.x,
        body.position.y,
        body.position.z,
        body.visual_radius,
        body.entry_radius,
        body.entry_enabled);
  }
  return op_success("Loaded world '" + record->name + "'.");
}

command::WorldOperationCommandResult ServerRuntimeImpl::unload_world_from_command(
    std::string_view name,
    bool force) {
  WorldRecord* record = worlds_.find(name);
  if (record == nullptr) {
    return op_failure("Unknown world '" + std::string{name} + "'. Use /world list to see available worlds.");
  }
  if (record->is_default || record->id == DefaultWorldID) {
    return op_failure("Cannot unload the current default world. Change the default world first.");
  }
  if (record->state != WorldRuntimeState::Running) {
    return op_failure("World '" + record->name + "' is already unloaded.");
  }
  if (player_count_for_world(sessions_, record->id) > 0U && !force) {
    return op_failure("World '" + record->name + "' contains players. Use --force to move them before unloading.");
  }
  if (force) {
    const std::optional<std::string> default_name = worlds_.default_world_name();
    const WorldRecord* default_record =
        default_name.has_value() ? worlds_.find(default_name.value())
                                 : worlds_.find(DefaultWorldID);
    if (default_record == nullptr ||
        default_record->id == record->id ||
        world_by_id(default_record->id) == nullptr) {
      return op_failure("Cannot force-unload world '" + record->name + "' because the default world is not loaded.");
    }
    std::vector<PlayerID> players_to_move;
    sessions_.for_each_session([&](const ServerSession& session) {
      if (session.authenticated && !session.closing && session.world_id == record->id) {
        players_to_move.push_back(session.player_id);
      }
    });
    for (const PlayerID player_id : players_to_move) {
      command::WorldOperationCommandResult moved =
          teleport_player_to_world_spawn_from_command(player_id, default_record->name);
      if (!moved.success) {
        return op_failure("Failed to move players out of world '" + record->name + "': " + moved.message);
      }
    }
  }
  command::SaveAllCommandResult saved = save_all_worlds_from_command(false);
  if (!saved.success) {
    return op_failure("Failed to save world '" + record->name + "' before unload: " + saved.message);
  }
  loaded_worlds_.erase(record->id);
  record->state = WorldRuntimeState::Stopped;
  record->load_on_start = false;
  if (!save_registry_index(server_save_root_, worlds_)) {
    return op_failure("Unloaded world, but failed to persist world registry metadata.");
  }
  core::info(
      "worlds",
      "unloaded world '{}' id={} force={}",
      record->name,
      record->id.value,
      force);
  return op_success("Unloaded world '" + record->name + "'.");
}

command::WorldOperationCommandResult ServerRuntimeImpl::delete_world_from_command(
    std::string_view name) {
  const WorldRecord* record = worlds_.find(name);
  if (record == nullptr) {
    return op_failure("Unknown world '" + std::string{name} + "'. Use /world list to see available worlds.");
  }
  if (record->is_default || record->id == DefaultWorldID) {
    return op_failure("Cannot delete the default world. Change the default world first.");
  }
  if (record->state == WorldRuntimeState::Running) {
    return op_failure("World '" + record->name + "' is loaded. Unload it before deleting.");
  }
  const std::optional<std::filesystem::path> safe_path =
      canonical_world_path_if_safe(server_save_root_, record->name);
  if (!safe_path.has_value()) {
    return op_failure("World path is not safely contained by the server worlds directory.");
  }
  std::error_code error;
  if (!std::filesystem::exists(safe_path.value(), error)) {
    return op_failure("World directory does not exist for '" + record->name + "'.");
  }
  std::filesystem::remove_all(safe_path.value(), error);
  if (error) {
    return op_failure("Failed to delete world '" + record->name + "': " + error.message());
  }
  const std::string deleted_name = record->name;
  (void)worlds_.erase(name);
  if (!save_registry_index(server_save_root_, worlds_)) {
    return op_failure("Deleted world, but failed to persist world registry metadata.");
  }
  return op_success("Deleted world '" + deleted_name + "'.");
}

command::WorldOperationCommandResult ServerRuntimeImpl::rename_world_from_command(
    std::string_view old_name,
    std::string_view new_name) {
  WorldRecord* record = worlds_.find(old_name);
  if (record == nullptr) {
    return op_failure("Unknown world '" + std::string{old_name} + "'. Use /world list to see available worlds.");
  }
  if (record->is_default || record->state == WorldRuntimeState::Running) {
    return op_failure("Cannot rename a loaded or default world.");
  }
  if (worlds_.find(new_name) != nullptr) {
    return op_failure("World '" + std::string{new_name} + "' already exists.");
  }
  const std::optional<std::filesystem::path> old_path =
      canonical_world_path_if_safe(server_save_root_, old_name);
  const std::optional<std::filesystem::path> new_path =
      canonical_world_path_if_safe(server_save_root_, new_name);
  if (!old_path.has_value() || !new_path.has_value()) {
    return op_failure("World path is not safely contained by the server worlds directory.");
  }
  std::error_code error;
  if (std::filesystem::exists(new_path.value(), error)) {
    return op_failure("World directory already exists for '" + std::string{new_name} + "'.");
  }
  std::filesystem::rename(old_path.value(), new_path.value(), error);
  if (error) {
    return op_failure("Failed to rename world directory: " + error.message());
  }
  if (!update_world_json_name(new_path.value(), new_name)) {
    return op_failure("Renamed directory, but failed to update world metadata.");
  }
  (void)worlds_.rename(old_name, std::string{new_name});
  if (WorldRecord* renamed = worlds_.find(new_name); renamed != nullptr) {
    renamed->path = new_path->string();
  }
  if (!save_registry_index(server_save_root_, worlds_)) {
    return op_failure("Renamed world, but failed to persist world registry metadata.");
  }
  return op_success(
      "Renamed world '" + std::string{old_name} + "' to '" + std::string{new_name} + "'.");
}

command::WorldOperationCommandResult ServerRuntimeImpl::set_default_world_from_command(
    std::string_view name) {
  WorldRecord* record = worlds_.find(name);
  if (record == nullptr) {
    return op_failure("Unknown world '" + std::string{name} + "'. Use /world list to see available worlds.");
  }
  if (record->state != WorldRuntimeState::Running && record->id != DefaultWorldID) {
    command::WorldOperationCommandResult loaded = load_world_from_command(record->name);
    if (!loaded.success) {
      return op_failure("Unable to load default world '" + std::string{name} + "': " + loaded.message);
    }
  }
  if (!worlds_.set_default(name)) {
    return op_failure("Unable to set default world.");
  }
  if (!save_registry_index(server_save_root_, worlds_)) {
    return op_failure("Default world changed, but failed to persist world registry metadata.");
  }
  return op_success("Default world is now '" + std::string{name} + "'.");
}

command::WorldOperationCommandResult ServerRuntimeImpl::set_world_spawn_from_command(
    std::string_view name,
    Location spawn) {
  WorldRecord* record = worlds_.find(name);
  if (record == nullptr) {
    return op_failure("Unknown world '" + std::string{name} + "'. Use /world list to see available worlds.");
  }
  spawn.world = record->id;
  if (world::WorldRuntime* runtime = world_by_id(record->id); runtime != nullptr) {
    runtime->set_spawn_point(spawn);
  }
  const std::filesystem::path root = world_root_path(server_save_root_, record->name);
  if (!update_world_json_spawn(
          root,
          record->name,
          record->seed,
          record->generator_id,
          spawn)) {
    return op_failure("Failed to persist world spawn for '" + record->name + "'.");
  }
  record->spawn = spawn;
  if (!save_registry_index(server_save_root_, worlds_)) {
    return op_failure("Set world spawn, but failed to persist world registry metadata.");
  }
  std::ostringstream out;
  out << "Set spawn for world '" << record->name << "' to "
      << spawn.x << ", " << spawn.y << ", " << spawn.z << ".";
  return op_success(out.str());
}

void ServerRuntimeImpl::send_space_transition_feedback(
    world::WorldRuntime& runtime,
    EntityID vehicle,
    std::string_view message,
    bool error) {
  const std::optional<EntityNetID> net_id =
      runtime.network_id_for_entity(vehicle);
  if (!net_id.has_value()) {
    return;
  }
  const VehicleEntityKey key{runtime.id(), net_id.value()};
  if (const auto cooldown = space_transition_feedback_cooldowns_.find(key);
      cooldown != space_transition_feedback_cooldowns_.end() &&
      cooldown->second > tick_index_) {
    return;
  }
  space_transition_feedback_cooldowns_[key] =
      tick_index_ + seconds_to_ticks_ceil(2.0F, base_tps_);

  const entity::RideableComponent* rideable =
      runtime.find_entity_rideable(vehicle);
  if (rideable == nullptr) {
    return;
  }
  for (const entity::Seat& seat : rideable->seats) {
    if (seat.occupant.value == 0U) {
      continue;
    }
    const std::optional<PlayerID> player_id =
        runtime.player_for_entity(seat.occupant);
    if (!player_id.has_value()) {
      continue;
    }
    ServerSession* session = find_session_for_player(sessions_, player_id.value());
    if (session == nullptr || !session->authenticated || session->closing ||
        session->world_id != runtime.id()) {
      continue;
    }
    arv::text::RichText rich =
        arv::text::ChatMessageTemplate::parse(message).to_rich_text();
    if (error) {
      send_error_message(*session, std::move(rich));
    } else {
      send_system_message(*session, std::move(rich));
    }
  }
}

void ServerRuntimeImpl::tick_space_exit_transitions() {
  ARV_PROFILE_ZONE("Space exit transition checks");
  if (space_definitions_ == nullptr || vehicle_registry_ == nullptr) {
    return;
  }

  const std::vector<world::WorldRuntime*> worlds = loaded_worlds();
  for (world::WorldRuntime* runtime : worlds) {
    if (runtime == nullptr) {
      continue;
    }
    const WorldRecord* source_record = worlds_.find(runtime->id());
    if (source_record == nullptr) {
      continue;
    }
    const world::WorldDefinition* source_definition =
        find_world_definition_for_record(space_definitions_, *source_record);
    if (source_definition == nullptr ||
        source_definition->kind != world::WorldDefinitionKind::Planet ||
        !source_definition->space_exit.has_value()) {
      continue;
    }
    const world::SpaceExitDefinition& exit =
        source_definition->space_exit.value();
    if (!exit.enabled || !source_definition->rules.allow_vehicle_launch) {
      continue;
    }
    const world::WorldDefinition* target_definition =
        space_definitions_->find_world(exit.target_space_world);
    if (target_definition == nullptr ||
        target_definition->kind != world::WorldDefinitionKind::Space) {
      core::warn(
          "space_exit",
          "rejected space exit from world '{}': target space world '{}' is not defined",
          source_definition->id,
          exit.target_space_world);
      continue;
    }

    for (const EntityID entity : runtime->entity_ids()) {
      if (!runtime->entity_exists(entity)) {
        continue;
      }
      entity::VehiclePhysicsComponent* physics =
          runtime->find_entity_vehicle_physics(entity);
      const entity::EntityTransform* transform =
          runtime->find_entity_transform(entity);
      if (physics == nullptr || transform == nullptr ||
          runtime->find_entity_rideable(entity) == nullptr) {
        continue;
      }
      const std::optional<PlayerID> primary_player =
          primary_player_for_occupied_vehicle(*runtime, sessions_, entity);
      if (!primary_player.has_value()) {
        continue;
      }
      const Vec3f launch_velocity =
          vehicle_launch_velocity(*runtime, entity, *physics);
      if (transform->position.y < static_cast<f32>(exit.min_y)) {
        if (launch_velocity.y + 0.001F >= exit.min_vertical_speed) {
          std::ostringstream message;
          message << "Launch needs altitude Y " << exit.min_y
                  << " before entering space.";
          send_space_transition_feedback(*runtime, entity, message.str(), true);
        }
        continue;
      }
      if (launch_velocity.y + 0.001F < exit.min_vertical_speed) {
        std::ostringstream message;
        message << "Launch needs upward speed "
                << exit.min_vertical_speed
                << " before entering space.";
        send_space_transition_feedback(*runtime, entity, message.str(), true);
        continue;
      }
      const entity::VehicleDefinition* vehicle_definition =
          vehicle_registry_->find(physics->definition);
      if (vehicle_definition == nullptr) {
        core::info(
            "space_exit",
            "rejected space exit for vehicle {} in world '{}': missing vehicle definition '{}'",
            entity.value,
            source_definition->id,
            physics->definition);
        send_space_transition_feedback(
            *runtime,
            entity,
            "Launch failed because this vehicle definition is unavailable.",
            true);
        continue;
      }
      if (!vehicle_satisfies_required_tags(
              *runtime,
              entity,
              *vehicle_definition,
              exit.require_vehicle_tags)) {
        core::info(
            "space_exit",
            "rejected space exit for vehicle {} in world '{}': missing required vehicle tag",
            entity.value,
            source_definition->id);
        send_space_transition_feedback(
            *runtime,
            entity,
            "Launch failed because this vehicle is not space-capable.",
            true);
        continue;
      }
      if (exit.consume_fuel &&
          !vehicle_has_launch_fuel(*physics, *vehicle_definition)) {
        core::info(
            "space_exit",
            "rejected space exit for vehicle {} in world '{}': insufficient launch fuel",
            entity.value,
            source_definition->id);
        send_space_transition_feedback(
            *runtime,
            entity,
            "Launch failed because the rocket needs more fuel.",
            true);
        continue;
      }

      WorldRecord* target_record =
          find_world_record_for_definition_id(worlds_, target_definition->id);
      if (target_record == nullptr) {
        command::WorldCreateCommandRequest create_request;
        create_request.name =
            world_runtime_name_for_definition_id(target_definition->id);
        create_request.world_type = std::string{"space"};
        create_request.generator_id = std::string{world::VoidWorldGenerationID};
        command::WorldOperationCommandResult created =
            create_world_from_command(create_request);
        if (!created.success) {
          core::warn(
              "space_exit",
              "rejected space exit from world '{}' to '{}': {}",
              source_definition->id,
              target_definition->id,
              created.message);
          send_space_transition_feedback(
              *runtime,
              entity,
              "Launch failed because the target space world is unavailable.",
              true);
          continue;
        }
        target_record =
            find_world_record_for_definition_id(worlds_, target_definition->id);
      }
      if (target_record == nullptr) {
        core::warn(
            "space_exit",
            "rejected space exit from world '{}' to '{}': target world record is unavailable",
            source_definition->id,
            target_definition->id);
        send_space_transition_feedback(
            *runtime,
            entity,
            "Launch failed because the target space world is unavailable.",
            true);
        continue;
      }
      if (target_record->state != WorldRuntimeState::Running) {
        command::WorldOperationCommandResult loaded =
            load_world_from_command(target_record->name);
        if (!loaded.success) {
          core::warn(
              "space_exit",
              "rejected space exit from world '{}' to '{}': {}",
              source_definition->id,
              target_definition->id,
              loaded.message);
          send_space_transition_feedback(
              *runtime,
              entity,
              "Launch failed because the target space world could not load.",
              true);
          continue;
        }
        target_record =
            find_world_record_for_definition_id(worlds_, target_definition->id);
      }
      if (target_record == nullptr ||
          world_by_id(target_record->id) == nullptr) {
        core::warn(
            "space_exit",
            "rejected space exit from world '{}' to '{}': target world failed to load",
            source_definition->id,
            target_definition->id);
        send_space_transition_feedback(
            *runtime,
            entity,
            "Launch failed because the target space world could not load.",
            true);
        continue;
      }

      const world::SpaceTransitionSpawnDefinition& spawn =
          exit.transition_spawn;
      const Vec3f target_velocity =
          spawn.velocity +
          launch_velocity * std::max(0.0F, spawn.velocity_scale);
      const f32 target_yaw = degrees_to_radians(spawn.rotation_degrees.y);
      const f32 target_pitch = degrees_to_radians(spawn.rotation_degrees.x);
      WorldTransitionRequest request;
      request.root_entity = entity;
      request.source_world = runtime->id();
      request.target_world = target_record->id;
      request.target_position = spawn.position;
      request.target_velocity = target_velocity;
      request.target_rotation = euler_degrees_rotation(spawn.rotation_degrees);
      request.target_yaw_radians = target_yaw;
      request.target_pitch_radians = target_pitch;
      request.reason = WorldTransitionReason::SpaceExit;
      request.primary_player = primary_player;

      WorldTransitionResult transition = execute_world_transition(request);
      if (!transition.success) {
        core::warn(
            "space_exit",
            "rejected space exit from world '{}' to '{}': {}",
            source_definition->id,
            target_definition->id,
            transition.message);
        send_space_transition_feedback(
            *runtime,
            entity,
            "Launch failed during world transition.",
            true);
        continue;
      }
      core::info(
          "space_exit",
          "transitioned vehicle {} from '{}' to '{}' at y={} velocity_y={}",
          entity.value,
          source_definition->id,
          target_definition->id,
          transform->position.y,
          launch_velocity.y);
    }
  }
}

void ServerRuntimeImpl::tick_planet_entry_transitions() {
  ARV_PROFILE_ZONE("Space body entry checks");
  if (space_definitions_ == nullptr || vehicle_registry_ == nullptr) {
    return;
  }

  const std::vector<world::WorldRuntime*> worlds = loaded_worlds();
  for (world::WorldRuntime* runtime : worlds) {
    if (runtime == nullptr) {
      continue;
    }
    const WorldRecord* source_record = worlds_.find(runtime->id());
    if (source_record == nullptr) {
      continue;
    }
    const world::WorldDefinition* source_definition =
        find_world_definition_for_record(space_definitions_, *source_record);
    if (source_definition == nullptr ||
        source_definition->kind != world::WorldDefinitionKind::Space ||
        !source_definition->space.has_value() ||
        !source_definition->rules.allow_space_entry) {
      continue;
    }

    for (const EntityID entity : runtime->entity_ids()) {
      if (!runtime->entity_exists(entity)) {
        continue;
      }
      entity::VehiclePhysicsComponent* physics =
          runtime->find_entity_vehicle_physics(entity);
      const entity::EntityTransform* transform =
          runtime->find_entity_transform(entity);
      if (physics == nullptr || transform == nullptr ||
          runtime->find_entity_rideable(entity) == nullptr) {
        continue;
      }
      const std::optional<PlayerID> primary_player =
          primary_player_for_occupied_vehicle(*runtime, sessions_, entity);
      if (!primary_player.has_value()) {
        continue;
      }
      const entity::VehicleDefinition* vehicle_definition =
          vehicle_registry_->find(physics->definition);
      if (vehicle_definition == nullptr) {
        core::info(
            "planet_entry",
            "rejected planet entry for vehicle {} in world '{}': missing vehicle definition '{}'",
            entity.value,
            source_definition->id,
            physics->definition);
        send_space_transition_feedback(
            *runtime,
            entity,
            "Entry failed because this vehicle definition is unavailable.",
            true);
        continue;
      }

      bool transitioned = false;
      for (const std::string& body_id : source_definition->space->bodies) {
        const world::SpaceBodyDefinition* body =
            space_definitions_->find_space_body(body_id);
        if (body == nullptr || body->space_world != source_definition->id) {
          continue;
        }
        const f32 entry_radius = std::max(0.0F, body->entry_radius);
        if (distance_squared(transform->position, body->position) >
            entry_radius * entry_radius) {
          continue;
        }
        if (!body->entry.enabled ||
            body->entry.mode == world::SpaceBodyEntryMode::Disabled) {
          core::info(
              "planet_entry",
              "rejected planet entry for vehicle {} at body '{}': entry is disabled",
              entity.value,
              body->id);
          send_space_transition_feedback(
              *runtime,
              entity,
              "Entry failed because this body cannot be entered yet.",
              true);
          continue;
        }
        if (body->entry.mode != world::SpaceBodyEntryMode::FixedSpawn) {
          core::info(
              "planet_entry",
              "rejected planet entry for vehicle {} at body '{}': entry mode is not supported",
              entity.value,
              body->id);
          send_space_transition_feedback(
              *runtime,
              entity,
              "Entry failed because this body uses an unsupported entry mode.",
              true);
          continue;
        }
        if (!vehicle_satisfies_required_tags(
                *runtime,
                entity,
                *vehicle_definition,
                body->entry.require_vehicle_tags)) {
          core::info(
              "planet_entry",
              "rejected planet entry for vehicle {} at body '{}': missing required vehicle tag",
              entity.value,
              body->id);
          send_space_transition_feedback(
              *runtime,
              entity,
              "Entry failed because this vehicle is not space-capable.",
              true);
          continue;
        }

        const world::WorldDefinition* target_definition =
            space_definitions_->find_world(body->target_world);
        if (target_definition == nullptr ||
            target_definition->kind != world::WorldDefinitionKind::Planet) {
          core::warn(
              "planet_entry",
              "rejected planet entry at body '{}': target world '{}' is not defined",
              body->id,
              body->target_world);
          send_space_transition_feedback(
              *runtime,
              entity,
              "Entry failed because the target world is unavailable.",
              true);
          continue;
        }

        WorldRecord* target_record =
            find_world_record_for_definition_id(worlds_, target_definition->id);
        if (target_record == nullptr) {
          command::WorldCreateCommandRequest create_request;
          create_request.name =
              world_runtime_name_for_definition_id(target_definition->id);
          create_request.world_type = std::string{"default"};
          if (!target_definition->worldgen.empty()) {
            create_request.generator_id = target_definition->worldgen;
          }
          command::WorldOperationCommandResult created =
              create_world_from_command(create_request);
          if (!created.success) {
            core::warn(
                "planet_entry",
                "rejected planet entry from world '{}' through body '{}' to '{}': {}",
                source_definition->id,
                body->id,
                target_definition->id,
                created.message);
            send_space_transition_feedback(
                *runtime,
                entity,
                "Entry failed because the target world is unavailable.",
                true);
            continue;
          }
          target_record =
              find_world_record_for_definition_id(worlds_, target_definition->id);
        }
        if (target_record == nullptr) {
          core::warn(
              "planet_entry",
              "rejected planet entry from world '{}' through body '{}' to '{}': target world record is unavailable",
              source_definition->id,
              body->id,
              target_definition->id);
          send_space_transition_feedback(
              *runtime,
              entity,
              "Entry failed because the target world is unavailable.",
              true);
          continue;
        }
        if (target_record->state != WorldRuntimeState::Running) {
          command::WorldOperationCommandResult loaded =
              load_world_from_command(target_record->name);
          if (!loaded.success) {
            core::warn(
                "planet_entry",
                "rejected planet entry from world '{}' through body '{}' to '{}': {}",
                source_definition->id,
                body->id,
                target_definition->id,
                loaded.message);
            send_space_transition_feedback(
                *runtime,
                entity,
                "Entry failed because the target world could not load.",
                true);
            continue;
          }
          target_record =
              find_world_record_for_definition_id(worlds_, target_definition->id);
        }
        world::WorldRuntime* target_runtime =
            target_record == nullptr ? nullptr : world_by_id(target_record->id);
        if (target_record == nullptr || target_runtime == nullptr) {
          core::warn(
              "planet_entry",
              "rejected planet entry from world '{}' through body '{}' to '{}': target world failed to load",
              source_definition->id,
              body->id,
              target_definition->id);
          send_space_transition_feedback(
              *runtime,
              entity,
              "Entry failed because the target world could not load.",
              true);
          continue;
        }

        const Vec3f target_position =
            clamp_planet_entry_position_to_world_height(
                *target_runtime,
                body->entry.spawn_position);
        if (distance_squared(target_position, body->entry.spawn_position) > 0.0001F) {
          core::warn(
              "planet_entry",
              "clamped entry spawn for body '{}' from y={} to y={} to fit target world '{}'",
              body->id,
              body->entry.spawn_position.y,
              target_position.y,
              target_definition->id);
        }
        const Vec3f spawn_rotation_degrees =
            body->entry.spawn_rotation_degrees;
        WorldTransitionRequest request;
        request.root_entity = entity;
        request.source_world = runtime->id();
        request.target_world = target_record->id;
        request.target_position = target_position;
        request.target_velocity = body->entry.spawn_velocity;
        request.target_rotation = euler_degrees_rotation(spawn_rotation_degrees);
        request.target_yaw_radians =
            degrees_to_radians(spawn_rotation_degrees.y);
        request.target_pitch_radians =
            degrees_to_radians(spawn_rotation_degrees.x);
        request.reason = WorldTransitionReason::PlanetEntry;
        request.primary_player = primary_player;

        WorldTransitionResult transition = execute_world_transition(request);
        if (!transition.success) {
          core::warn(
              "planet_entry",
              "rejected planet entry from world '{}' through body '{}' to '{}': {}",
              source_definition->id,
              body->id,
              target_definition->id,
              transition.message);
          send_space_transition_feedback(
              *runtime,
              entity,
              "Entry failed during world transition.",
              true);
          continue;
        }
        core::info(
            "planet_entry",
            "transitioned vehicle {} from '{}' through body '{}' to '{}'",
            entity.value,
            source_definition->id,
            body->id,
            target_definition->id);
        transitioned = true;
        break;
      }
      if (transitioned) {
        continue;
      }
    }
  }
}

WorldTransitionResult ServerRuntimeImpl::execute_world_transition(
    const WorldTransitionRequest& request) {
  ARV_PROFILE_ZONE("World transition");
  world::WorldRuntime* source_world = world_by_id(request.source_world);
  world::WorldRuntime* target_world = world_by_id(request.target_world);
  if (source_world == nullptr) {
    return transition_failure("Transition source world is not loaded.");
  }
  if (target_world == nullptr) {
    return transition_failure("Transition target world is not loaded.");
  }
  if (!source_world->entity_exists(request.root_entity)) {
    return transition_failure("Transition root entity does not exist in the source world.");
  }

  TransitionGroup group;
  group.root_entity = request.root_entity;
  auto add_player_member =
      [&](EntityID player_entity, std::optional<std::string> seat_id)
          -> std::optional<std::string> {
    const std::optional<PlayerID> player_id = source_world->player_for_entity(player_entity);
    if (!player_id.has_value()) {
      return std::string{"Transition group contains a non-player passenger."};
    }
    player::PlayerState* state = source_world->find_player(player_id.value());
    if (state == nullptr) {
      return std::string{"Transition group contains a player without live state."};
    }
    ServerSession* session = find_session_for_player(sessions_, player_id.value());
    if (session == nullptr) {
      return std::string{"Transition group contains a player without a live session."};
    }
    const bool duplicate = std::any_of(
        group.players.begin(),
        group.players.end(),
        [&](const PlayerTransitionMember& member) {
          return member.session != nullptr && member.session->player_id == player_id.value();
        });
    if (!duplicate) {
      group.players.push_back({session, player_entity, *state, std::move(seat_id)});
    }
    return std::nullopt;
  };

  if (source_world->find_entity_rideable(request.root_entity) != nullptr) {
    group.root_is_vehicle = true;
  } else if (const std::optional<EntityID> mounted =
                 source_world->mounted_rideable(request.root_entity);
             mounted.has_value()) {
    group.root_entity = mounted.value();
    group.root_is_vehicle = true;
  }

  if (group.root_is_vehicle) {
    const entity::RideableComponent* rideable =
        source_world->find_entity_rideable(group.root_entity);
    if (rideable == nullptr) {
      return transition_failure("Transition vehicle is no longer rideable.");
    }
    group.vehicle = snapshot_entity_for_transition(
        *source_world,
        group.root_entity,
        request.target_position,
        request.target_velocity,
        request.target_rotation,
        vehicle_registry_);
    if (!group.vehicle.has_value()) {
      return transition_failure("Unable to snapshot transition vehicle.");
    }
    for (const entity::Seat& seat : rideable->seats) {
      if (seat.occupant.value == 0U) {
        continue;
      }
      if (std::optional<std::string> error =
              add_player_member(seat.occupant, seat.id);
          error.has_value()) {
        return transition_failure(*error);
      }
    }
  } else {
    if (std::optional<std::string> error =
            add_player_member(request.root_entity, std::nullopt);
        error.has_value()) {
      return transition_failure(*error);
    }
  }

  if (request.primary_player.has_value()) {
    const bool found = std::any_of(
        group.players.begin(),
        group.players.end(),
        [&](const PlayerTransitionMember& member) {
          return member.session != nullptr &&
                 member.session->player_id == request.primary_player.value();
        });
    if (!found) {
      return transition_failure("Transition primary player is not in the transition group.");
    }
  }
  if (group.players.empty()) {
    return transition_failure("Transition group does not contain a player.");
  }

  {
    ARV_PROFILE_ZONE("World transition target chunk prepare");
    target_world->request_chunk_section_load(
        transition_load_section_for_position(*target_world, request.target_position));
  }

  const double receive_time =
      static_cast<double>(tick_index_) /
      static_cast<double>(std::max<TickRate>(base_tps_, 1U));

  if (source_world->id() == target_world->id()) {
    EntityID moved_root = request.root_entity;
    if (group.vehicle.has_value()) {
      moved_root = group.root_entity;
      (void)source_world->set_entity_transform(
          group.root_entity,
          request.target_position,
          request.target_rotation);
      if (entity::EntityVelocity* velocity =
              source_world->find_entity_velocity(group.root_entity);
          velocity != nullptr) {
        velocity->linear = request.target_velocity;
      }
      if (entity::VehiclePhysicsComponent* physics =
              source_world->find_entity_vehicle_physics(group.root_entity);
          physics != nullptr) {
        physics->linear_velocity = request.target_velocity;
      }
      (void)source_world->update_passenger_attachment_transforms();
      if (const std::optional<EntityNetID> net_id =
              source_world->network_id_for_entity(group.root_entity);
          net_id.has_value()) {
        accepted_vehicle_movement_[{source_world->id(), net_id.value()}] = {
            request.target_position,
            request.target_velocity,
            request.target_rotation,
            {},
            protocol::VehicleMoveFlags::None,
            0U,
            tick_index_,
            receive_time,
            true};
      }
    }

    for (PlayerTransitionMember& member : group.players) {
      player::PlayerState* state = source_world->find_player(member.session->player_id);
      if (state == nullptr) {
        continue;
      }
      state->world_id = source_world->id();
      state->velocity = request.target_velocity;
      state->grounded = false;
      state->yaw_radians = request.target_yaw_radians;
      state->pitch_radians = request.target_pitch_radians;
      state->selection.world = source_world->id();
      state->last_processed_input_tick = tick_index_;
      if (!group.vehicle.has_value()) {
        state->position = request.target_position;
        (void)source_world->set_entity_position(member.source_entity, request.target_position);
      } else if (const entity::EntityTransform* transform =
                     source_world->find_entity_transform(member.source_entity);
                 transform != nullptr) {
        state->position = transform->position;
      }
      accepted_player_movement_[member.session->player_id] = {
          state->position,
          state->velocity,
          state->yaw_radians,
          state->pitch_radians,
          protocol::PlayerMoveFlags::None,
          0U,
          tick_index_,
          receive_time,
          true};
    }

    std::vector<PlayerID> moved_players;
    moved_players.reserve(group.players.size());
    for (const PlayerTransitionMember& member : group.players) {
      moved_players.push_back(member.session->player_id);
    }
    return transition_success("Transition completed.", std::move(moved_players), moved_root);
  }

  EntityID target_vehicle{};
  if (group.vehicle.has_value()) {
    target_vehicle = target_world->create_entity(
        std::move(group.vehicle->create_info));
    if (target_vehicle.value == 0U) {
      return transition_failure("Unable to create transition vehicle in the target world.");
    }
    if (entity::VehiclePhysicsComponent* physics =
            target_world->find_entity_vehicle_physics(target_vehicle);
        physics != nullptr && group.vehicle->vehicle_physics.has_value()) {
      *physics = group.vehicle->vehicle_physics.value();
      physics->linear_velocity = request.target_velocity;
    }
    if (group.vehicle->vehicle_cargo.has_value()) {
      (void)target_world->set_entity_vehicle_cargo(
          target_vehicle,
          group.vehicle->vehicle_cargo.value());
    }
  }

  struct TargetPlayerState {
    PlayerTransitionMember* source{};
    player::PlayerState state{};
    EntityID target_entity{};
    std::optional<std::string> target_seat_id{};
    bool target_controls_vehicle{};
  };
  std::vector<TargetPlayerState> target_players;
  target_players.reserve(group.players.size());
  for (PlayerTransitionMember& member : group.players) {
    player::PlayerState transferred = member.source_state;
    transferred.world_id = target_world->id();
    transferred.position = request.target_position;
    transferred.velocity = request.target_velocity;
    transferred.grounded = false;
    transferred.yaw_radians = request.target_yaw_radians;
    transferred.pitch_radians = request.target_pitch_radians;
    transferred.selection.world = target_world->id();
    transferred.last_processed_input_tick = tick_index_;
    target_players.push_back({&member, std::move(transferred), {}});
  }

  if (transport_ != nullptr) {
    for (const PlayerTransitionMember& member : group.players) {
      const protocol::ClientboundWorldLeave leave{
          source_world->id(),
          static_cast<u16>(request.reason)};
      const net::NetworkResult leave_result = send_protocol_message(
          *transport_,
          member.session->connection_id,
          protocol::MessageType::ClientboundWorldLeave,
          protocol::ReliabilityClass::ReliableOrdered,
          protocol::NetworkLane::Session,
          protocol::serialize_clientbound_world_leave(leave));
      log_send_failure_if_dedicated(mode_, member.session->connection_id, leave_result);
    }
  }

  for (PlayerTransitionMember& member : group.players) {
    record_player_save_state(*member.session, member.source_state);
    (void)source_world->destroy_entity(
        member.source_entity,
        world::EntityDespawnReason::Transfer);
  }
  if (group.vehicle.has_value()) {
    if (group.vehicle->source_net_id.has_value()) {
      const VehicleEntityKey source_key{
          source_world->id(),
          group.vehicle->source_net_id.value()};
      accepted_vehicle_movement_.erase(source_key);
      vehicle_crash_cooldowns_.erase(source_key);
      last_vehicle_crash_debug_.erase(source_key);
      vehicle_smoke_states_.erase(source_key);
      space_transition_feedback_cooldowns_.erase(source_key);
    }
    (void)source_world->destroy_entity(
        group.root_entity,
        world::EntityDespawnReason::Transfer);
  }
  source_world->flush_entity_despawns();

  for (TargetPlayerState& target_player : target_players) {
    PlayerTransitionMember& member = *target_player.source;
    member.session->world_id = target_world->id();
    player::PlayerState& target_state =
        target_world->ensure_player(member.session->player_id);
    target_state = std::move(target_player.state);
    normalize_player_recipe_book(target_state);
    refresh_player_permissions(target_state);
    record_player_save_state(*member.session, target_state);
    if (const std::optional<EntityID> entity_id =
            target_world->entity_for_player(member.session->player_id);
        entity_id.has_value()) {
      target_player.target_entity = entity_id.value();
      (void)target_world->set_entity_position(
          entity_id.value(),
          target_state.position);
      if (entity::EntityVelocity* velocity =
              target_world->find_entity_velocity(entity_id.value());
          velocity != nullptr) {
        velocity->linear = target_state.velocity;
      }
    }
  }

  if (target_vehicle.value != 0U) {
    for (TargetPlayerState& target_player : target_players) {
      if (target_player.target_entity.value == 0U ||
          !target_player.source->seat_id.has_value()) {
        continue;
      }
      if (!target_world->mount_entity(
              target_vehicle,
              target_player.target_entity,
              target_player.source->seat_id.value())) {
        continue;
      }
      target_player.target_seat_id = target_player.source->seat_id;
      if (const entity::RideableComponent* rideable =
              target_world->find_entity_rideable(target_vehicle);
          rideable != nullptr) {
        for (const entity::Seat& seat : rideable->seats) {
          if (seat.occupant == target_player.target_entity) {
            target_player.target_seat_id = seat.id;
            target_player.target_controls_vehicle = seat.controls_vehicle;
            break;
          }
        }
      }
    }
    (void)target_world->update_passenger_attachment_transforms();
    for (TargetPlayerState& target_player : target_players) {
      if (const entity::EntityTransform* transform =
              target_world->find_entity_transform(target_player.target_entity);
          transform != nullptr) {
        if (player::PlayerState* state =
                target_world->find_player(target_player.source->session->player_id);
            state != nullptr) {
          state->position = transform->position;
        }
      }
    }
    if (const std::optional<EntityNetID> net_id =
            target_world->network_id_for_entity(target_vehicle);
        net_id.has_value()) {
      accepted_vehicle_movement_[{target_world->id(), net_id.value()}] = {
          request.target_position,
          request.target_velocity,
          request.target_rotation,
          {},
          protocol::VehicleMoveFlags::None,
          0U,
          tick_index_,
          receive_time,
          true};
    }
  }

  std::vector<PlayerID> moved_players;
  moved_players.reserve(target_players.size());
  for (TargetPlayerState& target_player : target_players) {
    ServerSession& session = *target_player.source->session;
    player::PlayerState* target_state = target_world->find_player(session.player_id);
    if (target_state == nullptr) {
      continue;
    }
    chunk_streamer_.remove_session(session.session_id);
    entity_replication_.erase(session.session_id);
    inventory_replication_.erase(session.player_id);
    pending_player_commands_.erase(session.player_id);
    block_break_attempts_.erase(session.player_id);
    last_sent_block_break_progress_.erase(session.player_id);
    close_container_sessions_for_player_session(
        session.session_id,
        protocol::ContainerCloseReason::ChunkUnloaded);
    accepted_player_movement_[session.player_id] = {
        target_state->position,
        target_state->velocity,
        target_state->yaw_radians,
        target_state->pitch_radians,
        protocol::PlayerMoveFlags::None,
        0U,
        tick_index_,
        receive_time,
        true};

    send_world_join_result(session, *target_state);
    session.connection_state = net::ConnectionState::InGame;
    if (target_vehicle.value != 0U &&
        target_player.target_entity.value != 0U &&
        target_player.target_seat_id.has_value()) {
      send_ride_started_to_session(
          session,
          *target_world,
          target_player.target_entity,
          target_vehicle,
          target_player.target_seat_id.value(),
          target_player.target_controls_vehicle,
          protocol::NetworkLane::Session);
      send_entity_passengers_to_session(
          session,
          *target_world,
          target_vehicle,
          protocol::NetworkLane::Session);
    }
    sessions_.for_each_session([&](const ServerSession& subject) {
      if (subject.authenticated && subject.world_id == session.world_id) {
        send_player_profile_info_to(session, subject);
      }
    });
    broadcast_player_profile_info(session);
    moved_players.push_back(session.player_id);
  }

  if (target_vehicle.value != 0U) {
    broadcast_entity_passengers(*target_world, target_vehicle);
  }

  return transition_success(
      "Transition completed.",
      std::move(moved_players),
      target_vehicle.value == 0U ? request.root_entity : target_vehicle);
}

command::WorldOperationCommandResult
ServerRuntimeImpl::teleport_player_to_world_spawn_from_command(
    PlayerID player_id,
    std::string_view name) {
  const WorldRecord* record = worlds_.find(name);
  if (record == nullptr) {
    return op_failure("Unknown world '" + std::string{name} + "'. Use /world list to see available worlds.");
  }
  world::WorldRuntime* target_world = world_by_id(record->id);
  if (target_world == nullptr || record->state != WorldRuntimeState::Running) {
    return op_failure("World '" + record->name + "' is not loaded. Use /world load " + record->name + " first.");
  }

  ServerSession* target_session = nullptr;
  for (const SessionID session_id : sessions_.session_ids()) {
    ServerSession* session = sessions_.find(session_id);
    if (session != nullptr && session->player_id == player_id) {
      target_session = session;
      break;
    }
  }
  if (target_session == nullptr) {
    return op_failure("Unable to teleport player to world spawn.");
  }

  world::WorldRuntime* source_world = world_for_session(*target_session);
  if (source_world == nullptr) {
    return op_failure("Unable to find the player's current world.");
  }
  player::PlayerState* source_state = source_world->find_player(player_id);
  if (source_state == nullptr) {
    return op_failure("Unable to teleport player to world spawn.");
  }
  const std::optional<EntityID> player_entity =
      source_world->entity_for_player(player_id);
  if (!player_entity.has_value()) {
    return op_failure("Unable to teleport player to world spawn.");
  }

  Location spawn = record->spawn.value_or(target_world->spawn_point());
  spawn.world = target_world->id();
  const Vec3f spawn_position = location_position(spawn);
  const std::string display_name =
      target_session->display_name.empty() ? target_session->profile_name
                                           : target_session->display_name;
  const EntityID root_entity =
      source_world->mounted_rideable(player_entity.value()).value_or(player_entity.value());
  WorldTransitionRequest request;
  request.root_entity = root_entity;
  request.source_world = source_world->id();
  request.target_world = target_world->id();
  request.target_position = spawn_position;
  request.target_velocity = {};
  request.target_rotation = yaw_rotation(spawn.yaw_radians);
  request.target_yaw_radians = spawn.yaw_radians;
  request.target_pitch_radians = spawn.pitch_radians;
  request.reason = WorldTransitionReason::Command;
  request.primary_player = player_id;
  WorldTransitionResult transition = execute_world_transition(request);
  if (!transition.success) {
    return op_failure(transition.message);
  }
  core::info(
      "worlds",
      "moved player '{}' id={} from world {} to '{}' id={}",
      display_name,
      player_id.value,
      source_world->id().value,
      record->name,
      target_world->id().value);
  return op_success("Teleported " + display_name + " to world '" + record->name + "'.");
}

} // namespace arv::server::detail
