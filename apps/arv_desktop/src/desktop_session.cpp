#include "desktop_session.hpp"

#include "desktop_runtime_config.hpp"
#include "desktop_save_helpers.hpp"
#include "save_state.hpp"

#include "arv/base/base_ids.hpp"
#include "arv/client/client_chunk_stream_packets.hpp"
#include "arv/client/item_ui_model.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/client/client_world_time.hpp"
#include "arv/core/log.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/persistence/save.hpp"
#include "arv/render/render_commands.hpp"

#include <atomic>
#include <array>
#include <exception>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace arv::desktop {

struct DesktopSessionManager::PendingShutdown {
  std::thread thread;
  std::atomic_bool finished{};
  std::atomic_bool succeeded{};
};

struct DesktopSessionManager::PendingStart {
  std::thread thread;
  std::atomic_bool finished{};
  std::atomic_bool succeeded{};
  std::unique_ptr<DesktopGameSession> session{};
};

DesktopGameSession::DesktopGameSession(TickRate base_tps) : ticks(base_tps) {
}

DesktopSessionManager::DesktopSessionManager(DesktopSessionDependencies dependencies) : dependencies_(dependencies) {
}

DesktopSessionManager::~DesktopSessionManager() {
  (void)finish_pending_start();
  (void)finish_pending_shutdown();
}

DesktopGameSession* DesktopSessionManager::active_session() noexcept {
  return game_session_.get();
}

const DesktopGameSession* DesktopSessionManager::active_session() const noexcept {
  return game_session_.get();
}

bool DesktopSessionManager::shutdown_pending() const noexcept {
  return pending_shutdown_ != nullptr;
}

bool DesktopSessionManager::shutdown_failed() const noexcept {
  return shutdown_failed_;
}

bool DesktopSessionManager::start_pending() const noexcept {
  return pending_start_ != nullptr;
}

void DesktopSessionManager::poll_shutdown() {
  if (pending_shutdown_ == nullptr ||
      !pending_shutdown_->finished.load(std::memory_order_acquire)) {
    return;
  }
  (void)finish_pending_shutdown();
}

std::optional<bool> DesktopSessionManager::poll_start() {
  if (pending_start_ == nullptr) {
    return true;
  }
  if (!pending_start_->finished.load(std::memory_order_acquire)) {
    return std::nullopt;
  }
  return finish_pending_start();
}

void DesktopSessionManager::remove_session_chunk_meshes(DesktopGameSession& session) const {
  for (const auto& [coord, item] : session.chunk_view.draw_items) {
    (void)coord;
    dependencies_.renderer.remove_chunk_mesh(item.key);
  }
}

bool DesktopSessionManager::shutdown_session(std::unique_ptr<DesktopGameSession> session) {
  if (session == nullptr) {
    return true;
  }

  if (session->integrated_server == nullptr) {
    cancel_chunk_view_mesh_jobs(dependencies_.client_tasks, session->chunk_view);
    remove_session_chunk_meshes(*session);
    dependencies_.world_jobs.join();
    return true;
  }

  if (const server::ServerSession* server_session =
          session->integrated_server->sessions().first();
      server_session != nullptr) {
    (void)session->integrated_server->close_player_inventory_menu(server_session->player_id);
  }

  const std::vector<persistence::SavedChunkSection> saved_chunks =
      capture_saved_chunks(session->integrated_server->voxel_world());
  const std::vector<persistence::PlayerSaveData> player_saves =
      capture_player_saves(*session->integrated_server, session->world_save_name);
  const std::vector<entity::PersistentEntityRecord> entity_saves =
      capture_saved_entities(session->integrated_server->voxel_world());
  const persistence::SaveMetadata save_metadata{
      persistence::CurrentSaveFormatVersion,
      std::string{persistence::CurrentGameVersion},
      0U,
      0U,
      session->server_save_display_name};
  persistence::WorldMetadata world_metadata{
      session->world_save_name,
      session->world_seed,
      session->integrated_server->voxel_world().tick_index(),
      session->generation_config,
      session->min_chunk_y,
      session->max_chunk_y,
      0U,
      0U,
      session->integrated_server->voxel_world().clock()};
  world_metadata.spawn_point = session->integrated_server->voxel_world().spawn_point();
  auto save_result = persistence::save_server_root(
      session->server_save_root,
      save_metadata,
      world_metadata,
      saved_chunks,
      player_saves,
      entity_saves,
      dependencies_.block_registry,
      dependencies_.item_registry);
  if (!save_result) {
    core::error("save", save_result.error().message);
    return false;
  }

  cancel_chunk_view_mesh_jobs(dependencies_.client_tasks, session->chunk_view);
  remove_session_chunk_meshes(*session);
  dependencies_.world_jobs.join();
  session->integrated_server->shutdown();
  return true;
}

