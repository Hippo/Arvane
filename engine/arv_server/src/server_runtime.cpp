#include "server_runtime_internal.hpp"

#include "arv/command/server_command_services.hpp"
#include "arv/core/log.hpp"
#include "arv/core/string.hpp"
#include "arv/crypto/random.hpp"
#include "arv/io/data_codecs.hpp"
#include "arv/io/files.hpp"
#include "arv/persistence/save.hpp"
#include "arv/server/server_commands.hpp"
#include "arv/server/skin_runtime.hpp"
#include "arv/text/language.hpp"
#include "arv/text/rich_text.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace arv::server {
namespace {

[[nodiscard]] command::OnlinePlayerCommandTarget command_target(
    const ServerSession& session,
    const player::PlayerState& player_state) {
  return {
      session.session_id,
      session.player_id,
      session.world_id,
      session.display_name,
      player_state.position,
      player_state.yaw_radians,
      player_state.pitch_radians,
  };
}

[[nodiscard]] std::vector<command::OnlinePlayerCommandTarget> online_players(
    const SessionRegistry& sessions,
    const world::WorldRuntime& world) {
  std::vector<command::OnlinePlayerCommandTarget> players;
  for (const SessionID session_id : sessions.session_ids()) {
    const ServerSession* session = sessions.find(session_id);
    if (session == nullptr || !session->authenticated || session->closing) {
      continue;
    }
    const player::PlayerState* player_state = world.find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }
    players.push_back(command_target(*session, *player_state));
  }
  std::sort(players.begin(), players.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.display_name < rhs.display_name;
  });
  return players;
}

[[nodiscard]] command::EntityCommandTarget entity_command_target(
    const world::WorldRuntime& world,
    EntityID entity_id,
    const SessionRegistry& sessions) {
  command::EntityCommandTarget target;
  target.entity_id = entity_id;
  target.world_id = world.id();
  if (const entity::EntityTransform* transform = world.find_entity_transform(entity_id);
      transform != nullptr) {
    target.position = transform->position;
  }
  if (const entity::EntityType* type = world.find_entity_type(entity_id);
      type != nullptr) {
    target.display_name = type->type_name;
  }
  if (const std::optional<PlayerID> player_id = world.player_for_entity(entity_id);
      player_id.has_value()) {
    target.player_id = player_id.value();
    for (const SessionID session_id : sessions.session_ids()) {
      const ServerSession* session = sessions.find(session_id);
      if (session != nullptr && session->player_id == player_id.value()) {
        target.session_id = session->session_id;
        target.display_name = session->display_name;
        break;
      }
    }
  }
  if (target.display_name.empty()) {
    target.display_name = "entity:" + std::to_string(entity_id.value);
  }
  return target;
}

[[nodiscard]] std::optional<command::OnlinePlayerCommandTarget> nearest_player(
    const std::vector<command::OnlinePlayerCommandTarget>& players,
    std::optional<Vec3f> anchor) {
  if (players.empty()) {
    return std::nullopt;
  }
  if (!anchor.has_value()) {
    return players.front();
  }
  return *std::min_element(players.begin(), players.end(), [&](const auto& lhs, const auto& rhs) {
    return distance_squared(lhs.position, anchor.value()) <
           distance_squared(rhs.position, anchor.value());
  });
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
  save.permission_overrides = player.permission_overrides;
  save.respawn_location = player.respawn_location;
  save.selected_hotbar_slot = player.selected_hotbar_slot;
  save.inventory = player.inventory;
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
  save.permission_overrides = player_state.permission_overrides;
  save.respawn_location = player_state.respawn_location;
  save.selected_hotbar_slot = player_state.selected_hotbar_slot;
  save.inventory = player_state.inventory;
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
    chunks.push_back(std::move(saved));
  }
  return chunks;
}

[[nodiscard]] std::string format_game_rule_type(world::GameRuleValueType type) {
  switch (type) {
    case world::GameRuleValueType::Boolean:
      return "bool";
    case world::GameRuleValueType::Integer:
      return "int";
    case world::GameRuleValueType::String:
      return "string";
  }
  return "unknown";
}

[[nodiscard]] command::GameRuleCommandEntry command_rule_from_world(
    const world::GameRuleState& rule) {
  return {
      rule.name,
      format_game_rule_type(rule.type),
      world::game_rule_value_to_string(rule),
      rule.description};
}

