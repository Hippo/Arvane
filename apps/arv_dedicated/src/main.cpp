#include "arv/core/fixed_timestep.hpp"
#include "arv/core/log.hpp"
#include "arv/core/coordinates.hpp"
#include "arv/entity/components.hpp"
#include "arv/io/config.hpp"
#include "arv/net/gns_transport.hpp"
#include "arv/persistence/save.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/server/dedicated_lifetime.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/sched/scheduler.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/fluid.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <csignal>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::atomic_bool g_shutdown_requested{false};

void handle_shutdown_signal(int) {
  g_shutdown_requested.store(true);
}

[[nodiscard]] arv::u64 generated_world_seed() noexcept {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return static_cast<arv::u64>(now == 0 ? 1 : now);
}

[[nodiscard]] std::pair<arv::i32, arv::i32> visible_worldgen_chunk_y_range(
    const arv::world::WorldGenerationConfig& config) {
  const arv::f32 height_extent =
      std::abs(static_cast<arv::f32>(config.height_scale)) * config.terrain_amplitude + 4.0F;
  const arv::i32 surface_min =
      static_cast<arv::i32>(std::floor(static_cast<arv::f32>(config.base_height) - height_extent));
  const arv::i32 surface_max =
      static_cast<arv::i32>(std::ceil(static_cast<arv::f32>(config.base_height) + height_extent));
  const arv::i32 min_y = std::max(config.min_y, surface_min - arv::ChunkSize);
  const arv::i32 max_y = std::min(config.max_y, surface_max);
  return {arv::floor_div(min_y, arv::ChunkSize), arv::floor_div(max_y, arv::ChunkSize)};
}

[[nodiscard]] std::vector<arv::persistence::SavedChunkSection> capture_saved_chunks(
    const arv::world::WorldRuntime& world) {
  std::vector<arv::ChunkCoord> coords = world.loaded_chunk_coords();
  std::sort(coords.begin(), coords.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.value.x != rhs.value.x) {
      return lhs.value.x < rhs.value.x;
    }
    if (lhs.value.z != rhs.value.z) {
      return lhs.value.z < rhs.value.z;
    }
    return lhs.value.y < rhs.value.y;
  });

  std::vector<arv::persistence::SavedChunkSection> chunks;
  chunks.reserve(coords.size());
  for (const arv::ChunkCoord coord : coords) {
    const arv::voxel::ChunkSection* chunk = world.find_chunk(coord);
    if (chunk == nullptr) {
      continue;
    }

    arv::persistence::SavedChunkSection saved;
    saved.coord = coord;
    saved.blocks = chunk->blocks();
    saved.lights = chunk->lights();
    saved.fluids = chunk->fluids();
    saved.lighting_valid = !chunk->is_dirty(arv::voxel::ChunkDirtyFlags::NeedsRelight);
    chunks.emplace_back(std::move(saved));
  }
  return chunks;
}

[[nodiscard]] arv::server::ServerPlayerSaveState to_server_player_save(
    const arv::persistence::PlayerSaveData& player) {
  arv::server::ServerPlayerSaveState save;
  save.profile_name = player.profile_name;
  save.world_name = player.world_name;
  save.position = player.position;
  save.velocity = player.velocity;
  save.yaw_radians = player.yaw_radians;
  save.pitch_radians = player.pitch_radians;
  save.grounded = player.grounded;
  save.movement_mode = player.movement_mode;
  save.game_mode_id = player.game_mode_id;
  save.selected_hotbar_slot = player.selected_hotbar_slot;
  save.inventory = player.inventory;
  return save;
}

[[nodiscard]] arv::persistence::PlayerSaveData to_persistence_player_save(
    const arv::server::ServerPlayerSaveState& player) {
  arv::persistence::PlayerSaveData save;
  save.profile_name = player.profile_name;
  save.world_name = player.world_name;
  save.position = player.position;
  save.velocity = player.velocity;
  save.yaw_radians = player.yaw_radians;
  save.pitch_radians = player.pitch_radians;
  save.grounded = player.grounded;
  save.movement_mode = player.movement_mode;
  save.game_mode_id = player.game_mode_id;
  save.selected_hotbar_slot = player.selected_hotbar_slot;
  save.inventory = player.inventory;
  return save;
}