bool DesktopSessionManager::finish_pending_shutdown() {
  if (pending_shutdown_ == nullptr) {
    return !shutdown_failed_;
  }

  if (pending_shutdown_->thread.joinable()) {
    pending_shutdown_->thread.join();
  }
  const bool succeeded = pending_shutdown_->succeeded.load(std::memory_order_acquire);
  pending_shutdown_.reset();
  if (!succeeded) {
    shutdown_failed_ = true;
  }
  return !shutdown_failed_;
}

void DesktopSessionManager::adopt_started_session(
    std::unique_ptr<DesktopGameSession> session) {
  if (session == nullptr) {
    return;
  }
  session->client_world.bind_owner_thread();
  if (session->integrated_server != nullptr) {
    session->integrated_server->voxel_world().bind_owner_thread();
  }
  game_session_ = std::move(session);
}

bool DesktopSessionManager::finish_pending_start() {
  if (pending_start_ == nullptr) {
    return true;
  }

  if (pending_start_->thread.joinable()) {
    pending_start_->thread.join();
  }
  const bool succeeded = pending_start_->succeeded.load(std::memory_order_acquire);
  std::unique_ptr<DesktopGameSession> session = std::move(pending_start_->session);
  pending_start_.reset();
  if (succeeded && session != nullptr) {
    adopt_started_session(std::move(session));
    return true;
  }
  return false;
}

bool DesktopSessionManager::begin_save_and_shutdown() {
  if (pending_shutdown_ != nullptr && !finish_pending_shutdown()) {
    return false;
  }
  if (!game_session_) {
    return !shutdown_failed_;
  }

  auto pending = std::make_unique<PendingShutdown>();
  PendingShutdown* state = pending.get();
  std::unique_ptr<DesktopGameSession> session = std::move(game_session_);
  pending->thread = std::thread([this, state, session = std::move(session)]() mutable {
    bool succeeded = false;
    try {
      succeeded = shutdown_session(std::move(session));
    } catch (const std::exception& ex) {
      core::error("desktop", "session shutdown failed: {}", ex.what());
    } catch (...) {
      core::error("desktop", "session shutdown failed with an unknown exception");
    }
    state->succeeded.store(succeeded, std::memory_order_release);
    state->finished.store(true, std::memory_order_release);
  });
  pending_shutdown_ = std::move(pending);
  return true;
}

bool DesktopSessionManager::save_and_shutdown() {
  if (!finish_pending_shutdown()) {
    return false;
  }
  if (!game_session_) {
    return !shutdown_failed_;
  }

  const bool succeeded = shutdown_session(std::move(game_session_));
  if (!succeeded) {
    shutdown_failed_ = true;
  }
  return succeeded && !shutdown_failed_;
}

