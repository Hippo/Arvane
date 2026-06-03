#include "arv/persistence/save.hpp"

#include "atomic_file.hpp"

#include "arv/io/data_codecs.hpp"
#include "arv/player/permissions.hpp"
#include "arv/world/world_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>

namespace arv::persistence {
namespace {

[[nodiscard]] u64 fnv1a_append(u64 hash, std::string_view text) noexcept {
  for (const char ch : text) {
    hash ^= static_cast<u8>(ch);
    hash *= 1099511628211ULL;
  }
  return hash;
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
  return value != nullptr && io::is_integer_value(*value) ? value->as_u64_or(fallback) : fallback;
}

[[nodiscard]] i64 i64_value_or(
    const io::DataValue& object,
    std::string_view key,
    i64 fallback) {
  const io::DataValue* value = member(object, key);
  return value != nullptr && io::is_integer_value(*value) ? value->as_i64_or(fallback) : fallback;
}

[[nodiscard]] f64 f64_value_or(
    const io::DataValue& object,
    std::string_view key,
    f64 fallback) {
  const io::DataValue* value = member(object, key);
  return value != nullptr && io::is_number_value(*value) ? value->as_f64_or(fallback) : fallback;
}

[[nodiscard]] io::DataValue vec3_to_value(Vec3f value) {
  io::DataValue array = io::DataValue::array();
  array.push(static_cast<f64>(value.x));
  array.push(static_cast<f64>(value.y));
  array.push(static_cast<f64>(value.z));
  return array;
}

[[nodiscard]] Vec3f vec3_from_value(const io::DataValue& node) {
  const io::DataValue::Array* array = node.array_if();
  if (array == nullptr || array->size() < 3U) {
    return {};
  }
  return {
      static_cast<f32>((*array)[0].as_f64_or(0.0)),
      static_cast<f32>((*array)[1].as_f64_or(0.0)),
      static_cast<f32>((*array)[2].as_f64_or(0.0))};
}

[[nodiscard]] io::DataValue location_to_value(Location location, bool include_world) {
  io::DataValue node = io::DataValue::object();
  node.set("x", static_cast<f64>(location.x));
  node.set("y", static_cast<f64>(location.y));
  node.set("z", static_cast<f64>(location.z));
  node.set("yaw_radians", static_cast<f64>(location.yaw_radians));
  node.set("pitch_radians", static_cast<f64>(location.pitch_radians));
  if (include_world && location.world.has_value()) {
    node.set("world", static_cast<u64>(location.world->value));
  }
  return node;
}

[[nodiscard]] std::optional<Location> location_from_value(const io::DataValue& node) {
  if (node.kind() != io::DataKind::Object) {
    return std::nullopt;
  }
  Location location;
  if (const io::DataValue* position = member(node, "position");
      position != nullptr && position->array_if() != nullptr &&
      position->array_if()->size() >= 3U) {
    const Vec3f vec = vec3_from_value(*position);
    location.x = vec.x;
    location.y = vec.y;
    location.z = vec.z;
  } else if (
      member(node, "x") != nullptr &&
      member(node, "y") != nullptr &&
      member(node, "z") != nullptr) {
    location.x = static_cast<f32>(f64_value_or(node, "x", 0.0));
    location.y = static_cast<f32>(f64_value_or(node, "y", 0.0));
    location.z = static_cast<f32>(f64_value_or(node, "z", 0.0));
  } else {
    return std::nullopt;
  }
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

[[nodiscard]] io::DataValue quat_to_value(Quatf value) {
  io::DataValue array = io::DataValue::array();
  array.push(static_cast<f64>(value.x));
  array.push(static_cast<f64>(value.y));
  array.push(static_cast<f64>(value.z));
  array.push(static_cast<f64>(value.w));
  return array;
}

[[nodiscard]] Quatf quat_from_value(const io::DataValue& node) {
  const io::DataValue::Array* array = node.array_if();
  if (array == nullptr || array->size() < 4U) {
    return {};
  }
  return {
      static_cast<f32>((*array)[0].as_f64_or(0.0)),
      static_cast<f32>((*array)[1].as_f64_or(0.0)),
      static_cast<f32>((*array)[2].as_f64_or(0.0)),
      static_cast<f32>((*array)[3].as_f64_or(0.0))};
}

[[nodiscard]] io::DataValue item_stack_to_value(
    player::ItemStack stack,
    const player::ItemRegistry& registry) {
  player::normalize_item_stack_data(stack);
  const player::ItemRecord* item = registry.item(stack.item);
  io::DataValue node = io::DataValue::object();
  node.set("item", item == nullptr ? std::string{player::MissingItemName} : item->name);
  node.set("count", static_cast<u64>(stack.count));
  if (player::item_stack_has_data(stack)) {
    node.set("data", stack.data->root);
  }
  return node;
}

[[nodiscard]] player::ItemStack item_stack_from_value(
    const io::DataValue& node,
    const player::ItemRegistry& registry) {
  const player::ItemRecord* missing_item = registry.missing_item();
  const player::ItemID fallback = missing_item == nullptr ? player::AirItemID : missing_item->id;
  const std::optional<player::ItemID> item =
      registry.find_item(string_value_or(node, "item", std::string{player::MissingItemName}));
  player::ItemStack stack{
      item.value_or(fallback),
      static_cast<u16>(u64_value_or(node, "count", 0U))};
  if (const io::DataValue* data = member(node, "data"); data != nullptr) {
    stack.data = io::DataDocument{1U, *data};
    player::normalize_item_stack_data(stack);
  }
  return stack;
}

[[nodiscard]] RegionCoord entity_region_for_position(Vec3f position) noexcept {
  const BlockPos block_pos{{
      static_cast<i32>(std::floor(position.x)),
      static_cast<i32>(std::floor(position.y)),
      static_cast<i32>(std::floor(position.z)),
  }};
  return chunk_column_to_region(chunk_to_column(block_to_chunk(block_pos)));
}

[[nodiscard]] io::DataValue entity_record_to_value(
    const entity::PersistentEntityRecord& entity,
    const player::ItemRegistry& item_registry) {
  io::DataValue components = io::DataValue::object();
  if (entity.dropped_item_stack.has_value()) {
    io::DataValue dropped_item = io::DataValue::object();
    dropped_item.set(
        "stack",
        item_stack_to_value(entity.dropped_item_stack.value(), item_registry));
    dropped_item.set(
        "pickup_delay_seconds",
        static_cast<f64>(entity.dropped_item_pickup_delay_seconds));
    components.set("dropped_item", std::move(dropped_item));
  }

  io::DataValue node = io::DataValue::object();
  node.set("uuid", entity.uuid);
  node.set("type", entity.type_name);
  node.set("version", static_cast<u64>(1U));
  node.set("world_id", static_cast<u64>(entity.world_id.value));
  node.set("position", vec3_to_value(entity.position));
  node.set("rotation", quat_to_value(entity.rotation));
  node.set("velocity", vec3_to_value(entity.velocity));
  node.set("components", std::move(components));
  return node;
}

[[nodiscard]] entity::PersistentEntityRecord entity_record_from_json(
    const io::DataValue& entry,
    const player::ItemRegistry& item_registry) {
  entity::PersistentEntityRecord record;
  record.uuid = string_value_or(entry, "uuid", std::string{});
  record.type_name = string_value_or(entry, "type", std::string{});
  record.world_id = WorldID{static_cast<u32>(u64_value_or(entry, "world_id", 0U))};
  if (const io::DataValue* value = member(entry, "position"); value != nullptr) {
    record.position = vec3_from_value(*value);
  }
  if (const io::DataValue* value = member(entry, "rotation"); value != nullptr) {
    record.rotation = quat_from_value(*value);
  }
  if (const io::DataValue* value = member(entry, "velocity"); value != nullptr) {
    record.velocity = vec3_from_value(*value);
  }
  if (const io::DataValue* components = member(entry, "components");
      components != nullptr && components->kind() == io::DataKind::Object) {
    if (const io::DataValue* dropped_item = member(*components, "dropped_item");
        dropped_item != nullptr && dropped_item->kind() == io::DataKind::Object) {
      if (const io::DataValue* stack = member(*dropped_item, "stack"); stack != nullptr) {
        record.dropped_item_stack = item_stack_from_value(*stack, item_registry);
      }
      if (const io::DataValue* delay = member(*dropped_item, "pickup_delay_seconds");
          delay != nullptr && io::is_number_value(*delay)) {
        record.dropped_item_pickup_delay_seconds =
            static_cast<Seconds>(delay->as_f64_or(0.0));
      }
    }
  }
  return record;
}

[[nodiscard]] std::filesystem::path entity_region_file_path(
    const std::filesystem::path& root,
    std::string_view world_name,
    RegionCoord region) {
  return root / "worlds" / std::string{world_name} / "entities" /
         ("r." + std::to_string(region.x) + "." + std::to_string(region.z) + ".json");
}

[[nodiscard]] io::DataValue world_generation_to_value(const world::WorldGenerationConfig& config) {
  io::DataValue surface_layers = io::DataValue::array();
  for (const world::SurfaceLayerDefinition& layer : config.surface_layers) {
    io::DataValue entry = io::DataValue::object();
    entry.set("block_name", layer.block_name);
    entry.set("depth", static_cast<i64>(layer.depth));
    entry.set("remaining", layer.remaining);
    surface_layers.push(std::move(entry));
  }

  io::DataValue node = io::DataValue::object();
  node.set("id", config.id);
  node.set("dimension_id", config.dimension_id);
  node.set("version", static_cast<u64>(config.version));
  node.set("seed_salt", config.seed_salt);
  node.set("min_y", static_cast<i64>(config.min_y));
  node.set("max_y", static_cast<i64>(config.max_y));
  node.set("sea_level", static_cast<i64>(config.sea_level));
  node.set("base_height", static_cast<i64>(config.base_height));
  node.set("height_scale", static_cast<i64>(config.height_scale));
  node.set("terrain_noise_type", config.terrain_noise_type);
  node.set("terrain_frequency", static_cast<f64>(config.terrain_frequency));
  node.set("terrain_octaves", static_cast<u64>(config.terrain_octaves));
  node.set("terrain_amplitude", static_cast<f64>(config.terrain_amplitude));
  node.set("terrain_lacunarity", static_cast<f64>(config.terrain_lacunarity));
  node.set("terrain_gain", static_cast<f64>(config.terrain_gain));
  node.set("surface_layers", std::move(surface_layers));
  return node;
}

[[nodiscard]] io::DataValue required_packs_to_value(
    std::span<const RequiredPackMetadata> packs) {
  io::DataValue required = io::DataValue::array();
  for (const RequiredPackMetadata& pack : packs) {
    required.push(pack.id);
  }
  io::DataValue node = io::DataValue::object();
  node.set("required", std::move(required));
  return node;
}

[[nodiscard]] std::vector<RequiredPackMetadata> required_packs_from_value(
    const io::DataValue& node) {
  std::vector<RequiredPackMetadata> packs;
  if (node.kind() != io::DataKind::Object) {
    return packs;
  }
  const io::DataValue* required = member(node, "required");
  const io::DataValue::Array* required_array =
      required == nullptr ? nullptr : required->array_if();
  if (required_array == nullptr) {
    return packs;
  }
  packs.reserve(required_array->size());
  for (const io::DataValue& entry : *required_array) {
    RequiredPackMetadata pack;
    if (entry.kind() == io::DataKind::String) {
      pack.id = std::string{entry.as_string_or({})};
    } else if (entry.kind() == io::DataKind::Object) {
      // Older saves may include version/hash metadata. Worlds only need the pack ID.
      pack.id = string_value_or(entry, "id", std::string{});
    } else {
      continue;
    }
    if (!pack.id.empty()) {
      packs.push_back(std::move(pack));
    }
  }
  return packs;
}

[[nodiscard]] world::WorldGenerationConfig world_generation_from_value(
    const io::DataValue& node) {
  world::WorldGenerationConfig config;
  config.id = string_value_or(node, "id", std::string{});
  config.dimension_id = string_value_or(node, "dimension_id", config.id);
  config.version = static_cast<u32>(u64_value_or(node, "version", 0U));
  config.seed_salt = string_value_or(node, "seed_salt", std::string{});
  config.min_y = static_cast<i32>(i64_value_or(node, "min_y", 0));
  config.max_y = static_cast<i32>(i64_value_or(node, "max_y", 0));
  config.sea_level = static_cast<i32>(i64_value_or(node, "sea_level", 0));
  config.base_height = static_cast<i32>(i64_value_or(node, "base_height", 0));
  config.height_scale = static_cast<i32>(i64_value_or(node, "height_scale", 0));
  config.terrain_noise_type = string_value_or(node, "terrain_noise_type", std::string{});
  config.terrain_frequency = static_cast<f32>(f64_value_or(node, "terrain_frequency", 0.0));
  config.terrain_octaves = static_cast<u32>(u64_value_or(node, "terrain_octaves", 0U));
  config.terrain_amplitude = static_cast<f32>(f64_value_or(node, "terrain_amplitude", 0.0));
  config.terrain_lacunarity = static_cast<f32>(f64_value_or(node, "terrain_lacunarity", 0.0));
  config.terrain_gain = static_cast<f32>(f64_value_or(node, "terrain_gain", 0.0));
  const io::DataValue* surface_layers = member(node, "surface_layers");
  const io::DataValue::Array* layer_array =
      surface_layers == nullptr ? nullptr : surface_layers->array_if();
  if (layer_array != nullptr) {
    for (const io::DataValue& entry : *layer_array) {
      config.surface_layers.push_back({
          string_value_or(entry, "block_name", std::string{}),
          static_cast<i32>(i64_value_or(entry, "depth", 0)),
          bool_value_or(entry, "remaining", false)});
    }
  }
  return config;
}

[[nodiscard]] io::DataValue world_clock_to_value(const world::WorldClock& clock) {
  io::DataValue node = io::DataValue::object();
  node.set("world_tick", static_cast<u64>(clock.world_tick));
  node.set("day_time_tick", static_cast<u64>(clock.day_time_tick));
  node.set("day_length_seconds", static_cast<f64>(clock.day_length_seconds));
  node.set("daylight_cycle", clock.daylight_cycle);
  node.set("time_scale", static_cast<f64>(clock.time_scale));
  return node;
}

[[nodiscard]] world::WorldClock world_clock_from_json(
    const io::DataValue& node,
    TickIndex fallback_world_tick) {
  world::WorldClock clock = world::make_world_clock();
  clock.world_tick = fallback_world_tick;
  if (node.kind() != io::DataKind::Object) {
    return clock;
  }
  clock.world_tick = static_cast<TickIndex>(
      u64_value_or(node, "world_tick", static_cast<u64>(clock.world_tick)));
  clock.day_length_seconds =
      static_cast<Seconds>(f64_value_or(node, "day_length_seconds", clock.day_length_seconds));
  clock.daylight_cycle = bool_value_or(node, "daylight_cycle", clock.daylight_cycle);
  clock.time_scale = static_cast<f32>(f64_value_or(node, "time_scale", clock.time_scale));
  world::retime_world_clock(clock, DefaultBaseTps);
  if (const io::DataValue* value = member(node, "day_time_tick");
      value != nullptr && io::is_integer_value(*value)) {
    world::set_day_time_tick(clock, static_cast<u32>(value->as_u64_or(0U)));
  }
  return clock;
}

Expected<void, PersistenceError> write_text_atomic(
    const std::filesystem::path& path,
    std::string_view text) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return Expected<void, PersistenceError>::failure(
        PersistenceError{"failed to create save directory: " + error.message()});
  }

