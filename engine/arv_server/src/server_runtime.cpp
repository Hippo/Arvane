#include "server_runtime_internal.hpp"

#include "arv/command/server_command_services.hpp"
#include "arv/crypto/random.hpp"
#include "arv/lang/language.hpp"
#include "arv/core/log.hpp"
#include "arv/server/server_commands.hpp"
#include "arv/server/skin_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>

namespace arv::server {
namespace {

[[nodiscard]] std::string tr(
    const lang::LanguageCatalog* language,
    std::string_view key,
    std::string_view fallback) {
  return language == nullptr ? std::string{fallback} : language->text(key, fallback);
}

[[nodiscard]] std::string tr(
    const lang::LanguageCatalog* language,
    std::string_view key,
    std::initializer_list<lang::FormatArg> args,
    std::string_view fallback) {
  return language == nullptr ? lang::LanguageCatalog{}.format(key, args, fallback)
                             : language->format(key, args, fallback);
}

[[nodiscard]] command::OnlinePlayerCommandTarget command_target(
    const ServerSession& session,
    const player::PlayerState& player_state) {
  return {
      session.session_id,
      session.player_id,
      session.world_id,
      session.display_name,
      player_state.position,
  };
}

[[nodiscard]] std::optional<command::OnlinePlayerCommandTarget> online_player_by_id(
    const SessionRegistry& sessions,
    const world::WorldRuntime& world,
    PlayerID player_id) {
  for (const SessionID session_id : sessions.session_ids()) {
    const ServerSession* session = sessions.find(session_id);
    if (session == nullptr || session->player_id != player_id) {
      continue;
    }

    const player::PlayerState* player_state = world.find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    return command_target(*session, *player_state);
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<command::OnlinePlayerCommandTarget> online_player_by_name(
    const SessionRegistry& sessions,
    const world::WorldRuntime& world,
    std::string_view display_name) {
  std::optional<command::OnlinePlayerCommandTarget> match;
  for (const SessionID session_id : sessions.session_ids()) {
    const ServerSession* session = sessions.find(session_id);
    if (session == nullptr || session->display_name != display_name) {
      continue;
    }

    const player::PlayerState* player_state = world.find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    if (match.has_value()) {
      return std::nullopt;
    }

    match = command_target(*session, *player_state);
  }

  return match;
}

[[nodiscard]] bool teleport_player(
    world::WorldRuntime& world,
    PlayerID player_id,
    Vec3f position) {
  player::PlayerState* player_state = world.find_player(player_id);
  if (player_state == nullptr) {
    return false;
  }

  player_state->position = position;
  player_state->velocity = {};
  player_state->grounded = false;

  if (const std::optional<EntityID> entity_id = world.entity_for_player(player_id);
      entity_id.has_value()) {
    (void)world.set_entity_position(entity_id.value(), position);
  }

  return true;
}

} // namespace

ServerRuntime::ServerRuntime(std::unique_ptr<net::ITransport> transport) : impl_(std::make_unique<detail::ServerRuntimeImpl>(std::move(transport))) {
}

ServerRuntime::~ServerRuntime() = default;

ServerRuntime::ServerRuntime(ServerRuntime&&) noexcept = default;

ServerRuntime& ServerRuntime::operator=(ServerRuntime&&) noexcept = default;

net::NetworkResult ServerRuntime::start(const ServerStartInfo& start_info) {
  return impl_->start(start_info);
}

void ServerRuntime::tick() {
  impl_->tick();
}

void ServerRuntime::shutdown() {
  impl_->shutdown();
}

void ServerRuntime::configure_chunk_streaming(ServerChunkStreamingConfig config) noexcept {
  impl_->configure_chunk_streaming(config);
}

ServerRuntimeState ServerRuntime::state() const noexcept {
  return impl_->state();
}

TickIndex ServerRuntime::tick_index() const noexcept {
  return impl_->tick_index();
}

TickRate ServerRuntime::base_tps() const noexcept {
  return impl_->base_tps();
}

ServerMode ServerRuntime::mode() const noexcept {
  return impl_->mode();
}

const SessionRegistry& ServerRuntime::sessions() const noexcept {
  return impl_->sessions();
}

const WorldRegistry& ServerRuntime::worlds() const noexcept {
  return impl_->worlds();
}

const command::CommandRegistry& ServerRuntime::command_registry() const noexcept {
  return impl_->command_registry();
}

std::vector<DroppedItemStack> ServerRuntime::dropped_item_stacks() const {
  return impl_->dropped_item_stacks();
}

const std::vector<ServerPlayerSaveState>& ServerRuntime::saved_player_states() const noexcept {
  return impl_->saved_player_states();
}

const world::WorldRuntime& ServerRuntime::voxel_world() const noexcept {
  return impl_->voxel_world();
}

world::WorldRuntime& ServerRuntime::voxel_world() noexcept {
  return impl_->voxel_world();
}

const entity::EntityRegistry* ServerRuntime::entity_registry() const noexcept {
  return impl_->entity_registry();
}

const player::GameModeRegistry& ServerRuntime::game_modes() const noexcept {
  return impl_->game_modes();
}

player::GameModeRegistry& ServerRuntime::game_modes() noexcept {
  return impl_->game_modes();
}

const player::PermissionGroupRegistry& ServerRuntime::permission_groups() const noexcept {
  return impl_->permission_groups();
}

player::PermissionGroupRegistry& ServerRuntime::permission_groups() noexcept {
  return impl_->permission_groups();
}

sched::JobSystem* ServerRuntime::world_job_system() noexcept {
  return impl_->world_job_system();
}

const sched::JobSystem* ServerRuntime::world_job_system() const noexcept {
  return impl_->world_job_system();
}

usize ServerRuntime::pending_generation_job_count() const noexcept {
  return impl_->pending_generation_job_count();
}

usize ServerRuntime::pending_lighting_job_count() const noexcept {
  return impl_->pending_lighting_job_count();
}

usize ServerRuntime::pending_fluid_job_count() const noexcept {
  return impl_->pending_fluid_job_count();
}

bool ServerRuntime::set_player_game_mode(PlayerID player_id, std::string_view mode_id) {
  return impl_->set_player_game_mode(player_id, mode_id);
}

player::InventoryInsertResult ServerRuntime::insert_player_inventory_stack(
    PlayerID player_id,
    player::ItemStack stack,
    std::optional<usize> preferred_slot,
    bool replace_preferred_slot) {
  return impl_->insert_player_inventory_stack(
      player_id,
      stack,
      preferred_slot,
      replace_preferred_slot);
}

player::InventoryOperationResult ServerRuntime::apply_player_inventory_click(
    PlayerID player_id,
    usize slot_index,
    player::InventoryClickButton button) {
  return impl_->apply_player_inventory_click(player_id, slot_index, button);
}

std::optional<player::ItemStack> ServerRuntime::drop_player_inventory_cursor(PlayerID player_id) {
  return impl_->drop_player_inventory_cursor(player_id);
}

std::optional<player::ItemStack> ServerRuntime::drop_one_from_player_inventory_cursor(
    PlayerID player_id) {
  return impl_->drop_one_from_player_inventory_cursor(player_id);
}

player::InventoryOperationResult ServerRuntime::close_player_inventory_menu(PlayerID player_id) {
  return impl_->close_player_inventory_menu(player_id);
}

command::CommandResult ServerRuntime::execute_command(
    command::CommandSender sender,
    std::string_view command_text) {
  return impl_->execute_command(std::move(sender), command_text);
}

std::optional<command::CommandSender> ServerRuntime::command_sender_for_session(
    SessionID session_id) const {
  return impl_->command_sender_for_session(session_id);
}

namespace detail {

ServerRuntimeImpl::ServerRuntimeImpl(std::unique_ptr<net::ITransport> transport) : transport_(std::move(transport)) {
}

ServerRuntimeImpl::~ServerRuntimeImpl() {
  shutdown();
}

net::NetworkResult ServerRuntimeImpl::start(const ServerStartInfo& start_info) {
  if (!transport_) {
    return net::NetworkResult::failure(net::NetworkError{"server has no transport"});
  }

  mode_ = start_info.mode;
  base_tps_ = start_info.base_tps;
  tick_index_ = start_info.initial_tick_index;
  world_name_ = start_info.world_name.empty() ? std::string{"default"} : start_info.world_name;
  server_save_root_ = start_info.server_save_root;
  permission_caches_.clear();
  player_saves_.load(start_info.saved_players);
  block_registry_ = start_info.block_registry;
  item_registry_ = start_info.item_registry;
  entity_registry_ = start_info.entity_registry;
  entity_goals_ = start_info.entity_goals;
  game_modes_ = start_info.game_modes == nullptr ? player::GameModeRegistry{}
                                                 : *start_info.game_modes;
  player_tuning_ = start_info.player_tuning;
  block_breaking_ = start_info.block_breaking;
  block_break_behaviors_ = start_info.block_break_behaviors;
  dropped_items_ = start_info.dropped_items;
  combat_ = start_info.combat;
  movement_ = start_info.movement;
  keep_alive_ = start_info.keep_alive;
  keep_alive_enabled_ = start_info.keep_alive_enabled;
  skins_ = start_info.skins;
  language_ = start_info.language;
  plugins_ = start_info.plugins;
  command_registry_ = command::CommandRegistry{};
  if (start_info.register_commands) {
    start_info.register_commands(command_registry_);
  }
  skin_transfers_.clear();
  chat_rate_limits_.clear();
  next_chat_message_id_ = 1U;
  online_profile_sessions_.clear();
  active_skin_uploads_global_ = 0U;
  skin_storage_bytes_ = current_skin_storage_bytes(server_save_root_);
  fluid_definition_ = start_info.fluid_definition;
  starter_inventory_.assign(
      start_info.starter_inventory.begin(),
      start_info.starter_inventory.end());
  fluid_containers_ = start_info.fluid_containers;
  configure_chunk_streaming(start_info.chunk_streaming);
  chunk_generation_ = start_info.chunk_generation;
  if (chunk_generation_.has_value() && chunk_generation_->enabled) {
    default_world_.set_generation_context(
        chunk_generation_->seed,
        chunk_generation_->config);
  } else {
    default_world_.clear_generation_context();
  }
  if (start_info.world_height.has_value()) {
    default_world_.configure_height(start_info.world_height.value());
  } else if (chunk_generation_.has_value() && chunk_generation_->enabled) {
    default_world_.configure_height(
        world::world_height_settings_from_generation_config(chunk_generation_->config));
  }
  world_jobs_.configure(start_info);
  world::WorldTickPolicy tick_policy = start_info.world_tick_policy;
  tick_policy.fluid_budget = 0U;
  default_world_.configure_clock(
      world::DefaultDayLengthSeconds,
      world::DefaultStartDayFraction,
      base_tps_);
  default_world_.set_tick_index(tick_index_);
  default_world_.set_tick_policy(tick_policy);
  worlds_.ensure_default_world();
  handshake_policy_.server_name =
      mode_ == ServerMode::Dedicated
          ? tr(language_, "server.name.dedicated", "Arvane Dedicated Server")
          : tr(language_, "server.name.integrated", "Arvane Integrated Server");
  handshake_policy_.initial_world_id = DefaultWorldID;
  crypto::fill_random(server_instance_id_);
  network_thread_enabled_ = start_info.use_network_thread;

  if (!server_save_root_.empty()) {
    auto loaded_permissions = load_server_permission_groups(server_save_root_);
    if (!loaded_permissions) {
      return net::NetworkResult::failure(
          net::NetworkError{loaded_permissions.error().message});
    }
    permission_groups_ = std::move(loaded_permissions.value());
    ++permission_config_revision_;
  }

  if (block_registry_ == nullptr || item_registry_ == nullptr) {
    return net::NetworkResult::failure(net::NetworkError{"server start requires block and item registries"});
  }
  if (!block_breaking_.behavior_id.empty() &&
      (block_break_behaviors_ == nullptr ||
       !block_break_behaviors_->contains(block_breaking_.behavior_id))) {
    return net::NetworkResult::failure(net::NetworkError{
        "missing block break behavior handler: " + block_breaking_.behavior_id});
  }

  auto listen_result = network_thread_enabled_
                           ? network_thread_.start(*transport_, start_info.listen_address)
                           : transport_->start_listening(start_info.listen_address);
  if (!listen_result) {
    return listen_result;
  }

  state_ = ServerRuntimeState::Running;
  if (plugins_ != nullptr) {
    plugins_->attach_runtime({
        &default_world_,
        block_registry_,
        item_registry_,
        entity_registry_,
        &game_modes_,
        &permission_groups_,
        world_jobs_.job_system(),
        tick_index_,
        chunk_generation_.has_value() ? chunk_generation_->seed : 0U,
        server_save_root_,
        world_name_,
        [this](const plugin::PluginNetworkSendRequest& request) {
          return send_plugin_network_packet(request);
        },
    });
  }
  return net::NetworkResult::success();
}

void ServerRuntimeImpl::configure_chunk_streaming(
    ServerChunkStreamingConfig config) noexcept {
  chunk_streamer_.configure(config);
  max_view_distance_ = std::max<i32>(config.max_view_distance, 0);
  simulation_radius_ =
      std::clamp(config.simulation_distance, 0, max_view_distance_);
}

void ServerRuntimeImpl::tick() {
  if (state_ != ServerRuntimeState::Running || !transport_) {
    return;
  }

  world_jobs_.drain_generation_jobs(default_world_, false);
  world_jobs_.drain_fluid_jobs(default_world_, fluid_definition_, false);
  world_jobs_.drain_lighting_jobs(default_world_, false);
  inbound_events_ = network_thread_enabled_
                        ? network_thread_.drain_events()
                        : transport_->poll_events();
  for (net::NetworkEvent& event : inbound_events_) {
    handle_network_event(std::move(event));
  }
  tick_keep_alive();
  expire_timed_out_skin_uploads();
  tick_players();
  const Seconds tick_seconds = tick_duration_seconds(base_tps_);
  if (plugins_ != nullptr) {
    plugins_->attach_runtime({
        &default_world_,
        block_registry_,
        item_registry_,
        entity_registry_,
        &game_modes_,
        &permission_groups_,
        world_jobs_.job_system(),
        tick_index_,
        chunk_generation_.has_value() ? chunk_generation_->seed : 0U,
        server_save_root_,
        world_name_,
        [this](const plugin::PluginNetworkSendRequest& request) {
          return send_plugin_network_packet(request);
        },
    });
    plugins_->tick(tick_index_);
  }
  default_world_.tick_dropped_items(tick_seconds, block_registry_, item_registry_);
  tick_dropped_item_pickups(tick_seconds);
  update_world_simulation_activity();
  (void)default_world_.tick(*block_registry_, fluid_definition_, entity_goals_);
  process_pending_entity_attacks();
  world_jobs_.schedule_fluid_jobs(default_world_, block_registry_, fluid_definition_);
  world_jobs_.schedule_lighting_jobs(default_world_, block_registry_);
  tick_chunk_streaming();
  world_jobs_.schedule_generation_jobs(default_world_, chunk_streamer_, chunk_generation_);
  send_player_block_break_progress();
  send_block_updates();
  send_entity_replication();
  send_dropped_item_replication();
  send_inventory_updates();
  ++tick_index_;
}

void ServerRuntimeImpl::shutdown() {
  if (state_ == ServerRuntimeState::Stopped) {
    return;
  }

  state_ = ServerRuntimeState::Stopping;
  if (plugins_ != nullptr) {
    plugins_->stop_runtime();
  }
  if (transport_) {
    std::vector<net::ConnectionID> connections;
    connections.reserve(sessions_.size());
    sessions_.for_each_session([&](const ServerSession& session) {
      connections.push_back(session.connection_id);
    });
    for (net::ConnectionID connection_id : connections) {
      kick_client(
          connection_id,
          tr(language_, "server.disconnect.shutting_down", "Server is shutting down."));
    }
  }
  world_jobs_.wait_for_all();
  world_jobs_.drain_generation_jobs(default_world_, true);
  world_jobs_.drain_fluid_jobs(default_world_, fluid_definition_, true);
  world_jobs_.drain_lighting_jobs(default_world_, true);
  if (transport_) {
    if (network_thread_enabled_) {
      network_thread_.request_stop();
      network_thread_.join();
      network_thread_enabled_ = false;
      state_ = ServerRuntimeState::Stopped;
      return;
    }
    transport_->shutdown();
  }
  state_ = ServerRuntimeState::Stopped;
}

net::NetworkResult ServerNetworkThread::start(
    net::ITransport& transport,
    const net::ListenAddress& listen_address) {
  stop_requested_.store(false, std::memory_order_release);
  if (!thread_.start()) {
    stop_requested_.store(true, std::memory_order_release);
    return net::NetworkResult::failure(net::NetworkError{"failed to start network thread"});
  }

  net::NetworkResult start_result = thread_.call([&transport, &listen_address] {
    return transport.start_listening(listen_address);
  });
  if (!start_result) {
    request_stop();
    join();
    return start_result;
  }

  if (!thread_.post([this, &transport] {
        run(transport);
      })) {
    request_stop();
    join();
    return net::NetworkResult::failure(net::NetworkError{"failed to start network pump"});
  }
  core::info("threading", "network_owner=network thread");
  return net::NetworkResult::success();
}

void ServerNetworkThread::request_stop() noexcept {
  stop_requested_.store(true, std::memory_order_release);
  thread_.request_stop();
}

void ServerNetworkThread::join() {
  thread_.join();
}

std::vector<net::NetworkEvent> ServerNetworkThread::drain_events() {
  return events_.drain();
}

void ServerNetworkThread::run(net::ITransport& transport) {
  using namespace std::chrono_literals;
  while (!stop_requested_.load(std::memory_order_acquire)) {
    std::vector<net::NetworkEvent> polled = transport.poll_events();
    if (!polled.empty()) {
      for (net::NetworkEvent& event : polled) {
        events_.push(std::move(event));
      }
    }
    std::this_thread::sleep_for(1ms);
  }
  std::vector<net::NetworkEvent> polled = transport.poll_events();
  if (!polled.empty()) {
    for (net::NetworkEvent& event : polled) {
      events_.push(std::move(event));
    }
  }
  transport.shutdown();
}

ServerRuntimeState ServerRuntimeImpl::state() const noexcept {
  return state_;
}

TickIndex ServerRuntimeImpl::tick_index() const noexcept {
  return tick_index_;
}

TickRate ServerRuntimeImpl::base_tps() const noexcept {
  return base_tps_;
}

ServerMode ServerRuntimeImpl::mode() const noexcept {
  return mode_;
}

const SessionRegistry& ServerRuntimeImpl::sessions() const noexcept {
  return sessions_;
}

const WorldRegistry& ServerRuntimeImpl::worlds() const noexcept {
  return worlds_;
}

const command::CommandRegistry& ServerRuntimeImpl::command_registry() const noexcept {
  return command_registry_;
}

const std::vector<ServerPlayerSaveState>& ServerRuntimeImpl::saved_player_states() const noexcept {
  return player_saves_.states();
}

const world::WorldRuntime& ServerRuntimeImpl::voxel_world() const noexcept {
  return default_world_;
}

world::WorldRuntime& ServerRuntimeImpl::voxel_world() noexcept {
  return default_world_;
}

const entity::EntityRegistry* ServerRuntimeImpl::entity_registry() const noexcept {
  return entity_registry_;
}

const player::GameModeRegistry& ServerRuntimeImpl::game_modes() const noexcept {
  return game_modes_;
}

player::GameModeRegistry& ServerRuntimeImpl::game_modes() noexcept {
  ++permission_config_revision_;
  return game_modes_;
}

const player::PermissionGroupRegistry& ServerRuntimeImpl::permission_groups() const noexcept {
  return permission_groups_;
}

player::PermissionGroupRegistry& ServerRuntimeImpl::permission_groups() noexcept {
  ++permission_config_revision_;
  return permission_groups_;
}

const sched::JobSystem* ServerRuntimeImpl::world_job_system() const noexcept {
  return world_jobs_.job_system();
}

sched::JobSystem* ServerRuntimeImpl::world_job_system() noexcept {
  return world_jobs_.job_system();
}

usize ServerRuntimeImpl::pending_generation_job_count() const noexcept {
  return world_jobs_.pending_generation_job_count();
}

usize ServerRuntimeImpl::pending_lighting_job_count() const noexcept {
  return world_jobs_.pending_lighting_job_count();
}

usize ServerRuntimeImpl::pending_fluid_job_count() const noexcept {
  return world_jobs_.pending_fluid_job_count();
}

command::CommandResult ServerRuntimeImpl::execute_command(
    command::CommandSender sender,
    std::string_view command_text) {
  auto find_player_by_id = [this](PlayerID player_id) {
    return online_player_by_id(sessions_, default_world_, player_id);
  };
  auto find_player_by_name = [this](std::string_view display_name) {
    return online_player_by_name(sessions_, default_world_, display_name);
  };
  auto move_player = [this](PlayerID player_id, Vec3f position) {
    return teleport_player(default_world_, player_id, position);
  };
  command::ServerCommandServices domain_services{
      command::OnlinePlayerByIDFn{find_player_by_id},
      command::OnlinePlayerByNameFn{find_player_by_name},
      command::TeleportPlayerFn{move_player},
  };
  auto permission_check =
      [this](const command::CommandSender& checked_sender, const command::Command& checked_command) {
        return check_command_permission(checked_sender, checked_command);
      };
  command::CommandServices services{
      &domain_services,
      command::CommandPermissionCheckFn{permission_check},
      language_,
  };
  return command::dispatch_command(
      command_registry_,
      command::CommandDomain::ServerBound,
      sender,
      services,
      command_text);
}

std::optional<command::CommandSender> ServerRuntimeImpl::command_sender_for_session(
    SessionID session_id) const {
  const ServerSession* session = sessions_.find(session_id);
  if (session == nullptr) {
    return std::nullopt;
  }

  command::CommandSender sender;
  sender.kind = command::CommandSourceKind::Player;
  if (!session->profile_id.empty()) {
    sender.player_profile_id = session->profile_id;
  }
  sender.session_id = session->session_id;
  sender.player_id = session->player_id;
  sender.world_id = session->world_id;
  sender.display_name = session->display_name;

  if (const player::PlayerState* player_state = default_world_.find_player(session->player_id);
      player_state != nullptr) {
    sender.position = player_state->position;
  }
  if (const std::optional<EntityID> entity_id =
          default_world_.entity_for_player(session->player_id);
      entity_id.has_value()) {
    sender.entity_id = entity_id.value();
  }

  return sender;
}

command::CommandPermissionResult ServerRuntimeImpl::check_command_permission(
    const command::CommandSender& sender,
    const command::Command& command_def) {
  if (sender.kind == command::CommandSourceKind::ServerConsole) {
    return command::CommandPermissionResult::allow();
  }
  if (command_def.permission.empty()) {
    return command::CommandPermissionResult::allow();
  }
  if (!sender.player_id.has_value()) {
    return command::CommandPermissionResult::deny(tr(
        language_,
        "command.dispatcher.permission_missing_player_context",
        "Permission denied: missing player context."));
  }

  player::PlayerState* player_state = default_world_.find_player(sender.player_id.value());
  if (player_state == nullptr) {
    return command::CommandPermissionResult::deny(tr(
        language_,
        "command.dispatcher.permission_player_not_online",
        "Permission denied: player is not online."));
  }

  refresh_player_permissions(*player_state);
  if (player_state->resolved_permissions.has(command_def.permission)) {
    return command::CommandPermissionResult::allow();
  }

  return command::CommandPermissionResult::deny(tr(
      language_,
      "command.dispatcher.permission_denied",
      {{"permission", command_def.permission}},
      "Permission denied: {permission}."));
}

} // namespace detail
} // namespace arv::server