std::unique_ptr<DesktopGameSession> DesktopSessionManager::build_singleplayer_session(
    std::string server_save_name,
    std::string world_name,
    std::optional<u64> create_seed,
    bool create_new,
    std::string save_display_name,
    const client::LocalPlayerIdentity& local_identity) {
  if (server_save_name.empty()) {
    core::error("save", "cannot start a singleplayer world without a server save name");
    return nullptr;
  }
  if (world_name.empty()) {
    world_name = "default";
  }

  const std::filesystem::path server_save_root =
      dependencies_.config.paths.server_save_path(server_save_name);
  auto loaded_save_result = persistence::load_server_root(
      server_save_root,
      world_name,
      dependencies_.block_registry,
      dependencies_.item_registry);
  if (!loaded_save_result) {
    core::error("save", loaded_save_result.error().message);
    return nullptr;
  }
  const persistence::LoadedSaveData loaded_save =
      std::move(loaded_save_result.value());
  if (!create_new && !loaded_save.has_world_metadata) {
    core::error(
        "save",
        "world '{}' was not found in server save '{}'",
        world_name,
        server_save_name);
    return nullptr;
  }

  auto session = std::make_unique<DesktopGameSession>(dependencies_.config.simulation.base_tps);
  session->server_save_name = std::move(server_save_name);
  session->server_save_display_name = save_display_name.empty()
                                          ? loaded_save.save.name
                                          : std::move(save_display_name);
  if (session->server_save_display_name.empty()) {
    session->server_save_display_name = session->server_save_name;
  }
  session->server_save_root = server_save_root;
  session->world_save_name = std::move(world_name);
  session->world_seed = create_seed.value_or(generated_world_seed());
  session->generation_config = dependencies_.default_generation_config;
  if (loaded_save.has_world_metadata) {
    session->generation_config = world::refresh_generation_config_for_current_dimension(
        loaded_save.world.generation_config,
        dependencies_.default_generation_config);
    session->world_seed = loaded_save.world.seed;
    session->world_save_name = loaded_save.world.name;
  }
  const world::WorldHeightSettings session_world_height =
      world::world_height_settings_from_generation_config(session->generation_config);
  session->min_chunk_y = session_world_height.min_chunk_y;
  session->max_chunk_y = session_world_height.max_chunk_y;
  const arv::Location session_world_spawn =
      loaded_save.has_world_metadata && loaded_save.world.spawn_point.has_value()
          ? loaded_save.world.spawn_point.value()
          : world::generate_world_spawn_location(
                session->generation_config,
                session->world_seed);
  session->client_world.set_generation_context(
      session->world_seed,
      session->generation_config);

  auto worldgen_palette = world::resolve_world_generation_palette(
      session->generation_config,
      dependencies_.block_registry);
  if (!worldgen_palette) {
    core::error("worldgen", worldgen_palette.error().message);
    return nullptr;
  }

  server::ServerChunkGenerationSettings chunk_generation;
  chunk_generation.enabled = true;
  chunk_generation.seed = session->world_seed;
  chunk_generation.config = session->generation_config;
  chunk_generation.palette = std::move(worldgen_palette.value());

  server::ServerStartInfo server_start_info;
  server_start_info.mode = server::ServerMode::Integrated;
  server_start_info.base_tps = dependencies_.config.simulation.base_tps;
  server_start_info.listen_address = {"in-process", 0};
  server_start_info.block_registry = &dependencies_.block_registry;
  server_start_info.item_registry = &dependencies_.item_registry;
  server_start_info.container_types = &dependencies_.container_types;
  server_start_info.container_layouts = &dependencies_.container_layouts;
  server_start_info.game_modes = &dependencies_.game_modes;
  server_start_info.starter_inventory =
      std::span<const server::StarterInventoryItem>{dependencies_.starter_inventory};
  server_start_info.fluid_containers = dependencies_.fluid_containers;
  server_start_info.register_commands = dependencies_.register_commands;
  server_start_info.plugins = dependencies_.plugins;
  server_start_info.player_tuning = dependencies_.player_tuning;
  server_start_info.fluid_definition = dependencies_.water_fluid;
  server_start_info.chunk_streaming = make_chunk_streaming_config(dependencies_.config);
  server_start_info.chunk_generation = std::move(chunk_generation);
  server_start_info.world_height = session_world_height;
  server_start_info.world_spawn = session_world_spawn;
  server_start_info.max_generation_results_per_tick =
      dependencies_.config.chunk_pipeline.max_generation_results_per_tick;
  server_start_info.max_lighting_results_per_tick =
      dependencies_.config.chunk_pipeline.max_lighting_results_per_tick;
  server_start_info.max_lighting_sections_per_job =
      dependencies_.config.chunk_pipeline.max_lighting_sections_per_job;
  server_start_info.max_pending_generation_jobs =
      dependencies_.config.chunk_pipeline.max_generation_jobs_per_frame;
  server_start_info.worker_threads = dependencies_.thread_budget.primary_workers;
  server_start_info.job_system =
      make_server_job_system_config(dependencies_.config.jobs);
  server_start_info.world_tick_policy = dependencies_.world_tick_policy;
  server_start_info.natural_spawning =
      server::make_natural_spawning_config(dependencies_.config.natural_spawning);
  server_start_info.entity_registry = &dependencies_.entity_registry;
  server_start_info.vehicle_registry = &dependencies_.vehicle_registry;
  server_start_info.entity_goals = &dependencies_.entity_goals;
  server_start_info.server_save_root = server_save_root;
  server_start_info.skins = dependencies_.config.skins;
  server_start_info.movement = dependencies_.config.network.movement;
  server_start_info.keep_alive = dependencies_.config.network.keep_alive;
  server_start_info.keep_alive_enabled = false;
  server_start_info.language = &dependencies_.language;
  server_start_info.command_permission_denied_message =
      dependencies_.config.commands.permission_denied_message;
  server_start_info.skins.enabled = true;
  server_start_info.skins.allow_uploads = true;
  for (io::SkinResolutionConfig& resolution : server_start_info.skins.resolutions) {
    resolution.enabled = true;
    resolution.permission.clear();
  }
  const std::vector<server::ServerPlayerSaveState> saved_players =
      to_server_player_saves(loaded_save.players);
  server_start_info.saved_players = saved_players;
  if (loaded_save.has_world_metadata) {
    server_start_info.initial_tick_index = loaded_save.world.tick_index;
  }
  server_start_info.block_breaking.base_seconds_per_hardness =
      dependencies_.config.gameplay.block_breaking.base_seconds_per_hardness;
  server_start_info.block_breaking.creative_inventory_multiplier =
      dependencies_.config.gameplay.block_breaking.creative_inventory_multiplier;
  server_start_info.block_breaking.hand_speed_multiplier =
      dependencies_.config.gameplay.block_breaking.hand_speed_multiplier;
  server_start_info.block_breaking.wrong_tool_speed_multiplier =
      dependencies_.config.gameplay.block_breaking.wrong_tool_speed_multiplier;
  server_start_info.block_breaking.behavior_id =
      std::string{base::ids::MineableTagsBlockBreakBehaviorID};
  server_start_info.block_break_behaviors = &dependencies_.block_break_behaviors;
  server_start_info.dropped_items.pickup_radius =
      dependencies_.config.gameplay.dropped_items.pickup_radius;
  server_start_info.dropped_items.pickup_delay_seconds =
      dependencies_.config.gameplay.dropped_items.pickup_delay_seconds;
  server_start_info.combat.default_attack_damage =
      dependencies_.config.gameplay.combat.default_attack_damage;
  server_start_info.combat.default_attack_range =
      dependencies_.config.gameplay.combat.default_attack_range;
  server_start_info.combat.default_attack_cooldown_seconds =
      dependencies_.config.gameplay.combat.default_attack_cooldown_seconds;
  server_start_info.combat.critical_chance =
      dependencies_.config.gameplay.combat.critical_chance;
  server_start_info.combat.critical_multiplier =
      dependencies_.config.gameplay.combat.critical_multiplier;
  server_start_info.combat.attack_validation_grace_blocks =
      dependencies_.config.gameplay.combat.attack_validation_grace_blocks;
  server_start_info.combat.max_attack_validation_range =
      dependencies_.config.gameplay.combat.max_attack_validation_range;
  server_start_info.combat.existing_horizontal_retention =
      dependencies_.config.gameplay.combat.existing_horizontal_retention;
  server_start_info.combat.base_horizontal_knockback =
      dependencies_.config.gameplay.combat.base_horizontal_knockback;
  server_start_info.combat.sprint_horizontal_bonus =
      dependencies_.config.gameplay.combat.sprint_horizontal_bonus;
  server_start_info.combat.base_vertical_knockback =
      dependencies_.config.gameplay.combat.base_vertical_knockback;
  server_start_info.combat.airborne_vertical_knockback =
      dependencies_.config.gameplay.combat.airborne_vertical_knockback;
  server_start_info.combat.vertical_knockback_cap =
      dependencies_.config.gameplay.combat.vertical_knockback_cap;
  server_start_info.combat.knockback_control_suppression_ticks =
      dependencies_.config.gameplay.combat.knockback_control_suppression_ticks;
  server_start_info.combat.use_current_block_obstruction_check =
      dependencies_.config.gameplay.combat.use_current_block_obstruction_check;

  auto transport_pair = net::make_in_process_transport_pair();
  auto integrated_server =
      std::make_unique<server::ServerRuntime>(std::move(transport_pair.server));
  auto server_start = integrated_server->start(server_start_info);
  if (!server_start) {
    core::error("server", server_start.error().message);
    return nullptr;
  }

  if (loaded_save.has_world_metadata) {
    integrated_server->voxel_world().set_clock(
        loaded_save.world.clock,
        dependencies_.config.simulation.base_tps);
  }
  for (const persistence::SavedChunkSection& chunk : loaded_save.chunks) {
    integrated_server->voxel_world().restore_saved_chunk(
        chunk.coord,
        chunk.blocks,
        chunk.lights,
        chunk.fluids,
        chunk.lighting_valid,
        chunk.block_containers);
  }
  integrated_server->voxel_world().restore_persistent_entities(
      loaded_save.entities,
      &dependencies_.entity_registry,
      &dependencies_.vehicle_registry);

  auto client = std::make_unique<client::ClientRuntime>(std::move(transport_pair.client));
  client->configure_networking(
      {dependencies_.config.multiplayer.keep_alive.server_timeout_seconds,
       dependencies_.config.multiplayer.connection_timeout_seconds,
       false});
  client->configure_chat({dependencies_.config.ui.chat.max_history_messages});
  client->configure_language(&dependencies_.language);
  client->configure_particles(&dependencies_.particle_registry);
  client->configure_vehicles(&dependencies_.vehicle_registry);
  client->set_local_identity(local_identity);
  client->set_player_tuning(dependencies_.player_tuning);
  client->set_chunk_view_distance(dependencies_.config.chunks.stream_distance);
  auto connect_result = client->connect_integrated();
  if (!connect_result) {
    core::error("client", connect_result.error().message);
    integrated_server->shutdown();
    return nullptr;
  }

  session->integrated_server = std::move(integrated_server);
  session->client = std::move(client);
  session->integrated_server->tick();
  session->client->fixed_tick(session->integrated_server->tick_index());
  collect_client_chunk_stream(*session);
  apply_pending_client_chunk_stream(
      *session,
      dependencies_.config.chunk_pipeline.max_client_chunk_applies_per_frame,
      dependencies_.config.chunk_pipeline.max_client_chunk_unloads_per_frame);
  apply_loaded_player_saves(
      *session->integrated_server,
      session->world_save_name,
      loaded_save.players);
  if (!loaded_save.players.empty()) {
    session->integrated_server->tick();
    session->client->fixed_tick(session->integrated_server->tick_index());
    collect_client_chunk_stream(*session);
    apply_pending_client_chunk_stream(
        *session,
        dependencies_.config.chunk_pipeline.max_client_chunk_applies_per_frame,
        dependencies_.config.chunk_pipeline.max_client_chunk_unloads_per_frame);
  }

  if (!update_chunk_view(
          session->client_world,
          dependencies_.renderer,
          dependencies_.client_tasks,
          session->chunk_view,
          dependencies_.config.chunks,
          dependencies_.config.chunk_pipeline,
          camera_chunk_section_coord(session->client->camera()),
          session->min_chunk_y,
          session->max_chunk_y,
          dependencies_.block_registry,
          dependencies_.block_atlas.render_data,
          &dependencies_.fluid_render_data)) {
    session->integrated_server->shutdown();
    return nullptr;
  }
  refresh_inventory_slots(*session);
  core::info(
      "desktop",
      "entered singleplayer world '{}' from server save '{}'",
      session->world_save_name,
      session->server_save_name);
  return session;
}