  const std::filesystem::path temp_path = detail::temporary_path_for(path);
  {
    std::ofstream output(temp_path, std::ios::trunc);
    if (!output) {
      return Expected<void, PersistenceError>::failure(
          PersistenceError{"failed to open temporary save file for write"});
    }
    output << text;
    output.flush();
    if (!output) {
      return Expected<void, PersistenceError>::failure(
          PersistenceError{"failed to write save file text"});
    }
  }

  std::filesystem::rename(temp_path, path, error);
  if (error) {
    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temp_path, path, error);
  }
  if (error) {
    return Expected<void, PersistenceError>::failure(
        PersistenceError{"failed to finalize save file write: " + error.message()});
  }

  return Expected<void, PersistenceError>::success();
}

[[nodiscard]] Expected<std::string, PersistenceError> read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return Expected<std::string, PersistenceError>::failure(
        PersistenceError{"failed to open save file for read: " + path.string()});
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return Expected<std::string, PersistenceError>::success(buffer.str());
}

[[nodiscard]] Expected<std::string, PersistenceError> encode_json_text(
    const io::DataValue& node,
    std::string_view purpose) {
  auto encoded = io::encode_json_value(node);
  if (!encoded) {
    return Expected<std::string, PersistenceError>::failure(
        PersistenceError{"failed to encode " + std::string{purpose} + ": " +
                         encoded.error().describe()});
  }
  std::string text = std::move(encoded.value());
  if (!text.empty() && text.back() == '\n') {
    text.pop_back();
  }
  return Expected<std::string, PersistenceError>::success(std::move(text));
}