[[nodiscard]] std::optional<io::DataDocument> parse_stack_metadata(
    std::string_view text,
    std::string& error) {
  auto decoded_json = io::decode_json_value(text, 1U);
  if (decoded_json) {
    return std::move(decoded_json.value());
  }
  auto decoded_yaml = io::decode_yaml_value(text, 1U);
  if (decoded_yaml) {
    return std::move(decoded_yaml.value());
  }
  error = "Invalid item metadata: " + decoded_json.error().describe();
  return std::nullopt;
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

[[nodiscard]] bool teleport_player_location(
    world::WorldRuntime& world,
    PlayerID player_id,
    Location location) {
  if (location.world.has_value() && location.world.value() != world.id()) {
    return false;
  }
  if (!teleport_player(world, player_id, location_position(location))) {
    return false;
  }
  player::PlayerState* player_state = world.find_player(player_id);
  if (player_state == nullptr) {
    return false;
  }
  player_state->yaw_radians = location.yaw_radians;
  player_state->pitch_radians = location.pitch_radians;
  return true;
}

[[nodiscard]] command::PlayerBlockSelection command_selection_from_player(
    const player::PlayerSelection& selection) {
  return {selection.world, selection.a, selection.b};
}

[[nodiscard]] player::PlayerSelection player_selection_from_command(
    const command::PlayerBlockSelection& selection) {
  return {selection.world, selection.a, selection.b};
}

[[nodiscard]] bool safe_blueprint_file_name_char(char c) noexcept {
  const bool ascii_alnum =
      (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9');
  return ascii_alnum || c == '_' || c == '-' || c == '.';
}

[[nodiscard]] bool valid_blueprint_file_name(std::string_view name) noexcept {
  if (name.empty() || name == "." || name == ".." ||
      name.find("..") != std::string_view::npos) {
    return false;
  }
  for (const char c : name) {
    if (!safe_blueprint_file_name_char(c)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::filesystem::path blueprint_root_path(
    const std::filesystem::path& server_save_root,
    std::string_view world_name) {
  return server_save_root / "worlds" / std::string{world_name} / "blueprints";
}

[[nodiscard]] std::filesystem::path blueprint_file_path(
    const std::filesystem::path& server_save_root,
    std::string_view world_name,
    std::string_view name) {
  return blueprint_root_path(server_save_root, world_name) /
         (std::string{name} + ".abp");
}

[[nodiscard]] std::vector<std::string> list_blueprint_files(
    const std::filesystem::path& server_save_root,
    std::string_view world_name) {
  std::vector<std::string> names;
  if (server_save_root.empty()) {
    return names;
  }

  std::error_code error;
  const std::filesystem::path root =
      blueprint_root_path(server_save_root, world_name);
  if (!std::filesystem::is_directory(root, error) || error) {
    return names;
  }
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(root, error)) {
    if (error) {
      break;
    }
    if (!entry.is_regular_file(error) || error ||
        entry.path().extension() != ".abp") {
      error.clear();
      continue;
    }
    const std::string name = entry.path().stem().string();
    if (valid_blueprint_file_name(name)) {
      names.push_back(name);
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

[[nodiscard]] std::optional<std::string> load_blueprint_file(
    const std::filesystem::path& server_save_root,
    std::string_view world_name,
    std::string_view name) {
  if (server_save_root.empty() || !valid_blueprint_file_name(name)) {
    return std::nullopt;
  }
  auto text = io::read_text_file_contents(
      blueprint_file_path(server_save_root, world_name, name));
  if (!text) {
    return std::nullopt;
  }
  return std::move(text.value());
}

[[nodiscard]] bool save_blueprint_file(
    const std::filesystem::path& server_save_root,
    std::string_view world_name,
    std::string_view name,
    std::string_view text) {
  if (server_save_root.empty() || !valid_blueprint_file_name(name)) {
    return false;
  }

  std::error_code error;
  const std::filesystem::path path =
      blueprint_file_path(server_save_root, world_name, name);
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
    output << text;
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

} // namespace

ServerRuntime::ServerRuntime(std::unique_ptr<net::ITransport> transport) : impl_(std::make_unique<detail::ServerRuntimeImpl>(std::move(transport))) {
}

world::NaturalSpawningConfig make_natural_spawning_config(
    const io::NaturalSpawningConfig& config) noexcept {
  world::NaturalSpawningConfig out;
  out.enabled = config.enabled;
  out.interval_ticks = config.interval_ticks;
  out.min_distance_from_player_blocks = config.min_distance_from_player_blocks;
  out.max_distance_from_player_blocks = config.max_distance_from_player_blocks;
  out.attempts_per_player_per_category = config.attempts_per_player_per_category;
  out.max_successes_per_player_per_tick = config.max_successes_per_player_per_tick;
  out.max_successes_per_world_per_tick = config.max_successes_per_world_per_tick;
  out.local_density_radius_blocks = config.local_density_radius_blocks;
  auto copy_cap = [](const io::NaturalSpawningCategoryCapConfig& from) {
    return world::SpawnCategoryCapConfig{
        from.per_player,
        from.per_world_soft,
        from.local_max};
  };
  out.caps[world::spawn_category_index(world::SpawnCategory::Passive)] =
      copy_cap(config.caps.passive);
  out.caps[world::spawn_category_index(world::SpawnCategory::Hostile)] =
      copy_cap(config.caps.hostile);
  out.caps[world::spawn_category_index(world::SpawnCategory::Ambient)] =
      copy_cap(config.caps.ambient);
  out.caps[world::spawn_category_index(world::SpawnCategory::Water)] =
      copy_cap(config.caps.water);
  out.caps[world::spawn_category_index(world::SpawnCategory::Underground)] =
      copy_cap(config.caps.underground);
  out.caps[world::spawn_category_index(world::SpawnCategory::Special)] =
      copy_cap(config.caps.special);
  auto copy_despawn = [](const io::NaturalSpawningDespawnCategoryConfig& from) {
    return world::NaturalDespawnCategoryConfig{
        from.natural_despawn,
        from.soft_distance_blocks,
        from.hard_distance_blocks,
        from.min_lifetime_ticks,
        from.save_transient};
  };
  out.despawning[world::spawn_category_index(world::SpawnCategory::Passive)] =
      copy_despawn(config.despawning.passive);
  out.despawning[world::spawn_category_index(world::SpawnCategory::Hostile)] =
      copy_despawn(config.despawning.hostile);
  out.despawning[world::spawn_category_index(world::SpawnCategory::Ambient)] =
      copy_despawn(config.despawning.ambient);
  out.despawning[world::spawn_category_index(world::SpawnCategory::Water)] =
      copy_despawn(config.despawning.water);
  out.despawning[world::spawn_category_index(world::SpawnCategory::Underground)] =
      copy_despawn(config.despawning.underground);
  out.despawning[world::spawn_category_index(world::SpawnCategory::Special)] =
      copy_despawn(config.despawning.special);
  return out;
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

const entity::VehicleDefinitionRegistry* ServerRuntime::vehicle_registry() const noexcept {
  return impl_->vehicle_registry();
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

const world::NaturalSpawnCounters& ServerRuntime::natural_spawn_counters() const noexcept {
  return impl_->natural_spawn_counters();
}

std::optional<VehicleCrashDebugInfo> ServerRuntime::last_vehicle_crash_debug(
    EntityNetID vehicle_id) const {
  return impl_->last_vehicle_crash_debug(vehicle_id);
}

bool ServerRuntime::set_player_game_mode(PlayerID player_id, std::string_view mode_id) {
  return impl_->set_player_game_mode(player_id, mode_id);
}

bool ServerRuntime::set_player_fly_speed(PlayerID player_id, f32 fly_speed) {
  return impl_->set_player_fly_speed(player_id, fly_speed);
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
  vehicle_registry_ = start_info.vehicle_registry;
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
  accepted_vehicle_movement_.clear();
  vehicle_crash_cooldowns_.clear();
  last_vehicle_crash_debug_.clear();
  crash_remount_cooldowns_.clear();
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
  default_world_.set_spawn_point(start_info.world_spawn);
  default_world_.configure_clock(
      world::DefaultDayLengthSeconds,
      world::DefaultStartDayFraction,
      base_tps_);
  default_world_.set_tick_index(tick_index_);
  default_world_.set_tick_policy(tick_policy);
  worlds_.ensure_default_world();
  handshake_policy_.server_name =
      mode_ == ServerMode::Dedicated
          ? arv::text::text_or_fallback(language_, "server.name.dedicated", "Arvane Dedicated Server")
          : arv::text::text_or_fallback(language_, "server.name.integrated", "Arvane Integrated Server");
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
  natural_spawns_.configure(start_info.natural_spawning);
  if (start_info.natural_spawning.enabled) {
    if (entity_registry_ == nullptr) {
      core::warn("natural_spawning", "disabled: server start has no entity registry");
      world::NaturalSpawningConfig disabled = start_info.natural_spawning;
      disabled.enabled = false;
      natural_spawns_.configure(disabled);
    } else if (!chunk_generation_.has_value() ||
               !chunk_generation_->config.resolved_dimension) {
      core::warn("natural_spawning", "disabled: world generation has no resolved dimension");
      world::NaturalSpawningConfig disabled = start_info.natural_spawning;
      disabled.enabled = false;
      natural_spawns_.configure(disabled);
    } else {
      world::SpawnRuleRegistry rules = world::SpawnRuleRegistry::build_from_dimension(
          *chunk_generation_->config.resolved_dimension,
          *entity_registry_,
          *block_registry_);
      for (const world::SpawnDiagnostic& diagnostic : rules.diagnostics()) {
        core::warn("natural_spawning", diagnostic.describe());
      }
      core::info(
          "natural_spawning",
          "loaded {} natural spawn entries (diagnostics={})",
          rules.entry_count(),
          rules.diagnostics().size());
      natural_spawns_.set_rules(std::move(rules));
    }
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
        vehicle_registry_,
        &game_modes_,
        &permission_groups_,
        world_jobs_.job_system(),
        tick_index_,
        chunk_generation_.has_value() ? chunk_generation_->seed : 0U,
        server_save_root_,
        world_name_,
        [this](PlayerID player_id) -> std::optional<std::string> {
          std::optional<std::string> stable_id;
          sessions_.for_each_session([&](const ServerSession& session) {
            if (session.player_id == player_id && !session.profile_id.empty()) {
              stable_id = session.profile_id.string();
            }
          });
          return stable_id;
        },
        [this](const plugin::PluginNetworkSendRequest& request) {
          return send_plugin_network_packet(request);
        },
        &text_variables_,
        [this](const plugin::PluginChatMessageSendRequest& request) {
          return send_plugin_chat_message(request);
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
        vehicle_registry_,
        &game_modes_,
        &permission_groups_,
        world_jobs_.job_system(),
        tick_index_,
        chunk_generation_.has_value() ? chunk_generation_->seed : 0U,
        server_save_root_,
        world_name_,
        [this](PlayerID player_id) -> std::optional<std::string> {
          std::optional<std::string> stable_id;
          sessions_.for_each_session([&](const ServerSession& session) {
            if (session.player_id == player_id && !session.profile_id.empty()) {
              stable_id = session.profile_id.string();
            }
          });
          return stable_id;
        },
        [this](const plugin::PluginNetworkSendRequest& request) {
          return send_plugin_network_packet(request);
        },
        &text_variables_,
        [this](const plugin::PluginChatMessageSendRequest& request) {
          return send_plugin_chat_message(request);
        },
    });
    plugins_->tick(tick_index_);
  }
  default_world_.tick_dropped_items(tick_seconds, block_registry_, item_registry_);
  tick_dropped_item_pickups(tick_seconds);
  update_world_simulation_activity();
  if (entity_registry_ != nullptr) {
    world_jobs_.drain_natural_spawn_jobs(
        default_world_,
        natural_spawns_,
        block_registry_,
        entity_registry_,
        vehicle_registry_,
        tick_index_,
        false);
    world_jobs_.schedule_natural_spawn_jobs(
        default_world_,
        natural_spawns_,
        tick_index_);
  }
  (void)default_world_.tick(
      *block_registry_,
      fluid_definition_,
      entity_goals_,
      vehicle_registry_);
  process_pending_entity_attacks();
  tick_vehicle_damage_effects();
  default_world_.flush_entity_despawns();
  broadcast_pending_ride_ended_events();
  world_jobs_.schedule_fluid_jobs(default_world_, block_registry_, fluid_definition_);
  world_jobs_.schedule_lighting_jobs(default_world_, block_registry_);
  tick_chunk_streaming();
  world_jobs_.schedule_generation_jobs(default_world_, chunk_streamer_, chunk_generation_);
  send_player_block_break_progress();
  send_block_updates();
  send_entity_replication();
  send_dropped_item_replication();
  send_inventory_updates();
  send_text_variable_updates();
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
          arv::text::text_or_fallback(language_, "server.disconnect.shutting_down", "Server is shutting down."));
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

const entity::VehicleDefinitionRegistry* ServerRuntimeImpl::vehicle_registry() const noexcept {
  return vehicle_registry_;
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

const world::NaturalSpawnCounters& ServerRuntimeImpl::natural_spawn_counters() const noexcept {
  return natural_spawns_.counters();
}

std::optional<VehicleCrashDebugInfo> ServerRuntimeImpl::last_vehicle_crash_debug(
    EntityNetID vehicle_id) const {
  const auto found = last_vehicle_crash_debug_.find(vehicle_id);
  if (found == last_vehicle_crash_debug_.end()) {
    return std::nullopt;
  }
  return found->second;
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
  auto move_player_location = [this](PlayerID player_id, Location location) {
    return teleport_player_location(default_world_, player_id, location);
  };
  auto set_game_mode = [this](PlayerID player_id, std::string_view mode_id) {
    return set_player_game_mode(player_id, mode_id);
  };
  auto set_fly_speed = [this](PlayerID player_id, f32 fly_speed) {
    return set_player_fly_speed(player_id, fly_speed);
  };
  auto default_fly_speed = [this]() {
    return player_tuning_.fly_speed;
  };
  auto max_fly_speed = [this]() {
    return movement_.max_fly_speed_blocks_per_second;
  };
  auto get_selection = [this](PlayerID player_id) -> std::optional<command::PlayerBlockSelection> {
    player::PlayerState* player_state = default_world_.find_player(player_id);
    if (player_state == nullptr) {
      return std::nullopt;
    }
    return command_selection_from_player(player_state->selection);
  };
  auto set_selection = [this](PlayerID player_id, const command::PlayerBlockSelection& selection) {
    player::PlayerState* player_state = default_world_.find_player(player_id);
    if (player_state == nullptr) {
      return false;
    }
    player_state->selection = player_selection_from_command(selection);
    return true;
  };
  auto read_block = [this](WorldID world_id, BlockPos pos) -> std::optional<u32> {
    if (world_id != default_world_.id()) {
      return std::nullopt;
    }
    return default_world_.get_block(pos).value;
  };
  auto write_block = [this](WorldID world_id, BlockPos pos, u32 block_variant) {
    if (world_id != default_world_.id() || block_registry_ == nullptr ||
        block_registry_->variant(voxel::BlockVariantID{block_variant}) == nullptr) {
      return false;
    }
    return default_world_.set_block(pos, voxel::BlockVariantID{block_variant});
  };
  auto block_variant_name = [this](u32 block_variant) -> std::optional<std::string> {
    if (block_registry_ == nullptr) {
      return std::nullopt;
    }
    const voxel::BlockVariantRecord* variant =
        block_registry_->variant(voxel::BlockVariantID{block_variant});
    if (variant == nullptr) {
      return std::nullopt;
    }
    return variant->debug_name;
  };
  auto block_variant_by_name = [this](std::string_view stable_id) -> std::optional<u32> {
    if (block_registry_ == nullptr) {
      return std::nullopt;
    }
    const std::optional<voxel::BlockVariantID> variant =
        block_registry_->find_variant(stable_id);
    if (!variant.has_value()) {
      return std::nullopt;
    }
    return variant->value;
  };
  auto air_block_variant = [this]() -> std::optional<u32> {
    if (block_registry_ == nullptr) {
      return std::nullopt;
    }
    return block_registry_->air_variant_id().value;
  };
  auto list_blueprints = [this](WorldID world_id) {
    if (world_id != default_world_.id()) {
      return std::vector<std::string>{};
    }
    return list_blueprint_files(server_save_root_, world_name_);
  };
  auto load_blueprint = [this](WorldID world_id, std::string_view name) {
    if (world_id != default_world_.id()) {
      return std::optional<std::string>{};
    }
    return load_blueprint_file(server_save_root_, world_name_, name);
  };
  auto save_blueprint =
      [this](WorldID world_id, std::string_view name, std::string_view text) {
        if (world_id != default_world_.id()) {
          return false;
        }
        return save_blueprint_file(server_save_root_, world_name_, name, text);
      };
  auto read_world_spawn = [this](WorldID world_id) -> std::optional<Location> {
    if (world_id != default_world_.id()) {
      return std::nullopt;
    }
    return default_world_.spawn_point();
  };
  auto write_world_spawn = [this](WorldID world_id, Location location) {
    if (world_id != default_world_.id()) {
      return false;
    }
    if (location.world.has_value() && location.world.value() != world_id) {
      return false;
    }
    location.world = world_id;
    default_world_.set_spawn_point(location);
    return true;
  };
  auto list_online_players = [this]() {
    return online_players(sessions_, default_world_);
  };
  auto resolve_player_targets =
      [this](const command::CommandSender& sender, std::string_view selector) {
        std::vector<command::OnlinePlayerCommandTarget> matches;
        if (selector == "@s") {
          if (sender.player_id.has_value()) {
            if (auto target = online_player_by_id(
                    sessions_,
                    default_world_,
                    sender.player_id.value());
                target.has_value()) {
              matches.push_back(target.value());
            }
          }
          return matches;
        }
        if (selector == "@a") {
          return online_players(sessions_, default_world_);
        }
        if (selector == "@p") {
          if (auto target = nearest_player(
                  online_players(sessions_, default_world_),
                  sender.position);
              target.has_value()) {
            matches.push_back(target.value());
          }
          return matches;
        }
        if (auto target = online_player_by_name(sessions_, default_world_, selector);
            target.has_value()) {
          matches.push_back(target.value());
        }
        return matches;
      };
  auto resolve_entity_targets =
      [this](const command::CommandSender& sender, std::string_view selector) {
        std::vector<command::EntityCommandTarget> matches;
        auto add_player_entity = [&](const command::OnlinePlayerCommandTarget& player) {
          if (const std::optional<EntityID> entity_id =
                  default_world_.entity_for_player(player.player_id);
              entity_id.has_value()) {
            matches.push_back(entity_command_target(default_world_, entity_id.value(), sessions_));
          }
        };

        if (selector == "@s") {
          if (sender.entity_id.has_value() &&
              default_world_.entity_exists(sender.entity_id.value())) {
            matches.push_back(
                entity_command_target(default_world_, sender.entity_id.value(), sessions_));
          } else if (sender.player_id.has_value()) {
            if (const std::optional<EntityID> entity_id =
                    default_world_.entity_for_player(sender.player_id.value());
                entity_id.has_value()) {
              matches.push_back(
                  entity_command_target(default_world_, entity_id.value(), sessions_));
            }
          }
          return matches;
        }
        if (selector == "@a") {
          for (const command::OnlinePlayerCommandTarget& player :
               online_players(sessions_, default_world_)) {
            add_player_entity(player);
          }
          return matches;
        }
        if (selector == "@p") {
          if (auto player = nearest_player(
                  online_players(sessions_, default_world_),
                  sender.position);
              player.has_value()) {
            add_player_entity(player.value());
          }
          return matches;
        }
        if (selector == "@e") {
          for (const EntityID entity_id : default_world_.entity_ids()) {
            matches.push_back(entity_command_target(default_world_, entity_id, sessions_));
          }
          return matches;
        }
        if (auto player = online_player_by_name(sessions_, default_world_, selector);
            player.has_value()) {
          add_player_entity(player.value());
        }
        return matches;
      };
  auto query_world_time = [this](WorldID world_id) -> std::optional<command::WorldTimeCommandState> {
    if (world_id != default_world_.id()) {
      return std::nullopt;
    }
    const world::WorldClock& clock = default_world_.clock();
    return command::WorldTimeCommandState{
        clock.world_tick,
        clock.day_time_tick,
        clock.day_length_ticks,
        clock.daylight_cycle};
  };
  auto set_world_time = [this](WorldID world_id, u32 tick) {
    if (world_id != default_world_.id()) {
      return false;
    }
    default_world_.set_day_time_tick(tick);
    broadcast_world_time(world_id);
    return true;
  };
  auto add_world_time = [this](WorldID world_id, u32 ticks) {
    if (world_id != default_world_.id()) {
      return false;
    }
    default_world_.add_day_time_ticks(ticks);
    broadcast_world_time(world_id);
    return true;
  };
  auto query_weather = [this](WorldID world_id) -> std::optional<std::string> {
    if (world_id != default_world_.id()) {
      return std::nullopt;
    }
    return default_world_.weather_id();
  };
  auto set_weather = [this](WorldID world_id, std::string_view weather) {
    if (world_id != default_world_.id() || weather.empty()) {
      return false;
    }
    return default_world_.set_weather_id(std::string{weather});
  };
  auto query_difficulty = [this](WorldID world_id) -> std::optional<std::string> {
    if (world_id != default_world_.id()) {
      return std::nullopt;
    }
    return std::string{world::world_difficulty_name(default_world_.difficulty())};
  };
  auto set_difficulty = [this](WorldID world_id, std::string_view difficulty) {
    if (world_id != default_world_.id()) {
      return false;
    }
    const std::optional<world::WorldDifficulty> parsed =
        world::world_difficulty_from_string(difficulty);
    if (!parsed.has_value()) {
      return false;
    }
    default_world_.set_difficulty(parsed.value());
    return true;
  };
  auto list_rules = [this](WorldID world_id) {
    std::vector<command::GameRuleCommandEntry> rules;
    if (world_id != default_world_.id()) {
      return rules;
    }
    for (const world::GameRuleState& rule : default_world_.game_rules()) {
      rules.push_back(command_rule_from_world(rule));
    }
    return rules;
  };
  auto get_rule = [this](WorldID world_id, std::string_view name)
      -> std::optional<command::GameRuleCommandEntry> {
    if (world_id != default_world_.id()) {
      return std::nullopt;
    }
    const world::GameRuleState* rule = default_world_.game_rule(name);
    return rule == nullptr ? std::nullopt
                           : std::optional<command::GameRuleCommandEntry>{
                                 command_rule_from_world(*rule)};
  };
  auto set_rule =
      [this](WorldID world_id, std::string_view name, std::string_view value) {
        command::GameRuleSetCommandResult result;
        if (world_id != default_world_.id()) {
          result.message = "Unknown world.";
          return result;
        }
        const world::GameRuleState* rule = default_world_.game_rule(name);
        if (rule == nullptr) {
          result.message = "Unknown game rule: " + std::string{name} + ".";
          return result;
        }
        result.old_value = world::game_rule_value_to_string(*rule);
        switch (rule->type) {
          case world::GameRuleValueType::Boolean: {
            const std::string lowered = arv::lower_ascii_copy(value);
            const bool parsed_true =
                lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on";
            const bool parsed_false =
                lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off";
            if (!parsed_true && !parsed_false) {
              result.message = "Game rule " + std::string{name} + " expects a boolean value.";
              return result;
            }
            const bool bool_value = parsed_true;
            if (!default_world_.set_game_rule_bool(name, bool_value)) {
              result.message = "Unable to update game rule: " + std::string{name} + ".";
              return result;
            }
            if (name == "doMobSpawning") {
              world::NaturalSpawningConfig config = natural_spawns_.config();
              config.enabled = bool_value;
              natural_spawns_.configure(config);
            }
            break;
          }
          case world::GameRuleValueType::Integer: {
            char* end = nullptr;
            const std::string text{value};
            const long long parsed = std::strtoll(text.c_str(), &end, 10);
            if (end == text.c_str() || *end != '\0') {
              result.message = "Game rule " + std::string{name} + " expects an integer value.";
              return result;
            }
            if (!default_world_.set_game_rule_integer(name, static_cast<i64>(parsed))) {
              result.message = "Unable to update game rule: " + std::string{name} + ".";
              return result;
            }
            break;
          }
          case world::GameRuleValueType::String:
            if (!default_world_.set_game_rule_string(name, std::string{value})) {
              result.message = "Unable to update game rule: " + std::string{name} + ".";
              return result;
            }
            break;
        }
        const world::GameRuleState* updated = default_world_.game_rule(name);
        result.new_value =
            updated == nullptr ? std::string{value} : world::game_rule_value_to_string(*updated);
        result.success = true;
        return result;
      };
  auto entity_type_by_name =
      [this](std::string_view name) -> std::optional<command::EntityTypeCommandRecord> {
        if (entity_registry_ == nullptr) {
          return std::nullopt;
        }
        const std::optional<EntityTypeID> type_id = entity_registry_->find_type(name);
        if (!type_id.has_value()) {
          return std::nullopt;
        }
        const entity::EntityTypeRecord* record = entity_registry_->type(type_id.value());
        if (record == nullptr) {
          return std::nullopt;
        }
        return command::EntityTypeCommandRecord{
            record->id.value,
            record->name,
            record->display_name,
            record->spawnable};
      };
  auto spawn_entity =
      [this](WorldID world_id, std::string_view type_name, Vec3f position)
      -> std::optional<command::EntityCommandTarget> {
        if (world_id != default_world_.id() || entity_registry_ == nullptr) {
          return std::nullopt;
        }
        const std::optional<EntityTypeID> type_id = entity_registry_->find_type(type_name);
        if (!type_id.has_value()) {
          return std::nullopt;
        }
        const entity::EntityTypeRecord* record = entity_registry_->type(type_id.value());
        if (record == nullptr || !record->spawnable) {
          return std::nullopt;
        }
        world::EntityCreateInfo info;
        info.type_id = record->id;
        info.type_name = record->name;
        info.position = position;
        info.persistent = record->persistent_by_default;
        info.replicated = record->replicated_by_default;
        info.authored_components = record->components;
        info.vehicle_definitions = vehicle_registry_;
        const EntityID entity_id = default_world_.create_entity(std::move(info));
        if (entity_id.value == 0U) {
          return std::nullopt;
        }
        return entity_command_target(default_world_, entity_id, sessions_);
      };
  auto kill_entity = [this](EntityID entity_id) {
    if (!default_world_.entity_exists(entity_id)) {
      return false;
    }
    bool affected = false;
    if (default_world_.find_entity_health(entity_id) != nullptr) {
      const world::DamageResult damage = default_world_.apply_damage({
          {},
          entity_id,
          std::numeric_limits<f32>::max(),
          world::DamageCause::Generic,
          true});
      affected = damage.applied || damage.killed;
      if (damage.killed && default_world_.player_for_entity(entity_id).has_value()) {
        return true;
      }
    }
    if (!default_world_.player_for_entity(entity_id).has_value()) {
      affected = default_world_.destroy_entity(entity_id, world::EntityDespawnReason::Removed) ||
                 affected;
      default_world_.flush_entity_despawns();
      broadcast_pending_ride_ended_events();
    }
    return affected;
  };
  auto item_by_name = [this](std::string_view name) -> std::optional<command::ItemCommandRecord> {
    if (item_registry_ == nullptr) {
      return std::nullopt;
    }
    const std::optional<player::ItemID> item_id = item_registry_->find_item(name);
    if (!item_id.has_value()) {
      return std::nullopt;
    }
    const player::ItemRecord* record = item_registry_->item(item_id.value());
    if (record == nullptr || item_registry_->is_air(record->id)) {
      return std::nullopt;
    }
    return command::ItemCommandRecord{
        record->id.value,
        record->name,
        record->display_name,
        record->max_stack_size};
  };
  auto give_player_item =
      [this](
          PlayerID player_id,
          u32 item_id,
          u16 count,
          std::optional<std::string> metadata) {
        command::InventoryInsertCommandResult out;
        out.requested = count;
        if (item_registry_ == nullptr || item_registry_->item(player::ItemID{item_id}) == nullptr ||
            count == 0U) {
          out.success = false;
          out.message = "Unknown item or invalid count.";
          out.leftover = count;
          return out;
        }
        player::ItemStack stack{player::ItemID{item_id}, count};
        if (metadata.has_value()) {
          std::string error;
          std::optional<io::DataDocument> document =
              parse_stack_metadata(metadata.value(), error);
          if (!document.has_value()) {
            out.success = false;
            out.message = std::move(error);
            out.leftover = count;
            return out;
          }
          auto encoded = io::encode_json_value(document->root);
          if (!encoded || encoded.value().size() > player::MaxItemStackDataBytes) {
            out.success = false;
            out.message = "Item metadata is too large.";
            out.leftover = count;
            return out;
          }
          stack.data = std::move(document.value());
        }
        const player::InventoryInsertResult result =
            insert_player_inventory_stack(player_id, std::move(stack));
        out.leftover = result.leftover.has_value() ? result.leftover->count : 0U;
        out.inserted = out.requested - out.leftover;
        return out;
      };
  auto clear_player_inventory =
      [this](PlayerID player_id, std::optional<u32> item_id) {
        command::InventoryClearCommandResult out;
        player::PlayerState* player_state = default_world_.find_player(player_id);
        if (player_state == nullptr) {
          out.success = false;
          out.message = "Player is not online.";
          return out;
        }
        auto matches = [&](const player::ItemStack& stack) {
          return !item_id.has_value() || stack.item.value == item_id.value();
        };
        for (usize slot = 0; slot < player_state->inventory.slot_count(); ++slot) {
          const std::optional<player::ItemStack> stack = player_state->inventory.slot(slot);
          if (!stack.has_value() || !matches(stack.value())) {
            continue;
          }
          out.removed_items += stack->count;
          ++out.removed_stacks;
          (void)player_state->inventory.clear_slot(slot);
        }
        if (player_state->inventory_cursor_stack.has_value() &&
            matches(player_state->inventory_cursor_stack.value())) {
          out.removed_items += player_state->inventory_cursor_stack->count;
          ++out.removed_stacks;
          player_state->inventory_cursor_stack.reset();
        }
        return out;
      };
  auto apply_status_effect =
      [this](EntityID entity_id, std::string_view effect_id, u32 duration_ticks, u32 amplifier) {
        return default_world_.apply_status_effect(
            entity_id,
            std::string{effect_id},
            duration_ticks,
            amplifier);
      };
  auto clear_status_effects =
      [this](EntityID entity_id, std::optional<std::string_view> effect_id) {
        return default_world_.clear_status_effects(entity_id, effect_id);
      };
  auto list_status_effects = [this](EntityID entity_id) {
    std::vector<command::StatusEffectCommandState> out;
    for (const world::StatusEffectInstance& effect :
         default_world_.status_effects(entity_id)) {
      out.push_back({effect.effect_id, effect.remaining_ticks, effect.amplifier});
    }
    return out;
  };
  auto save_all = [this](bool flush) {
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
    const std::vector<persistence::SavedChunkSection> chunks =
        capture_saved_chunks(default_world_);
    std::vector<persistence::PlayerSaveData> players;
    players.reserve(player_saves_.states().size() + sessions_.size());
    for (const ServerPlayerSaveState& saved : player_saves_.states()) {
      persistence::upsert_player_save(players, to_persistence_player_save(saved));
    }
    for (const SessionID session_id : sessions_.session_ids()) {
      const ServerSession* session = sessions_.find(session_id);
      if (session == nullptr) {
        continue;
      }
      const player::PlayerState* player_state =
          default_world_.find_player(session->player_id);
      if (player_state == nullptr) {
        continue;
      }
      persistence::upsert_player_save(
          players,
          to_persistence_player_save(*session, *player_state, world_name_));
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
    const std::vector<entity::PersistentEntityRecord> entities =
        default_world_.persistent_entity_records();
    const world::WorldGenerationConfig* active_generation =
        default_world_.generation_config();
    const world::WorldHeightSettings height = default_world_.height_settings();
    persistence::SaveMetadata save_metadata{
        persistence::CurrentSaveFormatVersion,
        std::string{persistence::CurrentGameVersion},
        persistence::hash_block_registry(*block_registry_),
        persistence::hash_item_registry(*item_registry_),
        server_save_root_.filename().string()};
    persistence::WorldMetadata world_metadata{
        world_name_,
        chunk_generation_.has_value() ? chunk_generation_->seed : 0U,
        default_world_.tick_index(),
        active_generation == nullptr ? world::WorldGenerationConfig{} : *active_generation,
        height.min_chunk_y,
        height.max_chunk_y,
        persistence::hash_block_registry(*block_registry_),
        persistence::hash_item_registry(*item_registry_),
        default_world_.clock(),
        {},
        true,
        default_world_.spawn_point()};
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
    out.success = true;
    out.saved_chunks = static_cast<u32>(chunks.size());
    out.saved_players = static_cast<u32>(players.size());
    out.saved_entities = static_cast<u32>(entities.size());
    return out;
  };
  auto create_explosion =
      [this](const command::ExplosionCommandRequest& request) {
        return create_explosion_from_command(request);
      };
  auto broadcast_message = [this](std::string_view message) {
    broadcast_system_message(arv::text::plain_rich_text(std::string{message}));
    return true;
  };
  auto kick_player = [this](PlayerID player_id, std::string_view reason) {
    for (const SessionID session_id : sessions_.session_ids()) {
      const ServerSession* session = sessions_.find(session_id);
      if (session == nullptr || session->player_id != player_id) {
        continue;
      }
      kick_client(session->connection_id, reason);
      return true;
    }
    return false;
  };
  auto debug_stats = [this](WorldID world_id) {
    command::DebugCommandStats stats;
    stats.tick_index = tick_index_;
    stats.tick_rate = base_tps_;
    if (world_id == default_world_.id()) {
      stats.entity_count = default_world_.entity_count();
      stats.loaded_chunk_count = default_world_.loaded_chunk_count();
      stats.active_chunk_count = default_world_.active_chunk_count();
    }
    stats.pending_generation_jobs = world_jobs_.pending_generation_job_count();
    stats.pending_lighting_jobs = world_jobs_.pending_lighting_job_count();
    stats.pending_fluid_jobs = world_jobs_.pending_fluid_job_count();
    return stats;
  };

  command::ServerCommandServices domain_services;
  domain_services.online_player_by_id = command::OnlinePlayerByIDFn{find_player_by_id};
  domain_services.online_player_by_name = command::OnlinePlayerByNameFn{find_player_by_name};
  domain_services.online_players = command::OnlinePlayersFn{list_online_players};
  domain_services.resolve_player_targets =
      command::ResolvePlayerTargetsFn{resolve_player_targets};
  domain_services.resolve_entity_targets =
      command::ResolveEntityTargetsFn{resolve_entity_targets};
  domain_services.teleport_player = command::TeleportPlayerFn{move_player};
  domain_services.teleport_player_location =
      command::TeleportPlayerLocationFn{move_player_location};
  domain_services.set_player_game_mode = command::SetPlayerGameModeFn{set_game_mode};
  domain_services.set_player_fly_speed = command::SetPlayerFlySpeedFn{set_fly_speed};
  domain_services.default_player_fly_speed =
      command::DefaultPlayerFlySpeedFn{default_fly_speed};
  domain_services.max_player_fly_speed = command::MaxPlayerFlySpeedFn{max_fly_speed};
  domain_services.query_world_time = command::WorldTimeQueryFn{query_world_time};
  domain_services.set_world_time = command::WorldTimeSetFn{set_world_time};
  domain_services.add_world_time = command::WorldTimeAddFn{add_world_time};
  domain_services.query_world_weather = command::WorldWeatherQueryFn{query_weather};
  domain_services.set_world_weather = command::WorldWeatherSetFn{set_weather};
  domain_services.query_world_difficulty =
      command::WorldDifficultyQueryFn{query_difficulty};
  domain_services.set_world_difficulty =
      command::WorldDifficultySetFn{set_difficulty};
  domain_services.list_game_rules = command::GameRuleListFn{list_rules};
  domain_services.get_game_rule = command::GameRuleGetFn{get_rule};
  domain_services.set_game_rule = command::GameRuleSetFn{set_rule};
  domain_services.entity_type_by_name = command::EntityTypeByNameFn{entity_type_by_name};
  domain_services.spawn_entity = command::SpawnEntityFn{spawn_entity};
  domain_services.kill_entity = command::KillEntityFn{kill_entity};
  domain_services.item_by_name = command::ItemByNameFn{item_by_name};
  domain_services.give_player_item = command::GivePlayerItemFn{give_player_item};
  domain_services.clear_player_inventory =
      command::ClearPlayerInventoryFn{clear_player_inventory};
  domain_services.apply_status_effect =
      command::ApplyStatusEffectFn{apply_status_effect};
  domain_services.clear_status_effects =
      command::ClearStatusEffectsFn{clear_status_effects};
  domain_services.list_status_effects =
      command::ListStatusEffectsFn{list_status_effects};
  domain_services.save_all = command::SaveAllFn{save_all};
  domain_services.create_explosion =
      command::CreateExplosionFn{create_explosion};
  domain_services.broadcast_system_message =
      command::BroadcastSystemMessageFn{broadcast_message};
  domain_services.kick_player = command::KickPlayerFn{kick_player};
  domain_services.debug_stats = command::DebugStatsFn{debug_stats};
  domain_services.player_selection_by_id = command::PlayerSelectionByIDFn{get_selection};
  domain_services.set_player_selection = command::SetPlayerSelectionFn{set_selection};
  domain_services.read_block = command::WorldBlockReadFn{read_block};
  domain_services.write_block = command::WorldBlockWriteFn{write_block};
  domain_services.block_variant_name = command::BlockVariantNameFn{block_variant_name};
  domain_services.block_variant_by_name =
      command::BlockVariantByNameFn{block_variant_by_name};
  domain_services.air_block_variant = command::AirBlockVariantFn{air_block_variant};
  domain_services.list_blueprints = command::BlueprintListFn{list_blueprints};
  domain_services.load_blueprint = command::BlueprintLoadFn{load_blueprint};
  domain_services.save_blueprint = command::BlueprintSaveFn{save_blueprint};
  domain_services.read_world_spawn = command::WorldSpawnReadFn{read_world_spawn};
  domain_services.write_world_spawn = command::WorldSpawnWriteFn{write_world_spawn};
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
    return command::CommandPermissionResult::deny(arv::text::text_or_fallback(language_,
                                                                              "command.dispatcher.permission_missing_player_context",
                                                                              "Permission denied: missing player context."));
  }

  player::PlayerState* player_state = default_world_.find_player(sender.player_id.value());
  if (player_state == nullptr) {
    return command::CommandPermissionResult::deny(arv::text::text_or_fallback(language_,
                                                                              "command.dispatcher.permission_player_not_online",
                                                                              "Permission denied: player is not online."));
  }

  refresh_player_permissions(*player_state);
  if (player_state->resolved_permissions.has(command_def.permission)) {
    return command::CommandPermissionResult::allow();
  }

  return command::CommandPermissionResult::deny(arv::text::format_or_fallback(
      language_,
      "command.dispatcher.permission_denied",
      {{"permission", command_def.permission}},
      "Permission denied: {permission}."));
}

} // namespace detail
} // namespace arv::server