bool DesktopSessionManager::begin_start_singleplayer(
    std::string server_save_name,
    std::string world_name,
    std::optional<u64> create_seed,
    bool create_new,
    std::string save_display_name,
    client::LocalPlayerIdentity local_identity) {
  if (pending_start_ != nullptr) {
    return false;
  }
  if (!save_and_shutdown()) {
    return false;
  }

  auto pending = std::make_unique<PendingStart>();
  PendingStart* state = pending.get();
  pending->thread = std::thread(
      [this,
       state,
       server_save_name = std::move(server_save_name),
       world_name = std::move(world_name),
       create_seed,
       create_new,
       save_display_name = std::move(save_display_name),
       local_identity = std::move(local_identity)]() mutable {
        std::unique_ptr<DesktopGameSession> session;
        try {
          session = build_singleplayer_session(
              std::move(server_save_name),
              std::move(world_name),
              create_seed,
              create_new,
              std::move(save_display_name),
              local_identity);
        } catch (const std::exception& ex) {
          core::error("desktop", "singleplayer world start failed: {}", ex.what());
        } catch (...) {
          core::error("desktop", "singleplayer world start failed with an unknown exception");
        }
        state->succeeded.store(session != nullptr, std::memory_order_release);
        state->session = std::move(session);
        state->finished.store(true, std::memory_order_release);
      });
  pending_start_ = std::move(pending);
  return true;
}