[[nodiscard]] Expected<io::DataValue, PersistenceError> decode_json_text(
    std::string_view text,
    std::string_view purpose) {
  auto decoded = io::decode_json_value(text);
  if (!decoded) {
    return Expected<io::DataValue, PersistenceError>::failure(
        PersistenceError{"failed to parse " + std::string{purpose} + ": " +
                         decoded.error().describe()});
  }
  return Expected<io::DataValue, PersistenceError>::success(std::move(decoded.value().root));
}

[[nodiscard]] Expected<io::DataValue, PersistenceError> read_json_file(
    const std::filesystem::path& path,
    std::string_view purpose) {
  auto text = read_text_file(path);
  if (!text) {
    return Expected<io::DataValue, PersistenceError>::failure(text.error());
  }
  return decode_json_text(text.value(), purpose);
}

Expected<void, PersistenceError> write_json_atomic(
    const std::filesystem::path& path,
    const io::DataValue& node,
    std::string_view purpose) {
  auto text = encode_json_text(node, purpose);
  if (!text) {
    return Expected<void, PersistenceError>::failure(text.error());
  }
  return write_text_atomic(path, text.value());
}

[[nodiscard]] std::optional<Location> read_world_spawn_from_file(
    const std::filesystem::path& path) {
  auto world_file = read_json_file(path, "world metadata JSON");
  if (!world_file || world_file.value().kind() != io::DataKind::Object) {
    return std::nullopt;
  }
  const io::DataValue* spawn = member(world_file.value(), "spawn");
  return spawn == nullptr ? std::nullopt : location_from_value(*spawn);
}

[[nodiscard]] std::optional<Location> read_primary_world_spawn(
    const std::filesystem::path& worlds_root,
    std::string_view loaded_world_name) {
  std::error_code error;
  if (!std::filesystem::is_directory(worlds_root, error) || error) {
    return std::nullopt;
  }

  std::vector<std::filesystem::path> candidates;
  const std::filesystem::path default_world = worlds_root / "default" / "world.json";
  if (std::filesystem::exists(default_world, error)) {
    candidates.push_back(default_world);
  }
  error.clear();
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(worlds_root, error)) {
    if (error || !entry.is_directory()) {
      error.clear();
      continue;
    }
    const std::filesystem::path candidate = entry.path() / "world.json";
    if (candidate == default_world || !std::filesystem::exists(candidate, error)) {
      error.clear();
      continue;
    }
    candidates.push_back(candidate);
  }
  std::sort(
      candidates.begin(),
      candidates.end(),
      [](const auto& lhs, const auto& rhs) {
        const bool lhs_default = lhs.parent_path().filename() == "default";
        const bool rhs_default = rhs.parent_path().filename() == "default";
        if (lhs_default != rhs_default) {
          return lhs_default;
        }
        return lhs.parent_path().filename().string() <
               rhs.parent_path().filename().string();
      });

  for (const std::filesystem::path& candidate : candidates) {
    if (candidate.parent_path().filename().string() == loaded_world_name) {
      continue;
    }
    if (std::optional<Location> spawn =
            read_world_spawn_from_file(candidate);
        spawn.has_value()) {
      return spawn;
    }
  }
  return std::nullopt;
}