void upsert_player_save(
    std::vector<arv::persistence::PlayerSaveData>& players,
    arv::persistence::PlayerSaveData save) {
  const auto found = std::find_if(
      players.begin(),
      players.end(),
      [&](const arv::persistence::PlayerSaveData& existing) {
        return existing.profile_name == save.profile_name &&
               existing.world_name == save.world_name;
      });
  if (found == players.end()) {
    players.emplace_back(std::move(save));
  } else {
    *found = std::move(save);
  }
}

[[nodiscard]] std::vector<arv::persistence::PlayerSaveData> capture_player_saves(
    const arv::server::ServerRuntime& server,
    std::string_view world_name) {
  std::vector<arv::persistence::PlayerSaveData> players;
  for (const arv::server::ServerPlayerSaveState& save : server.saved_player_states()) {
    upsert_player_save(players, to_persistence_player_save(save));
  }

  for (const arv::SessionID session_id : server.sessions().session_ids()) {
    const arv::server::ServerSession* session = server.sessions().find(session_id);
    if (session == nullptr) {
      continue;
    }

    const arv::player::PlayerState* player_state =
        server.voxel_world().find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    arv::persistence::PlayerSaveData save;
    save.profile_name = session->profile_name;
    save.world_name = std::string{world_name};
    save.position = player_state->position;
    save.velocity = player_state->velocity;
    save.yaw_radians = player_state->yaw_radians;
    save.pitch_radians = player_state->pitch_radians;
    save.grounded = player_state->grounded;
    save.movement_mode = player_state->movement_mode;
    save.game_mode_id = player_state->game_mode_id;
    save.selected_hotbar_slot = player_state->selected_hotbar_slot;
    save.inventory = player_state->inventory;
    upsert_player_save(players, std::move(save));
  }

  std::sort(players.begin(), players.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.world_name != rhs.world_name) {
      return lhs.world_name < rhs.world_name;
    }
    return lhs.profile_name < rhs.profile_name;
  });
  return players;
}

[[nodiscard]] arv::player::PlayerTuning make_player_tuning(
    const arv::io::PhysicsConfig& physics) noexcept {
  arv::player::PlayerTuning tuning = arv::player::default_player_tuning();
  tuning.standing_eye_height = physics.standing_eye_height;
  tuning.sneaking_eye_height = physics.sneaking_eye_height;
  tuning.swimming_eye_height = physics.swimming_eye_height;
  tuning.eye_height_lerp_per_second = physics.eye_height_lerp_per_second;
  tuning.reach_distance = physics.reach_distance;
  tuning.mouse_sensitivity_radians = physics.mouse_sensitivity_radians;
  tuning.walk_speed = physics.walk_speed;
  tuning.sprint_speed = physics.sprint_speed;
  tuning.sneak_speed = physics.sneak_speed;
  tuning.air_speed = physics.air_speed;
  tuning.fly_speed = physics.fly_speed;
  tuning.fly_vertical_speed = physics.fly_vertical_speed;
  tuning.jump_velocity = physics.jump_velocity;
  tuning.gravity_per_second = physics.gravity_per_second;
  tuning.terminal_velocity = physics.terminal_velocity;
  tuning.body_width = physics.body_width;
  tuning.body_height = physics.body_height;
  tuning.sneaking_body_height = physics.sneaking_body_height;
  tuning.swimming_body_width = physics.swimming_body_width;
  tuning.swimming_body_height = physics.swimming_body_height;
  tuning.sneak_support_probe_depth = physics.sneak_support_probe_depth;
  tuning.collision_epsilon = physics.collision_epsilon;
  tuning.surface_swim_speed = physics.surface_swim_speed;
  tuning.underwater_swim_speed = physics.underwater_swim_speed;
  tuning.fast_surface_swim_speed = physics.fast_surface_swim_speed;
  tuning.fast_underwater_swim_speed = physics.fast_underwater_swim_speed;
  tuning.upward_swim_acceleration = physics.upward_swim_acceleration;
  tuning.downward_swim_acceleration = physics.downward_swim_acceleration;
  tuning.liquid_drag = physics.liquid_drag;
  tuning.surface_float_depth = physics.surface_float_depth;
  tuning.surface_buoyancy = physics.surface_buoyancy;
  tuning.underwater_buoyancy = physics.underwater_buoyancy;
  tuning.breath_seconds = physics.breath_seconds;
  return tuning;
}