bool DesktopSessionManager::start_singleplayer(
    std::string server_save_name,
    std::string world_name,
    std::optional<u64> create_seed,
    bool create_new,
    std::string save_display_name,
    const client::LocalPlayerIdentity& local_identity) {
  if (!save_and_shutdown()) {
    return false;
  }
  std::unique_ptr<DesktopGameSession> session = build_singleplayer_session(
      std::move(server_save_name),
      std::move(world_name),
      create_seed,
      create_new,
      std::move(save_display_name),
      local_identity);
  if (session == nullptr) {
    return false;
  }
  adopt_started_session(std::move(session));
  return true;
}

std::unique_ptr<DesktopGameSession> DesktopSessionManager::build_remote_session(
    const ui::JoinServerCommand& command,
    const client::LocalPlayerIdentity& local_identity) {
  const std::optional<net::NetworkAddress> address =
      parse_server_address(command.address);
  if (!address.has_value()) {
    core::error("client", "invalid server address '{}'", command.address);
    return nullptr;
  }

  if (gns_environment_ == nullptr) {
    gns_environment_ = std::make_unique<net::GameNetworkingSocketsEnvironment>();
  }
  if (!gns_environment_->initialized()) {
    core::error("net", gns_environment_->error_message());
    gns_environment_.reset();
    return nullptr;
  }

  auto session = std::make_unique<DesktopGameSession>(dependencies_.config.simulation.base_tps);
  session->server_save_display_name = command.name;
  session->remote_server = command;
  session->generation_config = dependencies_.default_generation_config;
  const world::WorldHeightSettings session_world_height =
      world::world_height_settings_from_generation_config(session->generation_config);
  session->min_chunk_y = session_world_height.min_chunk_y;
  session->max_chunk_y = session_world_height.max_chunk_y;
  session->client_world.configure_height(session_world_height);

  auto client = std::make_unique<client::ClientRuntime>(
      std::make_unique<net::GameNetworkingSocketsTransport>());
  client->configure_networking(
      {dependencies_.config.multiplayer.keep_alive.server_timeout_seconds,
       dependencies_.config.multiplayer.connection_timeout_seconds});
  client->configure_chat({dependencies_.config.ui.chat.max_history_messages});
  client->configure_language(&dependencies_.language);
  client->configure_particles(&dependencies_.particle_registry);
  client->configure_vehicles(&dependencies_.vehicle_registry);
  client->set_local_identity(local_identity);
  client->set_player_tuning(dependencies_.player_tuning);
  client->set_chunk_view_distance(dependencies_.config.chunks.stream_distance);
  auto connect_result = client->connect(address.value());
  if (!connect_result) {
    core::error("client", connect_result.error().message);
    return nullptr;
  }

  session->client = std::move(client);
  core::info(
      "desktop",
      "connecting to server '{}' at {}:{}",
      command.name,
      address->host,
      address->port);
  return session;
}