[[nodiscard]] io::DataValue inventory_to_value(
    const player::Inventory& inventory,
    const player::ItemRegistry& registry) {
  HashMap<std::string, u16> indices;
  std::vector<std::string> table{std::string{player::AirItemName}};
  indices.emplace(std::string{player::AirItemName}, 0U);

  io::DataValue slots = io::DataValue::array();
  for (const std::optional<player::ItemStack>& slot : inventory.slots()) {
    if (!slot.has_value()) {
      slots.push(nullptr);
      continue;
    }

    const player::ItemRecord* item = registry.item(slot->item);
    const std::string item_name = item == nullptr ? std::string{player::MissingItemName} : item->name;
    auto [iter, inserted] = indices.emplace(item_name, static_cast<u16>(table.size()));
    if (inserted) {
      table.push_back(item_name);
    }

    io::DataValue slot_node = io::DataValue::object();
    slot_node.set("item", static_cast<u64>(iter->second));
    slot_node.set("count", static_cast<u64>(slot->count));
    if (player::item_stack_has_data(slot.value())) {
      slot_node.set("data", slot->data->root);
    }
    slots.push(std::move(slot_node));
  }

  io::DataValue item_table = io::DataValue::array();
  for (const std::string& item_name : table) {
    item_table.push(item_name);
  }
  io::DataValue node = io::DataValue::object();
  node.set("item_table", std::move(item_table));
  node.set("slots", std::move(slots));
  return node;
}

[[nodiscard]] player::Inventory inventory_from_json(
    const io::DataValue& node,
    const player::ItemRegistry& registry,
    usize slot_count) {
  player::Inventory inventory(slot_count);
  std::vector<std::optional<player::ItemID>> item_ids;
  const player::ItemRecord* missing_item = registry.missing_item();
  const player::ItemID fallback = missing_item == nullptr ? player::AirItemID : missing_item->id;

  const io::DataValue* item_table = member(node, "item_table");
  const io::DataValue::Array* item_array =
      item_table == nullptr ? nullptr : item_table->array_if();
  if (item_array != nullptr) {
    for (const io::DataValue& entry : *item_array) {
      if (entry.kind() != io::DataKind::String) {
        item_ids.push_back(fallback);
        continue;
      }
      const auto resolved = registry.find_item(entry.as_string_or({}));
      item_ids.push_back(resolved.has_value() ? resolved.value() : fallback);
    }
  }

  const io::DataValue* slots = member(node, "slots");
  const io::DataValue::Array* slot_array = slots == nullptr ? nullptr : slots->array_if();
  if (slot_array == nullptr) {
    return inventory;
  }
  for (usize index = 0; index < slot_array->size() && index < slot_count; ++index) {
    const io::DataValue& slot = (*slot_array)[index];
    if (slot.kind() == io::DataKind::Null) {
      continue;
    }

    const u16 item_index = static_cast<u16>(u64_value_or(slot, "item", 0U));
    const u16 count = static_cast<u16>(u64_value_or(slot, "count", 0U));
    if (item_index >= item_ids.size()) {
      continue;
    }
    player::ItemStack stack{item_ids[item_index].value_or(fallback), count};
    if (const io::DataValue* data = member(slot, "data"); data != nullptr) {
      stack.data = io::DataDocument{1U, *data};
      player::normalize_item_stack_data(stack);
    }
    inventory.set_slot(index, std::move(stack));
  }

  return inventory;
}

[[nodiscard]] const char* tri_state_name(player::TriState value) noexcept {
  switch (value) {
    case player::TriState::Allow:
      return "ALLOW";
    case player::TriState::Deny:
      return "DENY";
    case player::TriState::Unset:
      return "UNSET";
  }
  return "UNSET";
}

[[nodiscard]] std::optional<player::TriState> tri_state_from_value(
    const io::DataValue& node) {
  if (node.kind() == io::DataKind::Bool) {
    return node.as_bool_or(false) ? player::TriState::Allow : player::TriState::Deny;
  }
  if (node.kind() != io::DataKind::String) {
    return std::nullopt;
  }

  return player::tri_state_from_string(node.as_string_or({}));
}

[[nodiscard]] Expected<std::string, PersistenceError> encode_default_permission_config() {
  return Expected<std::string, PersistenceError>::success(
      "# Permission groups for this server save.\n"
      "groups: {}\n");
}

void write_core_value(io::DataValue& node, std::string_view name, player::TriState value) {
  if (value != player::TriState::Unset) {
    node.set(std::string{name}, tri_state_name(value));
  }
}

[[nodiscard]] io::DataValue core_permissions_to_value(const player::CorePermissionSet& core) {
  io::DataValue node = io::DataValue::object();
  write_core_value(node, "can_take_damage", core.can_take_damage);
  write_core_value(node, "can_modify_world", core.can_modify_world);
  write_core_value(node, "can_interact", core.can_interact);
  write_core_value(node, "can_open_inventory", core.can_open_inventory);
  write_core_value(node, "has_creative_inventory", core.has_creative_inventory);
  write_core_value(node, "can_fly", core.can_fly);
  write_core_value(node, "has_collision", core.has_collision);
  write_core_value(node, "is_visible_to_others", core.is_visible_to_others);
  return node;
}

[[nodiscard]] io::DataValue custom_permissions_to_value(
    const player::CustomPermissionMap& custom) {
  io::DataValue node = io::DataValue::object();
  for (const auto& [permission, value] : custom) {
    if (value != player::TriState::Unset) {
      node.set(permission, tri_state_name(value));
    }
  }
  return node;
}

[[nodiscard]] io::DataValue player_permissions_to_value(
    const player::PlayerPermissions& permissions) {
  io::DataValue node = io::DataValue::object();
  node.set("core", core_permissions_to_value(permissions.overrides));
  node.set("permissions", custom_permissions_to_value(permissions.custom_overrides));
  return node;
}

