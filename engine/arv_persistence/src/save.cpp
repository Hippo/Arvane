#include "arv/persistence/save.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_map>

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

[[nodiscard]] json vec3_to_json(Vec3f value) {
  return json::array({value.x, value.y, value.z});
}

[[nodiscard]] Vec3f json_to_vec3(const json& node) {
  return {
      node.at(0).get<f32>(),
      node.at(1).get<f32>(),
      node.at(2).get<f32>()};
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
    config.surface_layers.push_back({
        entry.at("block_name").get<std::string>(),
        entry.at("depth").get<i32>(),
        entry.at("remaining").get<bool>()});
  }
  return config;
}

[[nodiscard]] std::filesystem::path temporary_path_for(const std::filesystem::path& path) {
  return path.parent_path() / (path.filename().string() + ".tmp");
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

  const std::filesystem::path temp_path = temporary_path_for(path);
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
  std::unordered_map<std::string, u16> indices;
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

[[nodiscard]] std::filesystem::path player_file_path(
    const std::filesystem::path& root,
    std::string_view profile_name) {
  // TODO(arvane): Replace profile-name filenames with stable player UUID files
  // once the documented global profile model exists.
  return root / "players" / (std::string{profile_name} + ".dat");
}

} // namespace

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

Expected<void, PersistenceError> save_server_root(
    const std::filesystem::path& root,
    const SaveMetadata& save,
    const WorldMetadata& world,
    std::span<const SavedChunkSection> chunks,
    std::span<const PlayerSaveData> players,
    const voxel::BlockRegistry& block_registry,
    const player::ItemRegistry& item_registry) {
  const json save_json{
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

  const json world_json{
      {"name", world.name},
      {"seed", world.seed},
      {"tick_index", world.tick_index},
      {"min_chunk_y", world.min_chunk_y},
      {"max_chunk_y", world.max_chunk_y},
      {"registry_refs",
       {{"block_registry_hash", world.block_registry_hash},
        {"item_registry_hash", world.item_registry_hash}}},
      {"generation_config", world_generation_to_json(world.generation_config)},
  };
  save_result = write_text_atomic(root / "worlds" / world.name / "world.json", world_json.dump(2));
  if (!save_result) {
    return save_result;
  }

  std::unordered_map<RegionCoord, std::vector<SavedChunkSection>> regions;
  for (const SavedChunkSection& chunk : chunks) {
    regions[chunk_column_to_region(chunk_to_column(chunk.coord))].push_back(chunk);
  }
  for (auto& [region, region_chunks] : regions) {
    const std::filesystem::path region_path =
        root / "worlds" / world.name / "regions" /
        ("r." + std::to_string(region.x) + "." + std::to_string(region.z) + ".arvr");
    auto region_result = write_region_file(region_path, region, region_chunks, block_registry, world.min_chunk_y);
    if (!region_result) {
      return region_result;
    }
  }

  for (const PlayerSaveData& player : players) {
    const json player_json{
        {"profile_name", player.profile_name},
        {"world_name", player.world_name},
        {"position", vec3_to_json(player.position)},
        {"velocity", vec3_to_json(player.velocity)},
        {"yaw_radians", player.yaw_radians},
        {"pitch_radians", player.pitch_radians},
        {"grounded", player.grounded},
        {"movement_mode", player.movement_mode == player::MovementMode::Flying ? "flying" : "walking"},
        {"selected_hotbar_slot", player.selected_hotbar_slot},
        {"inventory_version", 1},
        {"inventory", inventory_to_json(player.inventory, item_registry)},
    };
    auto player_result = write_text_atomic(player_file_path(root, player.profile_name), player_json.dump(2));
    if (!player_result) {
      return player_result;
    }
  }

  return Expected<void, PersistenceError>::success();
}

Expected<LoadedSaveData, PersistenceError> load_server_root(
    const std::filesystem::path& root,
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
    for (const std::string_view candidate : {std::string_view{"default"}}) {
      const std::filesystem::path path = worlds_root / candidate / "world.json";
      if (std::filesystem::exists(path)) {
        world_file = path;
        break;
      }
    }
    if (world_file.empty() && std::filesystem::exists(worlds_root)) {
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
    loaded.world.min_chunk_y = world_json.at("min_chunk_y").get<i32>();
    loaded.world.max_chunk_y = world_json.at("max_chunk_y").get<i32>();
    loaded.world.block_registry_hash =
        world_json.at("registry_refs").at("block_registry_hash").get<u64>();
    loaded.world.item_registry_hash =
        world_json.at("registry_refs").at("item_registry_hash").get<u64>();
    loaded.world.generation_config =
        world_generation_from_json(world_json.at("generation_config"));
    loaded.has_world_metadata = true;

    const std::filesystem::path regions_dir = worlds_root / loaded.world.name / "regions";
    if (std::filesystem::exists(regions_dir)) {
      for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(regions_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".arvr") {
          continue;
        }

        auto region_result = read_region_file(entry.path(), block_registry, loaded.world.min_chunk_y);
        if (!region_result) {
          return Expected<LoadedSaveData, PersistenceError>::failure(region_result.error());
        }
        const auto& region_chunks = region_result.value().chunks;
        loaded.chunks.insert(loaded.chunks.end(), region_chunks.begin(), region_chunks.end());
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
        player_save.profile_name = player_json.at("profile_name").get<std::string>();
        player_save.world_name = player_json.at("world_name").get<std::string>();
        player_save.position = json_to_vec3(player_json.at("position"));
        player_save.velocity = json_to_vec3(player_json.at("velocity"));
        player_save.yaw_radians = player_json.at("yaw_radians").get<f32>();
        player_save.pitch_radians = player_json.at("pitch_radians").get<f32>();
        player_save.grounded = player_json.at("grounded").get<bool>();
        player_save.movement_mode =
            player_json.at("movement_mode").get<std::string>() == "flying"
                ? player::MovementMode::Flying
                : player::MovementMode::Walking;
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

} // namespace arv::persistence