bool DesktopSessionManager::begin_start_remote(
    ui::JoinServerCommand command,
    client::LocalPlayerIdentity local_identity) {
  if (pending_start_ != nullptr) {
    return false;
  }
  if (!save_and_shutdown()) {
    return false;
  }

  auto pending = std::make_unique<PendingStart>();
  PendingStart* state = pending.get();
  pending->thread = std::thread(
      [this,
       state,
       command = std::move(command),
       local_identity = std::move(local_identity)]() mutable {
        std::unique_ptr<DesktopGameSession> session;
        try {
          session = build_remote_session(command, local_identity);
        } catch (const std::exception& ex) {
          core::error("desktop", "remote server start failed: {}", ex.what());
        } catch (...) {
          core::error("desktop", "remote server start failed with an unknown exception");
        }
        state->succeeded.store(session != nullptr, std::memory_order_release);
        state->session = std::move(session);
        state->finished.store(true, std::memory_order_release);
      });
  pending_start_ = std::move(pending);
  return true;
}

bool DesktopSessionManager::start_remote(
    const ui::JoinServerCommand& command,
    const client::LocalPlayerIdentity& local_identity) {
  if (!save_and_shutdown()) {
    return false;
  }
  std::unique_ptr<DesktopGameSession> session =
      build_remote_session(command, local_identity);
  if (session == nullptr) {
    return false;
  }
  adopt_started_session(std::move(session));
  return true;
}