void read_group_list(player::PlayerPermissions& permissions, const io::DataValue& node) {
  const io::DataValue::Array* array = node.array_if();
  if (array == nullptr) {
    return;
  }
  for (const io::DataValue& entry : *array) {
    if (entry.kind() == io::DataKind::String) {
      permissions.groups.emplace_back(entry.as_string_or({}));
    }
  }
}

void read_core_permissions(player::PlayerPermissions& permissions, const io::DataValue& node) {
  const io::DataValue::Object* object = node.object_if();
  if (object == nullptr) {
    return;
  }
  for (const auto& [key, child] : *object) {
    const std::optional<player::TriState> value = tri_state_from_value(child);
    if (value.has_value()) {
      (void)player::set_core_permission_by_name(
          permissions.overrides,
          key,
          value.value());
    }
  }
}

void read_custom_permissions(player::PlayerPermissions& permissions, const io::DataValue& node) {
  const io::DataValue::Object* object = node.object_if();
  if (object == nullptr) {
    return;
  }
  for (const auto& [key, child] : *object) {
    const std::optional<player::TriState> value = tri_state_from_value(child);
    if (value.has_value() && player::is_valid_custom_permission_node(key)) {
      permissions.custom_overrides[key] = value.value();
    }
  }
}

[[nodiscard]] player::PlayerPermissions player_permissions_from_value(
    const io::DataValue& player_json) {
  player::PlayerPermissions permissions;
  if (const io::DataValue* groups = member(player_json, "groups"); groups != nullptr) {
    read_group_list(permissions, *groups);
  }

  if (const io::DataValue* overrides = member(player_json, "permission_overrides");
      overrides != nullptr && overrides->kind() == io::DataKind::Object) {
    if (const io::DataValue* groups = member(*overrides, "groups"); groups != nullptr) {
      read_group_list(permissions, *groups);
    }
    if (const io::DataValue* core = member(*overrides, "core"); core != nullptr) {
      read_core_permissions(permissions, *core);
    }
    if (const io::DataValue* custom = member(*overrides, "permissions"); custom != nullptr) {
      read_custom_permissions(permissions, *custom);
    }
  }
  return permissions;
}

[[nodiscard]] std::filesystem::path player_file_path(
    const std::filesystem::path& root,
    const PlayerSaveData& player) {
  const std::string filename =
      !player.profile_id.empty()
          ? player.profile_id.string() + ".json"
          : player.profile_name + ".dat";
  return root / "players" / filename;
}

Expected<void, PersistenceError> ensure_permission_config_file(
    const std::filesystem::path& root) {
  const std::filesystem::path path = root / "permissions.yaml";
  std::error_code error;
  if (std::filesystem::exists(path, error)) {
    return Expected<void, PersistenceError>::success();
  }
  if (error) {
    return Expected<void, PersistenceError>::failure(
        PersistenceError{"failed to inspect permission config file: " + error.message()});
  }

  auto encoded = encode_default_permission_config();
  if (!encoded) {
    return Expected<void, PersistenceError>::failure(encoded.error());
  }
  return write_text_atomic(path, encoded.value());
}

} // namespace

std::vector<RequiredPackMetadata> required_pack_metadata(
    std::span<const asset::RequiredPack> packs) {
  std::vector<RequiredPackMetadata> result;
  result.reserve(packs.size());
  for (const asset::RequiredPack& pack : packs) {
    result.push_back({pack.id});
  }
  return result;
}

void upsert_player_save(
    std::vector<PlayerSaveData>& players,
    PlayerSaveData save) {
  const auto found = std::find_if(
      players.begin(),
      players.end(),
      [&](const PlayerSaveData& existing) {
        const bool profile_matches =
            !save.profile_id.empty()
                ? existing.profile_id == save.profile_id
                : existing.profile_name == save.profile_name;
        return profile_matches && existing.world_name == save.world_name;
      });
  if (found == players.end()) {
    players.emplace_back(std::move(save));
  } else {
    *found = std::move(save);
  }
}

u64 hash_block_registry(const voxel::BlockRegistry& registry) noexcept {
  u64 hash = 14695981039346656037ULL;
  for (u32 index = 0; index < registry.variant_count(); ++index) {
    hash = fnv1a_append(hash, registry.variant_name(voxel::BlockVariantID{index}));
  }
  return hash;
}

u64 hash_item_registry(const player::ItemRegistry& registry) noexcept {
  u64 hash = 14695981039346656037ULL;
  for (u32 index = 0; index < registry.item_count(); ++index) {
    const player::ItemRecord* item = registry.item(player::ItemID{index});
    if (item != nullptr) {
      hash = fnv1a_append(hash, item->name);
    }
  }
  return hash;
}

std::vector<SavedServerSummary> discover_saved_servers(const std::filesystem::path& saves_root) {
  std::vector<SavedServerSummary> saves;
  std::error_code error;
  if (!std::filesystem::exists(saves_root, error) ||
      !std::filesystem::is_directory(saves_root, error)) {
    return saves;
  }

  for (const std::filesystem::directory_entry& server_entry :
       std::filesystem::directory_iterator(saves_root, error)) {
    if (error || !server_entry.is_directory()) {
      error.clear();
      continue;
    }

    const std::filesystem::path server_root = server_entry.path();
    const std::filesystem::path save_file = server_root / "save.json";
    const std::filesystem::path worlds_root = server_root / "worlds";
    if (!std::filesystem::exists(save_file, error) ||
        !std::filesystem::exists(worlds_root, error) ||
        !std::filesystem::is_directory(worlds_root, error)) {
      error.clear();
      continue;
    }

    std::string save_name = server_root.filename().string();
    if (auto save_json = read_json_file(save_file, "save metadata JSON"); save_json) {
      if (save_json.value().kind() == io::DataKind::Object) {
        const std::string parsed_name =
            string_value_or(save_json.value(), "name", std::string{});
        if (!parsed_name.empty()) {
          save_name = parsed_name;
        }
      }
    }

    std::string primary_world_name;
    const std::filesystem::path default_world = worlds_root / "default" / "world.json";
    if (std::filesystem::exists(default_world, error)) {
      primary_world_name = "default";
    } else {
      error.clear();
      for (const std::filesystem::directory_entry& world_entry :
           std::filesystem::directory_iterator(worlds_root, error)) {
        if (error || !world_entry.is_directory()) {
          error.clear();
          continue;
        }
        if (std::filesystem::exists(world_entry.path() / "world.json", error)) {
          primary_world_name = world_entry.path().filename().string();
          break;
        }
        error.clear();
      }
    }

    if (primary_world_name.empty()) {
      continue;
    }

    saves.push_back({save_name, server_root.filename().string(), primary_world_name, server_root});
  }

  std::sort(saves.begin(), saves.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.name != rhs.name) {
      return lhs.name < rhs.name;
    }
    return lhs.server_save_name < rhs.server_save_name;
  });
  return saves;
}