struct DedicatedLaunchOptions {
  arv::io::ConfigLoadOptions config{};
  arv::u64 max_ticks{};
};

DedicatedLaunchOptions parse_launch_options(int argc, char** argv) {
  DedicatedLaunchOptions options;
  options.config.profile = arv::io::ConfigProfile::Server;
  bool explicit_config = false;

  for (int i = 1; i + 1 < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--ticks") {
      options.max_ticks = static_cast<arv::u64>(std::strtoull(argv[++i], nullptr, 10));
    } else if (arg == "--config") {
      options.config.explicit_config_path = std::filesystem::path{argv[++i]};
      explicit_config = true;
    } else if (arg == "--arvane-home") {
      options.config.explicit_arvane_home_path = std::filesystem::path{argv[++i]};
    } else if (arg == "--project-root") {
      options.config.project_root = std::filesystem::path{argv[++i]};
    }
  }

  options.config.missing_config_is_error = explicit_config;
  return options;
}

void transition(
    arv::server::DedicatedServerLifetime& lifetime,
    arv::server::DedicatedServerLifetimeState state) {
  lifetime.transition(state);
  arv::core::info("dedicated", arv::server::to_string(state));
}

} // namespace

int main(int argc, char** argv) {
  arv::server::DedicatedServerLifetime lifetime;
  std::signal(SIGINT, handle_shutdown_signal);
  std::signal(SIGTERM, handle_shutdown_signal);

  transition(lifetime, arv::server::DedicatedServerLifetimeState::Starting);

  const DedicatedLaunchOptions launch_options = parse_launch_options(argc, argv);

  transition(lifetime, arv::server::DedicatedServerLifetimeState::LoadingConfig);
  auto config_result = arv::io::load_config(launch_options.config);
  if (!config_result) {
    arv::core::error("config", config_result.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }
  const arv::io::EngineConfig config = config_result.value();
  arv::core::info(
      "dedicated",
      "config={} arvane_home={} world={} listen={}:{} save_root={}",
      config.paths.config_file.string(),
      config.paths.arvane_home_path().string(),
      config.server.world,
      config.network.bind_address,
      config.network.port,
      config.paths.dedicated_server_save_path().string());

  transition(lifetime, arv::server::DedicatedServerLifetimeState::LoadingRegistries);
  auto block_definition_text = arv::io::read_text_file(
      config.paths.config_path() / "blocks.json",
      "dedicated block definitions");
  if (!block_definition_text) {
    arv::core::error("blocks", block_definition_text.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  auto block_definitions =
      arv::voxel::parse_block_definitions_json(block_definition_text.value());
  if (!block_definitions) {
    arv::core::error("blocks", block_definitions.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  auto block_registry_result =
      arv::voxel::BlockRegistry::build(std::move(block_definitions.value()));
  if (!block_registry_result) {
    arv::core::error("blocks", block_registry_result.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }
  const arv::voxel::BlockRegistry block_registry =
      std::move(block_registry_result.value());
  const arv::u64 block_registry_hash =
      arv::persistence::hash_block_registry(block_registry);
  arv::core::info(
      "dedicated",
      "loaded block registry: blocks={} variants={} hash={}",
      block_registry.block_count(),
      block_registry.variant_count(),
      block_registry_hash);

  auto item_definition_text = arv::io::read_text_file(
      config.paths.config_path() / "items.json",
      "dedicated item definitions");
  if (!item_definition_text) {
    arv::core::error("items", item_definition_text.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  auto item_definitions =
      arv::player::parse_item_definitions_json(item_definition_text.value());
  if (!item_definitions) {
    arv::core::error("items", item_definitions.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  auto item_registry_result =
      arv::player::ItemRegistry::build_from_blocks_and_items(
          block_registry,
          std::move(item_definitions.value()));
  if (!item_registry_result) {
    arv::core::error("items", item_registry_result.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }
  const arv::player::ItemRegistry item_registry =
      std::move(item_registry_result.value());
  const arv::u64 item_registry_hash =
      arv::persistence::hash_item_registry(item_registry);
  arv::core::info(
      "dedicated",
      "loaded item registry: items={} hash={}",
      item_registry.item_count(),
      item_registry_hash);

  auto entity_definition_text = arv::io::read_text_file(
      config.paths.config_path() / "entities.json",
      "dedicated entity definitions");
  if (!entity_definition_text) {
    arv::core::error("entities", entity_definition_text.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  auto entity_definitions =
      arv::entity::parse_entity_definitions_json(entity_definition_text.value());
  if (!entity_definitions) {
    arv::core::error("entities", entity_definitions.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  auto entity_registry_result =
      arv::entity::EntityRegistry::build_with_builtins(std::move(entity_definitions.value()));
  if (!entity_registry_result) {
    arv::core::error("entities", entity_registry_result.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }
  const arv::entity::EntityRegistry entity_registry =
      std::move(entity_registry_result.value());
  arv::core::info(
      "dedicated",
      "loaded entity registry: types={}",
      entity_registry.type_count());

  transition(lifetime, arv::server::DedicatedServerLifetimeState::LoadingWorlds);

  auto worldgen_text = arv::io::read_text_file(
      config.paths.config_path() / "worldgen/overworld.yaml",
      "dedicated overworld generator definition");
  if (!worldgen_text) {
    arv::core::error("worldgen", worldgen_text.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  auto worldgen_config = arv::world::parse_world_generation_yaml(worldgen_text.value());
  if (!worldgen_config) {
    arv::core::error("worldgen", worldgen_config.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }
  const arv::world::WorldGenerationConfig default_generation_config =
      std::move(worldgen_config.value());
  arv::core::info(
      "dedicated",
      "loaded worldgen preset: id={} y=[{}, {}] sea_level={}",
      default_generation_config.id,
      default_generation_config.min_y,
      default_generation_config.max_y,
      default_generation_config.sea_level);

  const std::filesystem::path server_save_root = config.paths.dedicated_server_save_path();
  auto loaded_save_result = arv::persistence::load_server_root(
      server_save_root,
      config.server.world,
      block_registry,
      item_registry);
  if (!loaded_save_result) {
    arv::core::error("save", loaded_save_result.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  arv::persistence::LoadedSaveData loaded_save = std::move(loaded_save_result.value());
  arv::world::WorldGenerationConfig active_generation_config = default_generation_config;
  arv::u64 world_seed = generated_world_seed();
  std::pair<arv::i32, arv::i32> visible_chunk_y =
      visible_worldgen_chunk_y_range(active_generation_config);
  arv::TickIndex initial_tick_index{};
  if (loaded_save.has_world_metadata) {
    active_generation_config = loaded_save.world.generation_config;
    world_seed = loaded_save.world.seed;
    visible_chunk_y = {loaded_save.world.min_chunk_y, loaded_save.world.max_chunk_y};
    initial_tick_index = loaded_save.world.tick_index;
    arv::core::info(
        "dedicated",
        "loaded world '{}' from {}: seed={} tick={} chunks={} players={} entities={}",
        loaded_save.world.name,
        server_save_root.string(),
        world_seed,
        initial_tick_index,
        loaded_save.chunks.size(),
        loaded_save.players.size(),
        loaded_save.entities.size());
  } else {
    arv::persistence::SaveMetadata save_metadata;
    save_metadata.name = loaded_save.has_save_metadata && !loaded_save.save.name.empty()
                             ? loaded_save.save.name
                             : std::string{"Dedicated Server"};
    save_metadata.block_registry_hash = block_registry_hash;
    save_metadata.item_registry_hash = item_registry_hash;

    arv::persistence::WorldMetadata world_metadata;
    world_metadata.name = config.server.world;
    world_metadata.seed = world_seed;
    world_metadata.generation_config = active_generation_config;
    world_metadata.min_chunk_y = visible_chunk_y.first;
    world_metadata.max_chunk_y = visible_chunk_y.second;
    world_metadata.block_registry_hash = block_registry_hash;
    world_metadata.item_registry_hash = item_registry_hash;

    auto create_save_result = arv::persistence::save_server_root(
        server_save_root,
        save_metadata,
        world_metadata,
        std::span<const arv::persistence::SavedChunkSection>{},
        std::span<const arv::persistence::PlayerSaveData>{},
        std::span<const arv::entity::PersistentEntityRecord>{},
        block_registry,
        item_registry);
    if (!create_save_result) {
      arv::core::error("save", create_save_result.error().message);
      transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
      return 1;
    }

    loaded_save.has_save_metadata = true;
    loaded_save.save = save_metadata;
    loaded_save.has_world_metadata = true;
    loaded_save.world = world_metadata;
    arv::core::info(
        "dedicated",
        "created world '{}' at {} with seed={}",
        config.server.world,
        server_save_root.string(),
        world_seed);
  }

  if (loaded_save.world.block_registry_hash != 0U &&
      loaded_save.world.block_registry_hash != block_registry_hash) {
    arv::core::warn(
        "save",
        "world block registry hash differs from current registry: saved={} current={}",
        loaded_save.world.block_registry_hash,
        block_registry_hash);
  }
  if (loaded_save.world.item_registry_hash != 0U &&
      loaded_save.world.item_registry_hash != item_registry_hash) {
    arv::core::warn(
        "save",
        "world item registry hash differs from current registry: saved={} current={}",
        loaded_save.world.item_registry_hash,
        item_registry_hash);
  }

  auto worldgen_palette = arv::world::resolve_world_generation_palette(
      active_generation_config,
      block_registry);
  if (!worldgen_palette) {
    arv::core::error("worldgen", worldgen_palette.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  std::vector<arv::server::ServerPlayerSaveState> saved_players;
  saved_players.reserve(loaded_save.players.size());
  for (const arv::persistence::PlayerSaveData& player : loaded_save.players) {
    saved_players.emplace_back(to_server_player_save(player));
  }

  arv::net::GameNetworkingSocketsEnvironment gns;
  if (!gns.initialized()) {
    arv::core::error("net", gns.error_message());
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  arv::sched::RuntimeThreadBudgetConfig thread_budget_config;
  thread_budget_config.workers.worker_thread_count = config.jobs.worker_threads;
  thread_budget_config.workers.reserved_thread_count = config.jobs.reserved_threads;
  thread_budget_config.workers.min_worker_thread_count = config.jobs.min_worker_threads;
  thread_budget_config.workers.max_worker_thread_count = config.jobs.max_worker_threads;
  const arv::sched::RuntimeThreadBudgetPlan thread_budget =
      arv::sched::plan_runtime_thread_budget(thread_budget_config);

  arv::server::ServerChunkStreamingConfig chunk_streaming;
  chunk_streaming.max_view_distance = config.chunks.load_distance;
  chunk_streaming.simulation_distance = config.chunks.simulation_distance;
  chunk_streaming.unload_margin = config.chunks.unload_margin;
  chunk_streaming.max_chunks_sent_per_player_tick =
      std::max<arv::u32>(config.chunk_pipeline.max_client_chunk_applies_per_frame, 1U);
  chunk_streaming.max_unloads_sent_per_player_tick = 64U;

  arv::world::WorldTickPolicy world_tick_policy;
  world_tick_policy.block_update_interval =
      config.simulation.systems.block_updates.interval;
  world_tick_policy.block_update_budget =
      config.simulation.systems.block_updates.budget;
  world_tick_policy.fluid_interval =
      config.simulation.systems.fluids.interval;
  world_tick_policy.fluid_budget =
      config.simulation.systems.fluids.budget;

  arv::voxel::FluidDefinition water_fluid =
      arv::voxel::make_water_fluid_definition();
  water_fluid.flow_interval_ticks =
      config.simulation.fluids.water.flow_interval_ticks;
  const arv::player::PlayerTuning player_tuning = make_player_tuning(config.physics);

  arv::server::ServerChunkGenerationSettings chunk_generation;
  chunk_generation.enabled = true;
  chunk_generation.seed = world_seed;
  chunk_generation.config = active_generation_config;
  chunk_generation.palette = std::move(worldgen_palette.value());

  arv::server::ServerStartInfo server_start_info;
  server_start_info.mode = arv::server::ServerMode::Dedicated;
  server_start_info.base_tps = config.simulation.base_tps;
  server_start_info.initial_tick_index = initial_tick_index;
  server_start_info.world_name = config.server.world;
  server_start_info.listen_address = {config.network.bind_address, config.network.port};
  server_start_info.block_registry = &block_registry;
  server_start_info.item_registry = &item_registry;
  server_start_info.player_tuning = player_tuning;
  server_start_info.fluid_definition = water_fluid;
  server_start_info.chunk_streaming = chunk_streaming;
  server_start_info.chunk_generation = std::move(chunk_generation);
  server_start_info.max_generation_results_per_tick =
      config.chunk_pipeline.max_generation_results_per_tick;
  server_start_info.max_lighting_results_per_tick =
      config.chunk_pipeline.max_lighting_results_per_tick;
  server_start_info.max_pending_generation_jobs =
      config.chunk_pipeline.max_generation_jobs_per_frame;
  server_start_info.worker_threads = thread_budget.primary_workers;
  server_start_info.world_tick_policy = world_tick_policy;
  server_start_info.entity_registry = &entity_registry;
  server_start_info.saved_players = std::span<const arv::server::ServerPlayerSaveState>{
      saved_players.data(),
      saved_players.size()};

  arv::server::ServerRuntime server(std::make_unique<arv::net::GameNetworkingSocketsTransport>());
  auto start_result = server.start(server_start_info);
  if (!start_result) {
    arv::core::error("server", start_result.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }

  for (const arv::persistence::SavedChunkSection& chunk : loaded_save.chunks) {
    server.voxel_world().restore_saved_chunk(
        chunk.coord,
        chunk.blocks,
        chunk.lights,
        chunk.fluids);
  }
  server.voxel_world().restore_persistent_entities(
      loaded_save.entities,
      &entity_registry);
  arv::core::info(
      "dedicated",
      "world '{}' ready: loaded_chunks={} entities={} restored_player_profiles={} chunk_y=[{}, {}]",
      config.server.world,
      server.voxel_world().loaded_chunk_count(),
      server.voxel_world().entity_count(),
      saved_players.size(),
      visible_chunk_y.first,
      visible_chunk_y.second);
  arv::core::info(
      "dedicated",
      "listening on {}:{} at {} TPS",
      config.network.bind_address,
      config.network.port,
      config.simulation.base_tps);

  transition(lifetime, arv::server::DedicatedServerLifetimeState::Running);
  const auto sleep_duration = std::chrono::duration<arv::Seconds>(
      arv::tick_duration_seconds(config.simulation.base_tps));

  arv::u64 ticks_run = 0;
  while (!g_shutdown_requested.load() &&
         (launch_options.max_ticks == 0 || ticks_run < launch_options.max_ticks)) {
    server.tick();
    ++ticks_run;
    std::this_thread::sleep_for(sleep_duration);
  }

  transition(lifetime, arv::server::DedicatedServerLifetimeState::Stopping);
  server.shutdown();
  transition(lifetime, arv::server::DedicatedServerLifetimeState::Saving);
  const std::vector<arv::persistence::SavedChunkSection> saved_chunks =
      capture_saved_chunks(server.voxel_world());
  const std::vector<arv::persistence::PlayerSaveData> player_saves =
      capture_player_saves(server, config.server.world);
  const std::vector<arv::entity::PersistentEntityRecord> saved_entities =
      server.voxel_world().persistent_entity_records();
  const arv::persistence::SaveMetadata save_metadata{
      arv::persistence::CurrentSaveFormatVersion,
      std::string{arv::persistence::CurrentGameVersion},
      block_registry_hash,
      item_registry_hash,
      loaded_save.save.name.empty() ? std::string{"Dedicated Server"} : loaded_save.save.name};
  const arv::persistence::WorldMetadata world_metadata{
      config.server.world,
      world_seed,
      server.voxel_world().tick_index(),
      active_generation_config,
      visible_chunk_y.first,
      visible_chunk_y.second,
      block_registry_hash,
      item_registry_hash};
  auto save_result = arv::persistence::save_server_root(
      server_save_root,
      save_metadata,
      world_metadata,
      saved_chunks,
      player_saves,
      saved_entities,
      block_registry,
      item_registry);
  if (!save_result) {
    arv::core::error("save", save_result.error().message);
    transition(lifetime, arv::server::DedicatedServerLifetimeState::Failed);
    return 1;
  }
  arv::core::info(
      "dedicated",
      "saved world '{}' to {}: tick={} chunks={} players={} entities={}",
      config.server.world,
      server_save_root.string(),
      world_metadata.tick_index,
      saved_chunks.size(),
      player_saves.size(),
      saved_entities.size());
  transition(lifetime, arv::server::DedicatedServerLifetimeState::Stopped);
  return 0;
}