void DesktopSessionManager::delete_singleplayer_save(
    const ui::DeleteSinglePlayerSaveCommand& command) {
  if (command.server_save_name.empty()) {
    return;
  }
  if (game_session_ != nullptr && game_session_->server_save_name == command.server_save_name &&
      !save_and_shutdown()) {
    return;
  }

  const std::filesystem::path save_root =
      dependencies_.config.paths.server_save_path(command.server_save_name);
  std::error_code error;
  const std::uintmax_t removed = std::filesystem::remove_all(save_root, error);
  if (error) {
    core::warn(
        "save",
        "failed to delete save '{}': {}",
        command.server_save_name,
        error.message());
  } else if (removed > 0) {
    core::info("save", "deleted server save '{}'", command.server_save_name);
  }
}

void DesktopSessionManager::refresh_inventory_slots(DesktopGameSession& session) const {
  if (session.client == nullptr) {
    session.inventory_slots.clear();
    session.container_slots.clear();
    session.carried_inventory_stack.reset();
    return;
  }

  client::InventoryUiProjection projection = client::project_inventory_ui(
      {
          .has_authoritative_inventory = session.client->has_authoritative_inventory(),
          .container_open = session.client->container_open(),
          .inventory = session.client->inventory(),
          .container_inventory = session.client->active_container_inventory(),
          .carried_stack = session.client->inventory_cursor_stack(),
          .item_registry = dependencies_.item_registry,
          .item_atlas = dependencies_.item_atlas,
          .block_atlas = dependencies_.block_atlas,
      });
  session.inventory_slots = std::move(projection.inventory_slots);
  session.container_slots = std::move(projection.container_slots);
  session.carried_inventory_stack = std::move(projection.carried_stack);
}