std::vector<SavedWorldSummary> discover_saved_worlds(const std::filesystem::path& saves_root) {
  std::vector<SavedWorldSummary> worlds;
  std::error_code error;
  if (!std::filesystem::exists(saves_root, error) ||
      !std::filesystem::is_directory(saves_root, error)) {
    return worlds;
  }

  for (const std::filesystem::directory_entry& server_entry :
       std::filesystem::directory_iterator(saves_root, error)) {
    if (error || !server_entry.is_directory()) {
      continue;
    }
    const std::filesystem::path server_root = server_entry.path();
    const std::filesystem::path worlds_root = server_root / "worlds";
    if (!std::filesystem::exists(worlds_root, error) ||
        !std::filesystem::is_directory(worlds_root, error)) {
      error.clear();
      continue;
    }

    for (const std::filesystem::directory_entry& world_entry :
         std::filesystem::directory_iterator(worlds_root, error)) {
      if (error || !world_entry.is_directory()) {
        continue;
      }
      const std::filesystem::path world_root = world_entry.path();
      if (!std::filesystem::exists(world_root / "world.json", error)) {
        error.clear();
        continue;
      }
      worlds.push_back({server_root.filename().string(),
                        world_root.filename().string(),
                        server_root,
                        world_root});
    }
  }

  std::sort(worlds.begin(), worlds.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.server_save_name != rhs.server_save_name) {
      return lhs.server_save_name < rhs.server_save_name;
    }
    return lhs.world_name < rhs.world_name;
  });
  return worlds;
}

Expected<void, PersistenceError> save_server_root(
    const std::filesystem::path& root,
    const SaveMetadata& save,
    const WorldMetadata& world,
    std::span<const SavedChunkSection> chunks,
    std::span<const PlayerSaveData> players,
    std::span<const entity::PersistentEntityRecord> entities,
    const voxel::BlockRegistry& block_registry,
    const player::ItemRegistry& item_registry) {
  const std::string save_name = save.name.empty() ? root.filename().string() : save.name;
  io::DataValue registry_refs = io::DataValue::object();
  registry_refs.set("block_registry_hash", save.block_registry_hash);
  registry_refs.set("item_registry_hash", save.item_registry_hash);
  io::DataValue save_json = io::DataValue::object();
  save_json.set("name", save_name);
  save_json.set("save_format_version", static_cast<u64>(save.save_format_version));
  save_json.set("game_version", save.game_version);
  save_json.set("registry_refs", std::move(registry_refs));
  auto save_result = write_json_atomic(root / "save.json", save_json, "save metadata JSON");
  if (!save_result) {
    return save_result;
  }

  save_result = ensure_permission_config_file(root);
  if (!save_result) {
    return save_result;
  }

  const world::WorldHeightSettings world_height =
      world::world_height_settings_from_generation_config(world.generation_config);
  world::WorldClock saved_clock = world.clock;
  saved_clock.world_tick = world.tick_index;
  io::DataValue world_registry_refs = io::DataValue::object();
  world_registry_refs.set("block_registry_hash", world.block_registry_hash);
  world_registry_refs.set("item_registry_hash", world.item_registry_hash);
  io::DataValue content_lock = io::DataValue::object();
  content_lock.set("enabled", world.content_lock_enabled);
  io::DataValue world_json = io::DataValue::object();
  world_json.set("name", world.name);
  world_json.set("seed", world.seed);
  world_json.set("tick_index", static_cast<u64>(world.tick_index));
  world_json.set("min_chunk_y", static_cast<i64>(world_height.min_chunk_y));
  world_json.set("max_chunk_y", static_cast<i64>(world_height.max_chunk_y));
  world_json.set("registry_refs", std::move(world_registry_refs));
  world_json.set("generation_config", world_generation_to_value(world.generation_config));
  world_json.set("time", world_clock_to_value(saved_clock));
  world_json.set("packs", required_packs_to_value(world.required_packs));
  world_json.set("content_lock", std::move(content_lock));
  if (world.spawn_point.has_value()) {
    world_json.set("spawn", location_to_value(world.spawn_point.value(), false));
  }
  save_result = write_json_atomic(
      root / "worlds" / world.name / "world.json",
      world_json,
      "world metadata JSON");
  if (!save_result) {
    return save_result;
  }

  HashMap<RegionCoord, std::vector<SavedChunkSection>> regions;
  for (const SavedChunkSection& chunk : chunks) {
    regions[chunk_column_to_region(chunk_to_column(chunk.coord))].push_back(chunk);
  }
  for (auto& [region, region_chunks] : regions) {
    const std::filesystem::path region_path =
        root / "worlds" / world.name / "regions" /
        ("r." + std::to_string(region.x) + "." + std::to_string(region.z) + ".arvr");
    auto region_result =
        write_region_file(region_path, region, region_chunks, block_registry, world_height.min_chunk_y);
    if (!region_result) {
      return region_result;
    }
  }

  for (const PlayerSaveData& player : players) {
    io::DataValue player_json = io::DataValue::object();
    player_json.set("profile_id", player.profile_id.string());
    player_json.set("profile_name", player.profile_name);
    player_json.set("display_name", player.display_name);
    player_json.set("world_name", player.world_name);
    player_json.set("position", vec3_to_value(player.position));
    player_json.set("velocity", vec3_to_value(player.velocity));
    player_json.set("yaw_radians", static_cast<f64>(player.yaw_radians));
    player_json.set("pitch_radians", static_cast<f64>(player.pitch_radians));
    player_json.set("grounded", player.grounded);
    player_json.set(
        "movement_mode",
        player.movement_mode == player::MovementMode::Flying ? "flying" : "walking");
    player_json.set("game_mode", player.game_mode_id);
    player_json.set("selected_skin_hash", player.selected_skin_hash);
    player_json.set("selected_skin_slim_model", player.selected_skin_slim_model);
    player_json.set("selected_hotbar_slot", static_cast<u64>(player.selected_hotbar_slot));
    player_json.set("inventory_version", static_cast<u64>(2U));
    player_json.set("inventory", inventory_to_value(player.inventory, item_registry));
    io::DataValue groups = io::DataValue::array();
    for (const std::string& group : player.permission_overrides.groups) {
      groups.push(group);
    }
    player_json.set("groups", std::move(groups));
    player_json.set(
        "permission_overrides",
        player_permissions_to_value(player.permission_overrides));
    auto player_result =
        write_json_atomic(player_file_path(root, player), player_json, "player save JSON");
    if (!player_result) {
      return player_result;
    }
  }

  const std::filesystem::path entity_root = root / "worlds" / world.name / "entities";
  std::error_code file_error;
  std::filesystem::remove(root / "worlds" / world.name / "entities.json", file_error);
  file_error.clear();
  std::filesystem::remove_all(entity_root, file_error);
  if (file_error) {
    return Expected<void, PersistenceError>::failure(
        PersistenceError{"failed to clear old entity persistence files: " + file_error.message()});
  }

  HashMap<RegionCoord, std::vector<entity::PersistentEntityRecord>> entity_regions;
  for (const entity::PersistentEntityRecord& entity : entities) {
    if (entity.world_id.value == 0U) {
      continue;
    }
    entity_regions[entity_region_for_position(entity.position)].push_back(entity);
  }
  for (const auto& [region, region_entities] : entity_regions) {
    io::DataValue entity_array = io::DataValue::array();
    for (const entity::PersistentEntityRecord& entity : region_entities) {
      entity_array.push(entity_record_to_value(entity, item_registry));
    }

    io::DataValue region_node = io::DataValue::object();
    region_node.set("x", static_cast<i64>(region.x));
    region_node.set("z", static_cast<i64>(region.z));
    io::DataValue entity_file = io::DataValue::object();
    entity_file.set("entity_version", static_cast<u64>(1U));
    entity_file.set("region", std::move(region_node));
    entity_file.set("entities", std::move(entity_array));
    auto entity_result = write_json_atomic(
        entity_region_file_path(root, world.name, region),
        entity_file,
        "entity region JSON");
    if (!entity_result) {
      return entity_result;
    }
  }

  return Expected<void, PersistenceError>::success();
}

