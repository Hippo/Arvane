#include "arv/persistence/save.hpp"

#include "atomic_file.hpp"

#include "arv/data/json_math.hpp"
#include "arv/player/permissions.hpp"
#include "arv/world/world_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace arv::persistence {
namespace {

using json = nlohmann::json;

[[nodiscard]] u64 fnv1a_append(u64 hash, std::string_view text) noexcept {
  for (const char ch : text) {
    hash ^= static_cast<u8>(ch);
    hash *= 1099511628211ULL;
  }
  return hash;
}

[[nodiscard]] json quat_to_json(Quatf value) {
  return json::array({value.x, value.y, value.z, value.w});
}

[[nodiscard]] Quatf json_to_quat(const json& node) {
  return {
      node.at(0).get<f32>(),
      node.at(1).get<f32>(),
      node.at(2).get<f32>(),
      node.at(3).get<f32>()};
}

[[nodiscard]] json item_stack_to_json(
    player::ItemStack stack,
    const player::ItemRegistry& registry) {
  const player::ItemRecord* item = registry.item(stack.item);
  return json{
      {"item", item == nullptr ? std::string{player::MissingItemName} : item->name},
      {"count", stack.count},
  };
}

[[nodiscard]] player::ItemStack item_stack_from_json(
    const json& node,
    const player::ItemRegistry& registry) {
  const player::ItemRecord* missing_item = registry.missing_item();
  const player::ItemID fallback = missing_item == nullptr ? player::AirItemID : missing_item->id;
  const std::optional<player::ItemID> item =
      registry.find_item(node.at("item").get<std::string>());
  return {
      item.value_or(fallback),
      node.at("count").get<u16>()};
}

[[nodiscard]] RegionCoord entity_region_for_position(Vec3f position) noexcept {
  const BlockPos block_pos{{
      static_cast<i32>(std::floor(position.x)),
      static_cast<i32>(std::floor(position.y)),
      static_cast<i32>(std::floor(position.z)),
  }};
  return chunk_column_to_region(chunk_to_column(block_to_chunk(block_pos)));
}

[[nodiscard]] json entity_record_to_json(
    const entity::PersistentEntityRecord& entity,
    const player::ItemRegistry& item_registry) {
  json components = json::object();
  if (entity.dropped_item_stack.has_value()) {
    components["dropped_item"] = {
        {"stack", item_stack_to_json(entity.dropped_item_stack.value(), item_registry)},
        {"pickup_delay_seconds", entity.dropped_item_pickup_delay_seconds},
    };
  }

  return json{
      {"uuid", entity.uuid},
      {"type", entity.type_name},
      {"version", 1},
      {"world_id", entity.world_id.value},
      {"position", data::vec3_to_json(entity.position)},
      {"rotation", quat_to_json(entity.rotation)},
      {"velocity", data::vec3_to_json(entity.velocity)},
      {"components", std::move(components)},
  };
}

[[nodiscard]] entity::PersistentEntityRecord entity_record_from_json(
    const json& entry,
    const player::ItemRegistry& item_registry) {
  entity::PersistentEntityRecord record;
  record.uuid = entry.at("uuid").get<std::string>();
  record.type_name = entry.at("type").get<std::string>();
  record.world_id = WorldID{entry.at("world_id").get<u32>()};
  record.position = data::json_to_vec3(entry.at("position"));
  record.rotation = json_to_quat(entry.at("rotation"));
  record.velocity = data::json_to_vec3(entry.at("velocity"));
  if (const auto components = entry.find("components");
      components != entry.end() && components->is_object()) {
    if (const auto dropped_item = components->find("dropped_item");
        dropped_item != components->end() && dropped_item->is_object()) {
      record.dropped_item_stack = item_stack_from_json(dropped_item->at("stack"), item_registry);
      if (const auto delay = dropped_item->find("pickup_delay_seconds");
          delay != dropped_item->end()) {
        record.dropped_item_pickup_delay_seconds = delay->get<Seconds>();
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

[[nodiscard]] json world_generation_to_json(const world::WorldGenerationConfig& config) {
  json surface_layers = json::array();
  for (const world::SurfaceLayerDefinition& layer : config.surface_layers) {
    json entry;
    entry["block_name"] = layer.block_name;
    entry["depth"] = layer.depth;
    entry["remaining"] = layer.remaining;
    surface_layers.push_back(std::move(entry));
  }

  return json{
      {"id", config.id},
      {"version", config.version},
      {"seed_salt", config.seed_salt},
      {"min_y", config.min_y},
      {"max_y", config.max_y},
      {"sea_level", config.sea_level},
      {"base_height", config.base_height},
      {"height_scale", config.height_scale},
      {"terrain_noise_type", config.terrain_noise_type},
      {"terrain_frequency", config.terrain_frequency},
      {"terrain_octaves", config.terrain_octaves},
      {"terrain_amplitude", config.terrain_amplitude},
      {"terrain_lacunarity", config.terrain_lacunarity},
      {"terrain_gain", config.terrain_gain},
      {"surface_layers", std::move(surface_layers)},
  };
}

[[nodiscard]] json required_packs_to_json(
    std::span<const RequiredPackMetadata> packs) {
  json required = json::array();
  for (const RequiredPackMetadata& pack : packs) {
    required.push_back(pack.id);
  }
  return json{{"required", std::move(required)}};
}

[[nodiscard]] std::vector<RequiredPackMetadata> required_packs_from_json(const json& node) {
  std::vector<RequiredPackMetadata> packs;
  if (!node.is_object()) {
    return packs;
  }
  const auto required = node.find("required");
  if (required == node.end() || !required->is_array()) {
    return packs;
  }
  packs.reserve(required->size());
  for (const json& entry : *required) {
    RequiredPackMetadata pack;
    if (entry.is_string()) {
      pack.id = entry.get<std::string>();
    } else if (entry.is_object()) {
      // Older saves may include version/hash metadata. Worlds only need the pack ID.
      pack.id = entry.value("id", std::string{});
    } else {
      continue;
    }
    if (!pack.id.empty()) {
      packs.push_back(std::move(pack));
    }
  }
  return packs;
}

[[nodiscard]] world::WorldGenerationConfig world_generation_from_json(const json& node) {
  world::WorldGenerationConfig config;
  config.id = node.at("id").get<std::string>();
  config.version = node.at("version").get<u32>();
  config.seed_salt = node.at("seed_salt").get<std::string>();
  config.min_y = node.at("min_y").get<i32>();
  config.max_y = node.at("max_y").get<i32>();
  config.sea_level = node.at("sea_level").get<i32>();
  config.base_height = node.at("base_height").get<i32>();
  config.height_scale = node.at("height_scale").get<i32>();
  config.terrain_noise_type = node.at("terrain_noise_type").get<std::string>();
  config.terrain_frequency = node.at("terrain_frequency").get<f32>();
  config.terrain_octaves = node.at("terrain_octaves").get<u32>();
  config.terrain_amplitude = node.at("terrain_amplitude").get<f32>();
  config.terrain_lacunarity = node.at("terrain_lacunarity").get<f32>();
  config.terrain_gain = node.at("terrain_gain").get<f32>();
  for (const json& entry : node.at("surface_layers")) {
    config.surface_layers.push_back({entry.at("block_name").get<std::string>(),
                                     entry.at("depth").get<i32>(),
                                     entry.at("remaining").get<bool>()});
  }
  return config;
}

[[nodiscard]] json world_clock_to_json(const world::WorldClock& clock) {
  return json{
      {"world_tick", clock.world_tick},
      {"day_time_tick", clock.day_time_tick},
      {"day_length_seconds", clock.day_length_seconds},
      {"daylight_cycle", clock.daylight_cycle},
      {"time_scale", clock.time_scale},
  };
}

[[nodiscard]] world::WorldClock world_clock_from_json(
    const json& node,
    TickIndex fallback_world_tick) {
  world::WorldClock clock = world::make_world_clock();
  clock.world_tick = fallback_world_tick;
  if (!node.is_object()) {
    return clock;
  }
  if (const auto value = node.find("world_tick"); value != node.end()) {
    clock.world_tick = value->get<TickIndex>();
  }
  if (const auto value = node.find("day_length_seconds"); value != node.end()) {
    clock.day_length_seconds = value->get<Seconds>();
  }
  if (const auto value = node.find("daylight_cycle"); value != node.end()) {
    clock.daylight_cycle = value->get<bool>();
  }
  if (const auto value = node.find("time_scale"); value != node.end()) {
    clock.time_scale = value->get<f32>();
  }
  world::retime_world_clock(clock, DefaultBaseTps);
  if (const auto value = node.find("day_time_tick"); value != node.end()) {
    world::set_day_time_tick(clock, value->get<u32>());
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

[[nodiscard]] json inventory_to_json(
    const player::Inventory& inventory,
    const player::ItemRegistry& registry) {
  HashMap<std::string, u16> indices;
  std::vector<std::string> table{std::string{player::AirItemName}};
  indices.emplace(std::string{player::AirItemName}, 0U);

  json slots = json::array();
  for (const std::optional<player::ItemStack>& slot : inventory.slots()) {
    if (!slot.has_value()) {
      slots.push_back(nullptr);
      continue;
    }

    const player::ItemRecord* item = registry.item(slot->item);
    const std::string item_name = item == nullptr ? std::string{player::MissingItemName} : item->name;
    auto [iter, inserted] = indices.emplace(item_name, static_cast<u16>(table.size()));
    if (inserted) {
      table.push_back(item_name);
    }

    slots.push_back(
        json{{"item", iter->second}, {"count", slot->count}});
  }

  return json{{"item_table", std::move(table)}, {"slots", std::move(slots)}};
}

[[nodiscard]] player::Inventory inventory_from_json(
    const json& node,
    const player::ItemRegistry& registry,
    usize slot_count) {
  player::Inventory inventory(slot_count);
  std::vector<std::optional<player::ItemID>> item_ids;
  const player::ItemRecord* missing_item = registry.missing_item();
  const player::ItemID fallback = missing_item == nullptr ? player::AirItemID : missing_item->id;

  for (const json& entry : node.at("item_table")) {
    const auto resolved = registry.find_item(entry.get<std::string>());
    item_ids.push_back(resolved.has_value() ? resolved.value() : fallback);
  }

  const json& slots = node.at("slots");
  for (usize index = 0; index < slots.size() && index < slot_count; ++index) {
    if (slots[index].is_null()) {
      continue;
    }

    const u16 item_index = slots[index].at("item").get<u16>();
    const u16 count = slots[index].at("count").get<u16>();
    if (item_index >= item_ids.size()) {
      continue;
    }
    inventory.set_slot(index, player::ItemStack{item_ids[item_index].value_or(fallback), count});
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

[[nodiscard]] std::optional<player::TriState> tri_state_from_json(const json& node) {
  if (node.is_boolean()) {
    return node.get<bool>() ? player::TriState::Allow : player::TriState::Deny;
  }
  if (!node.is_string()) {
    return std::nullopt;
  }

  return player::tri_state_from_string(node.get<std::string>());
}

void write_core_value(json& node, std::string_view name, player::TriState value) {
  if (value != player::TriState::Unset) {
    node[std::string{name}] = tri_state_name(value);
  }
}

[[nodiscard]] json core_permissions_to_json(const player::CorePermissionSet& core) {
  json node = json::object();
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

[[nodiscard]] json custom_permissions_to_json(const player::CustomPermissionMap& custom) {
  json node = json::object();
  for (const auto& [permission, value] : custom) {
    if (value != player::TriState::Unset) {
      node[permission] = tri_state_name(value);
    }
  }
  return node;
}

[[nodiscard]] json player_permissions_to_json(
    const player::PlayerPermissions& permissions) {
  return json{
      {"core", core_permissions_to_json(permissions.overrides)},
      {"permissions", custom_permissions_to_json(permissions.custom_overrides)},
  };
}

void read_group_list(player::PlayerPermissions& permissions, const json& node) {
  if (!node.is_array()) {
    return;
  }
  for (const json& entry : node) {
    if (entry.is_string()) {
      permissions.groups.push_back(entry.get<std::string>());
    }
  }
}

void read_core_permissions(player::PlayerPermissions& permissions, const json& node) {
  if (!node.is_object()) {
    return;
  }
  for (auto iter = node.begin(); iter != node.end(); ++iter) {
    const std::optional<player::TriState> value = tri_state_from_json(iter.value());
    if (value.has_value()) {
      (void)player::set_core_permission_by_name(
          permissions.overrides,
          iter.key(),
          value.value());
    }
  }
}

void read_custom_permissions(player::PlayerPermissions& permissions, const json& node) {
  if (!node.is_object()) {
    return;
  }
  for (auto iter = node.begin(); iter != node.end(); ++iter) {
    const std::optional<player::TriState> value = tri_state_from_json(iter.value());
    if (value.has_value() && player::is_valid_custom_permission_node(iter.key())) {
      permissions.custom_overrides[iter.key()] = value.value();
    }
  }
}

[[nodiscard]] player::PlayerPermissions player_permissions_from_json(const json& player_json) {
  player::PlayerPermissions permissions;
  if (const auto groups = player_json.find("groups"); groups != player_json.end()) {
    read_group_list(permissions, *groups);
  }

  if (const auto overrides = player_json.find("permission_overrides");
      overrides != player_json.end() && overrides->is_object()) {
    if (const auto groups = overrides->find("groups"); groups != overrides->end()) {
      read_group_list(permissions, *groups);
    }
    if (const auto core = overrides->find("core"); core != overrides->end()) {
      read_core_permissions(permissions, *core);
    }
    if (const auto custom = overrides->find("permissions"); custom != overrides->end()) {
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

  return write_text_atomic(
      path,
      "# Permission groups for this server save.\n"
      "groups: {}\n");
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
    if (auto save_text = read_text_file(save_file); save_text) {
      try {
        const json save_json = json::parse(save_text.value());
        if (save_json.contains("name") && save_json.at("name").is_string()) {
          const std::string parsed_name = save_json.at("name").get<std::string>();
          if (!parsed_name.empty()) {
            save_name = parsed_name;
          }
        }
      } catch (const json::exception&) {
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
  const json save_json{
      {"name", save_name},
      {"save_format_version", save.save_format_version},
      {"game_version", save.game_version},
      {"registry_refs",
       {{"block_registry_hash", save.block_registry_hash},
        {"item_registry_hash", save.item_registry_hash}}},
  };
  auto save_result = write_text_atomic(root / "save.json", save_json.dump(2));
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
  const json world_json{
      {"name", world.name},
      {"seed", world.seed},
      {"tick_index", world.tick_index},
      {"min_chunk_y", world_height.min_chunk_y},
      {"max_chunk_y", world_height.max_chunk_y},
      {"registry_refs",
       {{"block_registry_hash", world.block_registry_hash},
        {"item_registry_hash", world.item_registry_hash}}},
      {"generation_config", world_generation_to_json(world.generation_config)},
      {"time", world_clock_to_json(saved_clock)},
      {"packs", required_packs_to_json(world.required_packs)},
      {"content_lock", {{"enabled", world.content_lock_enabled}}},
  };
  save_result = write_text_atomic(root / "worlds" / world.name / "world.json", world_json.dump(2));
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
    json player_json{
        {"profile_id", player.profile_id.string()},
        {"profile_name", player.profile_name},
        {"display_name", player.display_name},
        {"world_name", player.world_name},
        {"position", data::vec3_to_json(player.position)},
        {"velocity", data::vec3_to_json(player.velocity)},
        {"yaw_radians", player.yaw_radians},
        {"pitch_radians", player.pitch_radians},
        {"grounded", player.grounded},
        {"movement_mode", player.movement_mode == player::MovementMode::Flying ? "flying" : "walking"},
        {"game_mode", player.game_mode_id},
        {"selected_skin_hash", player.selected_skin_hash},
        {"selected_skin_slim_model", player.selected_skin_slim_model},
        {"selected_hotbar_slot", player.selected_hotbar_slot},
        {"inventory_version", 1},
        {"inventory", inventory_to_json(player.inventory, item_registry)},
    };
    player_json["groups"] = player.permission_overrides.groups;
    player_json["permission_overrides"] =
        player_permissions_to_json(player.permission_overrides);
    auto player_result = write_text_atomic(player_file_path(root, player), player_json.dump(2));
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
    json entity_array = json::array();
    for (const entity::PersistentEntityRecord& entity : region_entities) {
      entity_array.push_back(entity_record_to_json(entity, item_registry));
    }

    auto entity_result = write_text_atomic(
        entity_region_file_path(root, world.name, region),
        json{
            {"entity_version", 1},
            {"region", {{"x", region.x}, {"z", region.z}}},
            {"entities", std::move(entity_array)}}
            .dump(2));
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

    auto save_text = read_text_file(root / "save.json");
    if (!save_text) {
      return Expected<LoadedSaveData, PersistenceError>::failure(save_text.error());
    }
    const json save_json = json::parse(save_text.value());
    if (save_json.contains("name") && save_json.at("name").is_string()) {
      loaded.save.name = save_json.at("name").get<std::string>();
    } else {
      loaded.save.name = root.filename().string();
    }
    if (loaded.save.name.empty()) {
      loaded.save.name = root.filename().string();
    }
    loaded.save.save_format_version = save_json.at("save_format_version").get<u32>();
    loaded.save.game_version = save_json.at("game_version").get<std::string>();
    loaded.save.block_registry_hash = save_json.at("registry_refs").at("block_registry_hash").get<u64>();
    loaded.save.item_registry_hash = save_json.at("registry_refs").at("item_registry_hash").get<u64>();
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

    auto world_text = read_text_file(world_file);
    if (!world_text) {
      return Expected<LoadedSaveData, PersistenceError>::failure(world_text.error());
    }
    const json world_json = json::parse(world_text.value());
    loaded.world.name = world_json.at("name").get<std::string>();
    loaded.world.seed = world_json.at("seed").get<u64>();
    loaded.world.tick_index = world_json.at("tick_index").get<TickIndex>();
    loaded.world.clock.world_tick = loaded.world.tick_index;
    loaded.world.block_registry_hash =
        world_json.at("registry_refs").at("block_registry_hash").get<u64>();
    loaded.world.item_registry_hash =
        world_json.at("registry_refs").at("item_registry_hash").get<u64>();
    loaded.world.generation_config =
        world_generation_from_json(world_json.at("generation_config"));
    const world::WorldHeightSettings loaded_world_height =
        world::world_height_settings_from_generation_config(loaded.world.generation_config);
    const i32 saved_region_min_chunk_y =
        world_json.contains("min_chunk_y")
            ? world_json.at("min_chunk_y").get<i32>()
            : loaded_world_height.min_chunk_y;
    loaded.world.min_chunk_y = loaded_world_height.min_chunk_y;
    loaded.world.max_chunk_y = loaded_world_height.max_chunk_y;
    if (const auto time = world_json.find("time"); time != world_json.end()) {
      loaded.world.clock = world_clock_from_json(*time, loaded.world.tick_index);
      loaded.world.tick_index = loaded.world.clock.world_tick;
    }
    if (const auto packs = world_json.find("packs"); packs != world_json.end()) {
      loaded.world.required_packs = required_packs_from_json(*packs);
    }
    if (const auto content_lock = world_json.find("content_lock");
        content_lock != world_json.end() && content_lock->is_object()) {
      loaded.world.content_lock_enabled =
          content_lock->value("enabled", loaded.world.content_lock_enabled);
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
      auto entity_text = read_text_file(path);
      if (!entity_text) {
        return Expected<void, PersistenceError>::failure(entity_text.error());
      }
      const json entity_json = json::parse(entity_text.value());
      for (const json& entry : entity_json.at("entities")) {
        loaded.entities.emplace_back(entity_record_from_json(entry, item_registry));
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

        auto player_text = read_text_file(entry.path());
        if (!player_text) {
          return Expected<LoadedSaveData, PersistenceError>::failure(player_text.error());
        }
        const json player_json = json::parse(player_text.value());

        PlayerSaveData player_save;
        const std::string profile_id_text = player_json.value("profile_id", std::string{});
        if (!profile_id_text.empty()) {
          auto profile_id = crypto::parse_player_profile_id(profile_id_text);
          if (!profile_id) {
            return Expected<LoadedSaveData, PersistenceError>::failure(
                PersistenceError{profile_id.error().message});
          }
          player_save.profile_id = profile_id.value();
        }
        player_save.profile_name = player_json.value("profile_name", std::string{"local_player"});
        player_save.display_name = player_json.value("display_name", player_save.profile_name);
        player_save.world_name = player_json.at("world_name").get<std::string>();
        player_save.position = data::json_to_vec3(player_json.at("position"));
        player_save.velocity = data::json_to_vec3(player_json.at("velocity"));
        player_save.yaw_radians = player_json.at("yaw_radians").get<f32>();
        player_save.pitch_radians = player_json.at("pitch_radians").get<f32>();
        player_save.grounded = player_json.at("grounded").get<bool>();
        player_save.movement_mode =
            player_json.at("movement_mode").get<std::string>() == "flying"
                ? player::MovementMode::Flying
                : player::MovementMode::Walking;
        if (player_json.contains("game_mode")) {
          player_save.game_mode_id = player_json.at("game_mode").get<std::string>();
        }
        player_save.selected_skin_hash =
            player_json.value("selected_skin_hash", std::string{});
        player_save.selected_skin_slim_model =
            !player_save.selected_skin_hash.empty() &&
            player_json.value("selected_skin_slim_model", false);
        player_save.permission_overrides = player_permissions_from_json(player_json);
        player_save.selected_hotbar_slot = player_json.at("selected_hotbar_slot").get<u8>();
        player_save.inventory = inventory_from_json(
            player_json.at("inventory"),
            item_registry,
            player::DefaultPlayerInventorySlotCount);
        loaded.players.push_back(std::move(player_save));
      }
    }

    return Expected<LoadedSaveData, PersistenceError>::success(std::move(loaded));
  } catch (const json::exception& ex) {
    return Expected<LoadedSaveData, PersistenceError>::failure(
        PersistenceError{"failed to parse save JSON: " + std::string{ex.what()}});
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