void DesktopSessionManager::collect_client_chunk_stream(DesktopGameSession& session) const {
  if (std::optional<protocol::ClientboundWorldTime> time =
          session.client->take_world_time_update();
      time.has_value() && time->world_id == session.client_world.id()) {
    session.client_world.set_clock(
        client::world_clock_from_time_packet(time.value()),
        session.client->server_tick_rate());
  }

  client::ClientChunkStreamPackets packets =
      session.client->drain_chunk_stream_packets();
  struct EnqueuePacket {
    DesktopGameSession& session;

    void operator()(const protocol::ClientboundChunkSectionUnload& unload) const {
      session.pending_client_chunk_unloads.emplace_back(unload);
    }

    void operator()(protocol::ClientboundChunkSectionData&& data) const {
      session.pending_client_chunk_data.emplace_back(std::move(data));
    }

    void operator()(protocol::ClientboundBlockUpdateBatch&& batch) const {
      session.pending_client_block_updates.emplace_back(std::move(batch));
    }
  };
  client::consume_client_chunk_stream_packets(
      packets,
      EnqueuePacket{.session = session});
}

void DesktopSessionManager::apply_pending_client_chunk_stream(
    DesktopGameSession& session,
    u32 chunk_budget,
    u32 unload_budget) const {
  bool changed = false;
  u32 unloads_applied = 0;
  while (unloads_applied < unload_budget && !session.pending_client_chunk_unloads.empty()) {
    const protocol::ClientboundChunkSectionUnload unload = session.pending_client_chunk_unloads.front();
    session.pending_client_chunk_unloads.pop_front();
    ++unloads_applied;
    if (unload.world_id != session.client_world.id()) {
      continue;
    }
    if (session.client_world.unload_chunk(unload.coord)) {
      dependencies_.renderer.remove_chunk_mesh({unload.world_id, unload.coord});
      remove_chunk_view_chunk(session.chunk_view, unload.coord);
      changed = true;
    }
  }

  u32 chunks_applied = 0;
  while (chunks_applied < chunk_budget && !session.pending_client_chunk_data.empty()) {
    protocol::ClientboundChunkSectionData data = std::move(session.pending_client_chunk_data.front());
    session.pending_client_chunk_data.pop_front();
    ++chunks_applied;
    if (data.world_id != session.client_world.id()) {
      continue;
    }

    std::optional<std::array<voxel::LightWord, voxel::ChunkVolume>> lights;
    if (data.has_lighting) {
      lights = std::move(data.lights);
    }
    session.client_world.apply_chunk_snapshot(
        data.coord,
        std::move(data.blocks),
        std::move(lights),
        std::move(data.fluids),
        data.has_lighting && data.lighting_valid);
    changed = true;
  }

  while (!session.pending_client_block_updates.empty()) {
    protocol::ClientboundBlockUpdateBatch batch =
        std::move(session.pending_client_block_updates.front());
    session.pending_client_block_updates.pop_front();
    if (batch.world_id != session.client_world.id()) {
      continue;
    }
    for (const protocol::BlockUpdate& update : batch.updates) {
      if (session.client_world.find_chunk(block_to_chunk(update.pos)) == nullptr) {
        continue;
      }
      if (session.client_world.set_block(update.pos, update.new_state)) {
        changed = true;
      }
      if (update.has_fluid &&
          session.client_world.set_fluid(
              update.pos,
              update.new_fluid,
              dependencies_.water_fluid)) {
        changed = true;
      }
    }
    (void)session.client_world.drain_replicated_block_updates();
  }

  if (changed) {
    request_chunk_view_mesh_rescan(session.chunk_view);
  }
}

} // namespace arv::desktop