Expected<void, PersistenceError> save_server_root(
    const std::filesystem::path& root,
    const SaveMetadata& save,
    const WorldMetadata& world,
    std::span<const SavedChunkSection> chunks,
    std::span<const PlayerSaveData> players,
    const voxel::BlockRegistry& block_registry,
    const player::ItemRegistry& item_registry) {
  return save_server_root(
      root,
      save,
      world,
      chunks,
      players,
      std::span<const entity::PersistentEntityRecord>{},
      block_registry,
      item_registry);
}

Expected<LoadedSaveData, PersistenceError> load_server_root(
    const std::filesystem::path& root,
    std::optional<std::string_view> requested_world_name,
    const voxel::BlockRegistry& block_registry,
    const player::ItemRegistry& item_registry) {
  LoadedSaveData loaded;
  try {
    if (!std::filesystem::exists(root / "save.json")) {
      return Expected<LoadedSaveData, PersistenceError>::success(std::move(loaded));
    }

    auto save_file = read_json_file(root / "save.json", "save metadata JSON");
    if (!save_file) {
      return Expected<LoadedSaveData, PersistenceError>::failure(save_file.error());
    }
    const io::DataValue save_json = std::move(save_file.value());
    loaded.save.name = string_value_or(save_json, "name", root.filename().string());
    if (loaded.save.name.empty()) {
      loaded.save.name = root.filename().string();
    }
    loaded.save.save_format_version =
        static_cast<u32>(u64_value_or(save_json, "save_format_version", 0U));
    loaded.save.game_version = string_value_or(save_json, "game_version", std::string{});
    if (const io::DataValue* refs = member(save_json, "registry_refs"); refs != nullptr) {
      loaded.save.block_registry_hash = u64_value_or(*refs, "block_registry_hash", 0U);
      loaded.save.item_registry_hash = u64_value_or(*refs, "item_registry_hash", 0U);
    }
    loaded.has_save_metadata = true;
    if (loaded.save.save_format_version != CurrentSaveFormatVersion) {
      return Expected<LoadedSaveData, PersistenceError>::failure(
          PersistenceError{"unsupported save format version: " +
                           std::to_string(loaded.save.save_format_version)});
    }

    const std::filesystem::path worlds_root = root / "worlds";
    std::filesystem::path world_file;
    if (requested_world_name.has_value() && !requested_world_name->empty()) {
      const std::filesystem::path path =
          worlds_root / std::string{requested_world_name.value()} / "world.json";
      if (std::filesystem::exists(path)) {
        world_file = path;
      }
    } else {
      const std::filesystem::path default_world = worlds_root / "default" / "world.json";
      if (std::filesystem::exists(default_world)) {
        world_file = default_world;
      }
    }
    if (world_file.empty() && std::filesystem::exists(worlds_root) &&
        (!requested_world_name.has_value() || requested_world_name->empty())) {
      for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(worlds_root)) {
        if (!entry.is_directory()) {
          continue;
        }
        const std::filesystem::path candidate = entry.path() / "world.json";
        if (std::filesystem::exists(candidate)) {
          world_file = candidate;
          break;
        }
      }
    }
    if (world_file.empty()) {
      return Expected<LoadedSaveData, PersistenceError>::success(std::move(loaded));
    }

    auto world_file_json = read_json_file(world_file, "world metadata JSON");
    if (!world_file_json) {
      return Expected<LoadedSaveData, PersistenceError>::failure(world_file_json.error());
    }
    const io::DataValue world_json = std::move(world_file_json.value());
    loaded.world.name = string_value_or(world_json, "name", std::string{});
    if (loaded.world.name.empty()) {
      loaded.world.name = world_file.parent_path().filename().string();
    }
    loaded.world.seed = u64_value_or(world_json, "seed", 0U);
    loaded.world.tick_index =
        static_cast<TickIndex>(u64_value_or(world_json, "tick_index", 0U));
    loaded.world.clock.world_tick = loaded.world.tick_index;
    if (const io::DataValue* refs = member(world_json, "registry_refs"); refs != nullptr) {
      loaded.world.block_registry_hash = u64_value_or(*refs, "block_registry_hash", 0U);
      loaded.world.item_registry_hash = u64_value_or(*refs, "item_registry_hash", 0U);
    }
    if (const io::DataValue* generation = member(world_json, "generation_config");
        generation != nullptr) {
      loaded.world.generation_config = world_generation_from_value(*generation);
    }
    const world::WorldHeightSettings loaded_world_height =
        world::world_height_settings_from_generation_config(loaded.world.generation_config);
    const i32 saved_region_min_chunk_y =
        static_cast<i32>(i64_value_or(
            world_json,
            "min_chunk_y",
            loaded_world_height.min_chunk_y));
    loaded.world.min_chunk_y = loaded_world_height.min_chunk_y;
    loaded.world.max_chunk_y = loaded_world_height.max_chunk_y;
    if (const io::DataValue* time = member(world_json, "time"); time != nullptr) {
      loaded.world.clock = world_clock_from_json(*time, loaded.world.tick_index);
      loaded.world.tick_index = loaded.world.clock.world_tick;
    }
    if (const io::DataValue* packs = member(world_json, "packs"); packs != nullptr) {
      loaded.world.required_packs = required_packs_from_value(*packs);
    }
    if (const io::DataValue* content_lock = member(world_json, "content_lock");
        content_lock != nullptr && content_lock->kind() == io::DataKind::Object) {
      loaded.world.content_lock_enabled =
          bool_value_or(*content_lock, "enabled", loaded.world.content_lock_enabled);
    }
    if (const io::DataValue* spawn = member(world_json, "spawn"); spawn != nullptr) {
      loaded.world.spawn_point = location_from_value(*spawn);
    }
    if (!loaded.world.spawn_point.has_value()) {
      loaded.world.spawn_point =
          read_primary_world_spawn(worlds_root, loaded.world.name);
    }
    loaded.has_world_metadata = true;

    const std::filesystem::path regions_dir = worlds_root / loaded.world.name / "regions";
    if (std::filesystem::exists(regions_dir)) {
      for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(regions_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".arvr") {
          continue;
        }

        auto region_result = read_region_file(entry.path(), block_registry, saved_region_min_chunk_y);
        if (!region_result) {
          return Expected<LoadedSaveData, PersistenceError>::failure(region_result.error());
        }
        const auto& region_chunks = region_result.value().chunks;
        loaded.chunks.insert(loaded.chunks.end(), region_chunks.begin(), region_chunks.end());
      }
    }

    auto load_entities_file = [&](const std::filesystem::path& path) -> Expected<void, PersistenceError> {
      auto entity_file = read_json_file(path, "entity region JSON");
      if (!entity_file) {
        return Expected<void, PersistenceError>::failure(entity_file.error());
      }
      const io::DataValue entity_json = std::move(entity_file.value());
      const io::DataValue* entities = member(entity_json, "entities");
      const io::DataValue::Array* entity_array =
          entities == nullptr ? nullptr : entities->array_if();
      if (entity_array != nullptr) {
        for (const io::DataValue& entry : *entity_array) {
          loaded.entities.emplace_back(entity_record_from_json(entry, item_registry));
        }
      }
      return Expected<void, PersistenceError>::success();
    };

    const std::filesystem::path entities_dir = worlds_root / loaded.world.name / "entities";
    if (std::filesystem::exists(entities_dir)) {
      for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(entities_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
          continue;
        }
        auto entity_result = load_entities_file(entry.path());
        if (!entity_result) {
          return Expected<LoadedSaveData, PersistenceError>::failure(entity_result.error());
        }
      }
    } else {
      const std::filesystem::path entities_file = worlds_root / loaded.world.name / "entities.json";
      if (std::filesystem::exists(entities_file)) {
        auto entity_result = load_entities_file(entities_file);
        if (!entity_result) {
          return Expected<LoadedSaveData, PersistenceError>::failure(entity_result.error());
        }
      }
    }

    const std::filesystem::path players_dir = root / "players";
    if (std::filesystem::exists(players_dir)) {
      for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(players_dir)) {
        if (!entry.is_regular_file()) {
          continue;
        }

        auto player_file = read_json_file(entry.path(), "player save JSON");
        if (!player_file) {
          return Expected<LoadedSaveData, PersistenceError>::failure(player_file.error());
        }
        const io::DataValue player_json = std::move(player_file.value());

        PlayerSaveData player_save;
        const std::string profile_id_text =
            string_value_or(player_json, "profile_id", std::string{});
        if (!profile_id_text.empty()) {
          auto profile_id = crypto::parse_player_profile_id(profile_id_text);
          if (!profile_id) {
            return Expected<LoadedSaveData, PersistenceError>::failure(
                PersistenceError{profile_id.error().message});
          }
          player_save.profile_id = profile_id.value();
        }
        player_save.profile_name =
            string_value_or(player_json, "profile_name", std::string{"local_player"});
        player_save.display_name =
            string_value_or(player_json, "display_name", player_save.profile_name);
        player_save.world_name = string_value_or(player_json, "world_name", std::string{});
        if (const io::DataValue* value = member(player_json, "position"); value != nullptr) {
          player_save.position = vec3_from_value(*value);
        }
        if (const io::DataValue* value = member(player_json, "velocity"); value != nullptr) {
          player_save.velocity = vec3_from_value(*value);
        }
        player_save.yaw_radians =
            static_cast<f32>(f64_value_or(player_json, "yaw_radians", 0.0));
        player_save.pitch_radians =
            static_cast<f32>(f64_value_or(player_json, "pitch_radians", 0.0));
        player_save.grounded = bool_value_or(player_json, "grounded", false);
        player_save.movement_mode =
            string_value_or(player_json, "movement_mode", std::string{}) == "flying"
                ? player::MovementMode::Flying
                : player::MovementMode::Walking;
        player_save.game_mode_id =
            string_value_or(player_json, "game_mode", player_save.game_mode_id);
        player_save.selected_skin_hash =
            string_value_or(player_json, "selected_skin_hash", std::string{});
        player_save.selected_skin_slim_model =
            !player_save.selected_skin_hash.empty() &&
            bool_value_or(player_json, "selected_skin_slim_model", false);
        player_save.permission_overrides = player_permissions_from_value(player_json);
        player_save.selected_hotbar_slot =
            static_cast<u8>(u64_value_or(player_json, "selected_hotbar_slot", 0U));
        if (const io::DataValue* inventory = member(player_json, "inventory");
            inventory != nullptr) {
          player_save.inventory = inventory_from_json(
              *inventory,
              item_registry,
              player::DefaultPlayerInventorySlotCount);
        }
        loaded.players.push_back(std::move(player_save));
      }
    }

    return Expected<LoadedSaveData, PersistenceError>::success(std::move(loaded));
  } catch (const std::exception& ex) {
    return Expected<LoadedSaveData, PersistenceError>::failure(
        PersistenceError{"failed to load save data: " + std::string{ex.what()}});
  }
}

Expected<LoadedSaveData, PersistenceError> load_server_root(
    const std::filesystem::path& root,
    const voxel::BlockRegistry& block_registry,
    const player::ItemRegistry& item_registry) {
  return load_server_root(root, std::optional<std::string_view>{}, block_registry, item_registry);
}

Expected<LoadedSaveData, PersistenceError> load_server_root(
    const std::filesystem::path& root,
    std::string_view world_name,
    const voxel::BlockRegistry& block_registry,
    const player::ItemRegistry& item_registry) {
  return load_server_root(root, std::optional<std::string_view>{world_name}, block_registry, item_registry);
}

} // namespace arv::persistence
