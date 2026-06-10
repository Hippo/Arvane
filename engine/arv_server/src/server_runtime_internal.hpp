#pragma once

#include "arv/server/server_runtime.hpp"

#include "arv/command/command_dispatcher.hpp"
#include "arv/command/server_command_services.hpp"
#include "arv/core/rate_limiter.hpp"
#include "arv/physics/collision.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/entity_interaction.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/explosion.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/inventory_sync.hpp"
#include "arv/protocol/particles.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/session_control.hpp"
#include "arv/protocol/skins.hpp"
#include "arv/protocol/text_variables.hpp"
#include "arv/protocol/vehicles.hpp"
#include "arv/protocol/world_join.hpp"
#include "arv/sched/completion_queue.hpp"
#include "arv/sched/owner_thread.hpp"
#include "arv/server/chat_service.hpp"
#include "arv/server/permission_config.hpp"
#include "arv/server/player_movement_validation.hpp"
#include "arv/server/player_profile.hpp"
#include "arv/server/vehicle_movement_validation.hpp"
#include "arv/text/variables.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/explosion.hpp"
#include "arv/world/world_lighting.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::server::detail {

[[nodiscard]] inline protocol::PlayerMoveFlags player_move_flags_from_state(
    const player::PlayerState& player_state) noexcept {
  protocol::PlayerMoveFlags flags = protocol::PlayerMoveFlags::None;
  if (player_state.grounded) {
    flags |= protocol::PlayerMoveFlags::OnGround;
  }
  if (player_state.sneaking) {
    flags |= protocol::PlayerMoveFlags::Sneaking;
  }
  if (player_state.swimming) {
    flags |= protocol::PlayerMoveFlags::Swimming;
  }
  if (player_state.movement_mode == player::MovementMode::Flying) {
    flags |= protocol::PlayerMoveFlags::Flying;
  }
  return flags;
}

struct BlockBreakAttempt {
  BlockPos block{};
  voxel::BlockVariantID variant{};
  player::ItemID held_item{};
  f32 elapsed_seconds{};
  f32 required_seconds{};
  f32 progress{};
  u8 stage{};
};

struct EntityReplicationState {
  HashSet<EntityNetID> known_entities{};
  HashSet<EntityNetID> known_dropped_items{};
};

struct InventoryReplicationState {
  u32 revision{};
  bool initialized{};
  std::vector<std::optional<player::ItemStack>> slots{};
  std::optional<player::ItemStack> carried_stack{};
  u8 selected_hotbar_slot{};
};

struct ActiveSkinUpload {
  u32 upload_id{};
  protocol::ServerboundSkinUploadBegin begin{};
  protocol::SkinUploadMode mode{protocol::SkinUploadMode::Full};
  u32 max_chunk_bytes{};
  TickIndex started_tick{};
  TickIndex last_packet_tick{};
  SessionID session_id{};
  std::vector<u8> bytes{};
};

struct SkinTransferSessionState {
  u32 next_upload_id{1};
  std::optional<ActiveSkinUpload> active_upload{};
  HashSet<std::string> advertised_skin_hashes{};
  HashSet<u32> in_flight_request_ids{};
  HashSet<std::string> in_flight_skin_hashes{};
  core::TokenBucket upload_begin_limiter{};
  core::TokenBucket completed_upload_minute_limiter{};
  core::TokenBucket completed_upload_hour_limiter{};
  core::TokenBucket upload_byte_limiter{};
  core::TokenBucket failed_decode_limiter{};
  core::TokenBucket request_second_limiter{};
  core::TokenBucket request_minute_limiter{};
  core::TokenBucket download_byte_limiter{};
  bool limiters_initialized{};
};

struct PendingLightingJob {
  u64 source_revision{};
  sched::JobFuture<world::LightingResult> job;
};

struct PendingFluidJob {
  sched::JobFuture<world::FluidSimulationResult> job;
};

struct PendingGenerationJob {
  ChunkCoord coord{};
  sched::JobFuture<world::GeneratedChunkResult> job;
};

struct PendingNaturalSpawnJob {
  TickIndex source_tick{};
  sched::JobFuture<world::NaturalSpawnJobResult> job;
};

struct PlayerPermissionCache {
  u64 source_fingerprint{};
  u64 permission_config_revision{};
  u64 region_registry_revision{};
  bool initialized{};
  bool has_last_region_query{};
  WorldID last_region_world{};
  Vec3f last_region_position{};
  std::vector<world::RegionID> active_regions{};
  player::PermissionLayer region_layer{};
};

struct PendingEntityAttack {
  SessionID session_id{};
  protocol::ServerboundAttackEntity attack{};
};

enum class VehicleDamageStage : u8 {
  Healthy,
  Damaged,
  Critical,
  Destroyed,
};

struct VehicleSmokeState {
  VehicleDamageStage stage{VehicleDamageStage::Healthy};
  TickIndex next_smoke_tick{};
};

class ServerWorldJobRuntime {
public:
  ServerWorldJobRuntime();

  void configure(const ServerStartInfo& start_info);
  void wait_for_all();

  void drain_generation_jobs(world::WorldRuntime& world, bool drain_all);
  void schedule_generation_jobs(
      world::WorldRuntime& world,
      const ServerChunkStreamer& chunk_streamer,
      const std::optional<ServerChunkGenerationSettings>& chunk_generation);
  void drain_lighting_jobs(world::WorldRuntime& world, bool drain_all);
  void schedule_lighting_jobs(
      world::WorldRuntime& world,
      const voxel::BlockRegistry* block_registry);
  void drain_fluid_jobs(
      world::WorldRuntime& world,
      const voxel::FluidDefinition& fluid_definition,
      bool drain_all);
  void schedule_fluid_jobs(
      world::WorldRuntime& world,
      const voxel::BlockRegistry* block_registry,
      const voxel::FluidDefinition& fluid_definition);
  void drain_natural_spawn_jobs(
      world::WorldRuntime& world,
      world::NaturalSpawnSystem& natural_spawns,
      const voxel::BlockRegistry* block_registry,
      const entity::EntityRegistry* entity_registry,
      const entity::VehicleDefinitionRegistry* vehicle_registry,
      TickIndex tick,
      bool drain_all);
  void schedule_natural_spawn_jobs(
      world::WorldRuntime& world,
      world::NaturalSpawnSystem& natural_spawns,
      TickIndex tick);

  [[nodiscard]] sched::JobSystem* job_system() noexcept;
  [[nodiscard]] const sched::JobSystem* job_system() const noexcept;
  [[nodiscard]] usize pending_generation_job_count() const noexcept;
  [[nodiscard]] usize pending_lighting_job_count() const noexcept;
  [[nodiscard]] usize pending_fluid_job_count() const noexcept;
  [[nodiscard]] usize pending_natural_spawn_job_count() const noexcept;

private:
  std::unique_ptr<sched::JobSystem> job_system_;
  std::vector<PendingGenerationJob> pending_generation_jobs_;
  std::vector<PendingLightingJob> pending_lighting_jobs_;
  std::vector<PendingFluidJob> pending_fluid_jobs_;
  std::vector<PendingNaturalSpawnJob> pending_natural_spawn_jobs_;
  u32 max_pending_generation_jobs_{4};
  u32 max_pending_lighting_jobs_{2};
  u32 max_pending_fluid_jobs_{1};
  u32 max_pending_natural_spawn_jobs_{1};
  u32 max_generation_results_per_tick_{2};
  u32 max_lighting_results_per_tick_{1};
  u32 max_lighting_sections_per_job_{32};
  u32 max_fluid_results_per_tick_{1};
  u32 max_natural_spawn_results_per_tick_{1};
  u32 fluid_update_interval_{1};
  u32 fluid_update_budget_{2000};
};

class ServerPlayerSaveCache {
public:
  void load(std::span<const ServerPlayerSaveState> saves);
  [[nodiscard]] bool apply_to_player(
      std::string_view world_name,
      const ServerSession& session,
      player::PlayerState& player_state) const;
  void record(
      std::string_view world_name,
      const ServerSession& session,
      const player::PlayerState& player_state);
  [[nodiscard]] std::string selected_skin_hash_for(
      std::string_view world_name,
      const ServerSession& session) const;
  [[nodiscard]] bool selected_skin_slim_model_for(
      std::string_view world_name,
      const ServerSession& session) const;
  void set_selected_skin_for(
      std::string_view world_name,
      const ServerSession& session,
      std::string skin_hash,
      bool slim_model);
  [[nodiscard]] const std::vector<ServerPlayerSaveState>& states() const noexcept;

private:
  std::vector<ServerPlayerSaveState> states_;
};

class ServerNetworkThread {
public:
  [[nodiscard]] net::NetworkResult start(
      net::ITransport& transport,
      const net::ListenAddress& listen_address);
  void request_stop() noexcept;
  void join();
  [[nodiscard]] std::vector<net::NetworkEvent> drain_events();

private:
  void run(net::ITransport& transport);

  sched::NetworkThread thread_;
  std::atomic_bool stop_requested_{true};
  sched::CompletionQueue<net::NetworkEvent> events_;
};

struct ServerRuntimeImpl {
  explicit ServerRuntimeImpl(std::unique_ptr<net::ITransport> transport);
  ~ServerRuntimeImpl();

  net::NetworkResult start(const ServerStartInfo& start_info);
  void tick();
  void shutdown();
  void configure_chunk_streaming(ServerChunkStreamingConfig config) noexcept;

  [[nodiscard]] ServerRuntimeState state() const noexcept;
  [[nodiscard]] TickIndex tick_index() const noexcept;
  [[nodiscard]] TickRate base_tps() const noexcept;
  [[nodiscard]] ServerMode mode() const noexcept;
  [[nodiscard]] const SessionRegistry& sessions() const noexcept;
  [[nodiscard]] const WorldRegistry& worlds() const noexcept;
  [[nodiscard]] const command::CommandRegistry& command_registry() const noexcept;
  [[nodiscard]] std::vector<DroppedItemStack> dropped_item_stacks() const;
  [[nodiscard]] const std::vector<ServerPlayerSaveState>& saved_player_states() const noexcept;
  [[nodiscard]] const world::WorldRuntime& voxel_world() const noexcept;
  [[nodiscard]] world::WorldRuntime& voxel_world() noexcept;
  [[nodiscard]] const entity::EntityRegistry* entity_registry() const noexcept;
  [[nodiscard]] const entity::VehicleDefinitionRegistry* vehicle_registry() const noexcept;
  [[nodiscard]] const player::GameModeRegistry& game_modes() const noexcept;
  [[nodiscard]] player::GameModeRegistry& game_modes() noexcept;
  [[nodiscard]] const player::PermissionGroupRegistry& permission_groups() const noexcept;
  [[nodiscard]] player::PermissionGroupRegistry& permission_groups() noexcept;
  [[nodiscard]] const sched::JobSystem* world_job_system() const noexcept;
  [[nodiscard]] sched::JobSystem* world_job_system() noexcept;
  [[nodiscard]] usize pending_generation_job_count() const noexcept;
  [[nodiscard]] usize pending_lighting_job_count() const noexcept;
  [[nodiscard]] usize pending_fluid_job_count() const noexcept;
  [[nodiscard]] const world::NaturalSpawnCounters& natural_spawn_counters() const noexcept;
  [[nodiscard]] std::optional<VehicleCrashDebugInfo> last_vehicle_crash_debug(
      EntityNetID vehicle_id) const;

  [[nodiscard]] bool set_player_game_mode(PlayerID player_id, std::string_view mode_id);
  [[nodiscard]] bool set_player_fly_speed(PlayerID player_id, f32 fly_speed);
  [[nodiscard]] player::InventoryInsertResult insert_player_inventory_stack(
      PlayerID player_id,
      player::ItemStack stack,
      std::optional<usize> preferred_slot = std::nullopt,
      bool replace_preferred_slot = false);
  [[nodiscard]] player::InventoryOperationResult apply_player_inventory_click(
      PlayerID player_id,
      usize slot_index,
      player::InventoryClickButton button);
  [[nodiscard]] std::optional<player::ItemStack> drop_player_inventory_cursor(PlayerID player_id);
  [[nodiscard]] std::optional<player::ItemStack> drop_one_from_player_inventory_cursor(
      PlayerID player_id);
  [[nodiscard]] player::InventoryOperationResult close_player_inventory_menu(PlayerID player_id);
  [[nodiscard]] command::CommandResult execute_command(
      command::CommandSender sender,
      std::string_view command_text);
  [[nodiscard]] std::optional<command::CommandSender> command_sender_for_session(
      SessionID session_id) const;
  [[nodiscard]] command::CommandPermissionResult check_command_permission(
      const command::CommandSender& sender,
      const command::Command& command_def);
  [[nodiscard]] command::ExplosionCommandResult create_explosion_from_command(
      const command::ExplosionCommandRequest& request);
  [[nodiscard]] std::optional<world::ExplosionResult> execute_explosion_request(
      world::ExplosionRequest request);
  [[nodiscard]] plugin::PluginNetworkSendResult send_plugin_network_packet(
      const plugin::PluginNetworkSendRequest& request);
  [[nodiscard]] plugin::PluginNetworkSendResult send_plugin_chat_message(
      const plugin::PluginChatMessageSendRequest& request);

  void handle_network_event(net::NetworkEvent event);
  void handle_connection_lost(net::ConnectionID connection_id);
  void handle_message(net::ConnectionID connection_id, const std::vector<u8>& packet);
  void handle_client_hello(net::ConnectionID connection_id, const protocol::ServerboundHello& client_hello);
  void handle_auth_response(
      net::ConnectionID connection_id,
      const protocol::ServerboundAuthResponse& response);
  void handle_player_move(net::ConnectionID connection_id, const protocol::ServerboundPlayerMove& move);
  void handle_vehicle_move(
      net::ConnectionID connection_id,
      const protocol::ServerboundVehicleMove& move);
  void handle_ride_entity_request(
      net::ConnectionID connection_id,
      const protocol::ServerboundRideEntityRequest& request);
  void handle_dismount_request(
      net::ConnectionID connection_id,
      const protocol::ServerboundDismountRequest& request);
  void handle_vehicle_action(
      net::ConnectionID connection_id,
      const protocol::ServerboundVehicleAction& action);
  [[nodiscard]] bool apply_vehicle_crash(
      EntityID vehicle_entity,
      EntityNetID vehicle_net_id,
      const entity::VehicleDefinition& definition,
      const VehicleCrashAssessment& assessment,
      const VehicleCrashContact& contact,
      AcceptedVehicleMovementState& accepted,
      VehicleCrashDebugInfo* debug);
  void handle_chunk_interest_update(
      net::ConnectionID connection_id,
      const protocol::ServerboundChunkInterestUpdate& interest);
  void handle_inventory_action_request(
      net::ConnectionID connection_id,
      const protocol::ServerboundInventoryActionRequest& request);
  void handle_chat_message(
      net::ConnectionID connection_id,
      const protocol::ServerboundChatMessage& message);
  void handle_attack_entity(
      net::ConnectionID connection_id,
      const protocol::ServerboundAttackEntity& attack);
  void handle_skin_request(
      net::ConnectionID connection_id,
      const protocol::ServerboundSkinRequest& request);
  void handle_skin_request_batch(
      net::ConnectionID connection_id,
      const protocol::ServerboundSkinRequestBatch& batch);
  void handle_skin_upload_begin(
      net::ConnectionID connection_id,
      const protocol::ServerboundSkinUploadBegin& begin);
  void handle_skin_upload_data(
      net::ConnectionID connection_id,
      const protocol::ServerboundSkinUploadData& data);
  void handle_skin_upload_chunk(
      net::ConnectionID connection_id,
      const protocol::ServerboundSkinUploadChunk& chunk);
  void handle_skin_upload_end(
      net::ConnectionID connection_id,
      const protocol::ServerboundSkinUploadEnd& end);
  void handle_keep_alive(
      net::ConnectionID connection_id,
      const protocol::ServerboundKeepAlive& keep_alive);

  void kick_client(net::ConnectionID connection_id, std::string_view reason);
  void cleanup_connection(net::ConnectionID connection_id);
  void tick_keep_alive();
  void send_keep_alive(ServerSession& session, std::chrono::steady_clock::time_point now);
  [[nodiscard]] bool profile_is_online_for_another_session(
      const ServerSession& session) const;
  void register_online_profile(const ServerSession& session);
  void remove_online_profile_for_session(const ServerSession& session);

  void tick_players();
  void update_world_simulation_activity();
  void refresh_player_permissions(player::PlayerState& player_state);
  void apply_player_action_command(player::PlayerState& player_state, const player::PlayerCommand& command);

  void apply_player_interaction_command(
      player::PlayerState& player_state,
      const player::PlayerCommand& command,
      const physics::ICollisionQuery& interaction_query,
      Vec3f eye_position,
      Vec3f look_direction,
      const physics::KinematicConfig& physics_config,
      Seconds tick_seconds);
  void clear_block_break_attempt(PlayerID player_id);
  void tick_block_break_attempt(
      player::PlayerState& player_state,
      const player::PlayerCommand& command,
      const physics::ICollisionQuery& interaction_query,
      Vec3f eye_position,
      Vec3f look_direction,
      Seconds tick_seconds);

  void tick_dropped_item_pickups(Seconds tick_seconds);
  void spawn_dropped_item_stack(WorldID world_id, Vec3f position, player::ItemStack stack);
  void expire_timed_out_skin_uploads();
  void tick_vehicle_damage_effects();
  void handle_vehicle_damage_result(
      EntityID vehicle_entity,
      const world::DamageResult& damage,
      Vec3f normal,
      u32 seed);
  [[nodiscard]] bool destroy_vehicle_from_damage(
      EntityID vehicle_entity,
      Vec3f normal,
      u32 seed,
      bool broadcast_explosion);

  void tick_chunk_streaming();
  void broadcast_explosion_event(const world::ExplosionResult& result);
  void spawn_explosion_drops(const world::ExplosionResult& result);
  void send_explosion_player_velocities(const world::ExplosionResult& result);

  void send_player_move_correction(
      const ServerSession& session,
      const player::PlayerState& player_state,
      u32 rejected_sequence,
      protocol::PlayerMoveCorrectionReason reason);
  void send_player_velocity(
      const ServerSession& session,
      const player::PlayerState& player_state);
  void send_vehicle_correction(
      const ServerSession& session,
      EntityNetID vehicle_net_id,
      const AcceptedVehicleMovementState& accepted,
      u32 rejected_sequence,
      protocol::VehicleCorrectionReason reason);
  void broadcast_vehicle_state(
      EntityNetID vehicle_net_id,
      const AcceptedVehicleMovementState& accepted);
  void broadcast_vehicle_smoke_particles(
      Vec3f position,
      VehicleDamageStage stage,
      u32 seed);
  void broadcast_entity_passengers(EntityID rideable_id);
  void broadcast_ride_started(
      EntityID passenger_id,
      EntityID rideable_id,
      std::string_view seat_id,
      bool controls_vehicle);
  void broadcast_ride_ended(
      EntityID passenger_id,
      EntityID rideable_id,
      Vec3f position);
  void broadcast_ride_ended(
      EntityNetID passenger_net_id,
      EntityNetID rideable_net_id,
      Vec3f position);
  void broadcast_pending_ride_ended_events();
  void send_auth_result(
      const ServerSession& session,
      bool accepted,
      std::string_view reason_if_rejected = {});
  void send_auth_challenge(ServerSession& session);
  [[nodiscard]] bool complete_authenticated_session(ServerSession& session);
  void send_world_join_result(const ServerSession& session, const player::PlayerState& player_state);
  void send_world_time(const ServerSession& session);
  void broadcast_world_time(WorldID world_id);
  void send_player_permissions(const ServerSession& session, const player::PlayerState& player_state);
  void send_player_movement_tuning(
      const ServerSession& session,
      const player::PlayerState& player_state);
  void send_movement_hz_if_non_default(const ServerSession& session);
  void send_skin_rules(const ServerSession& session, const player::PlayerState& player_state);
  void send_player_profile_info_to(
      const ServerSession& receiver,
      const ServerSession& subject);
  void broadcast_player_profile_info(const ServerSession& subject);
  void send_system_message(const ServerSession& recipient, arv::text::RichText message);
  void send_error_message(const ServerSession& recipient, arv::text::RichText message);
  void broadcast_system_message(arv::text::RichText message);
  void send_block_updates();
  void send_player_block_break_progress();
  void send_entity_replication();
  void send_dropped_item_replication();
  void send_inventory_updates();
  void refresh_builtin_text_variables();
  void send_text_variable_updates();
  void process_pending_entity_attacks();
  [[nodiscard]] protocol::ClientboundInventoryUpdate make_inventory_update(
      const player::PlayerState& player_state,
      u32 revision) const;

  bool apply_saved_player_state(const ServerSession& session, player::PlayerState& player_state);
  void record_player_save_state(const ServerSession& session, const player::PlayerState& player_state);
  [[nodiscard]] std::string selected_skin_hash_for_session(const ServerSession& session) const;
  [[nodiscard]] bool selected_skin_slim_model_for_session(const ServerSession& session) const;
  void set_selected_skin_for_session(
      const ServerSession& session,
      std::string skin_hash,
      bool slim_model);
  void seed_player_inventory(player::PlayerState& player_state);
  [[nodiscard]] bool inventory_is_empty(const player::Inventory& inventory) const noexcept;

  std::unique_ptr<net::ITransport> transport_;
  ServerNetworkThread network_thread_;
  bool network_thread_enabled_{};
  ServerRuntimeState state_{ServerRuntimeState::Stopped};
  ServerMode mode_{ServerMode::Integrated};
  TickRate base_tps_{DefaultBaseTps};
  TickIndex tick_index_{};
  i32 max_view_distance_{};
  std::string world_name_{"default"};
  protocol::HandshakePolicy handshake_policy_{};
  protocol::AuthServerInstanceID server_instance_id_{};
  SessionRegistry sessions_;
  command::CommandRegistry command_registry_;
  PlayerProfileStore profiles_;
  WorldRegistry worlds_;
  world::WorldRuntime default_world_{DefaultWorldID};
  world::NaturalSpawnSystem natural_spawns_{};
  std::vector<net::NetworkEvent> inbound_events_;
  const voxel::BlockRegistry* block_registry_{};
  const player::ItemRegistry* item_registry_{};
  const entity::EntityRegistry* entity_registry_{};
  const entity::VehicleDefinitionRegistry* vehicle_registry_{};
  const world::EntityGoalRegistry* entity_goals_{};
  player::PlayerTuning player_tuning_{};
  BlockBreakingConfig block_breaking_{};
  const BlockBreakBehaviorRegistry* block_break_behaviors_{};
  DroppedItemsConfig dropped_items_{};
  CombatConfig combat_{};
  io::MovementValidationConfig movement_{};
  io::KeepAliveConfig keep_alive_{};
  io::SkinConfig skins_{};
  voxel::FluidDefinition fluid_definition_{};
  std::vector<StarterInventoryItem> starter_inventory_{};
  FluidContainerInteractionConfig fluid_containers_{};
  ServerChunkStreamer chunk_streamer_{};
  std::optional<ServerChunkGenerationSettings> chunk_generation_{};
  std::vector<QueuedChunkStreamMessage> queued_chunk_stream_messages_;
  player::GameModeRegistry game_modes_{};
  player::PermissionGroupRegistry permission_groups_{};
  std::filesystem::path server_save_root_{};
  u64 permission_config_revision_{};
  HashMap<PlayerID, PlayerPermissionCache> permission_caches_;
  HashMap<PlayerID, player::PlayerCommand> pending_player_commands_;
  std::vector<PendingEntityAttack> pending_entity_attacks_;
  HashMap<PlayerID, AcceptedPlayerMovementState> accepted_player_movement_;
  HashMap<EntityNetID, AcceptedVehicleMovementState> accepted_vehicle_movement_;
  HashMap<EntityNetID, TickIndex> vehicle_crash_cooldowns_;
  HashMap<EntityNetID, VehicleCrashDebugInfo> last_vehicle_crash_debug_;
  HashMap<EntityNetID, VehicleSmokeState> vehicle_smoke_states_;
  HashMap<PlayerID, TickIndex> crash_remount_cooldowns_;
  HashMap<PlayerID, double> last_player_move_correction_seconds_;
  HashMap<PlayerID, protocol::BlockBreakProgress> last_sent_block_break_progress_;
  arv::text::TextVariableStore text_variables_;
  HashMap<PlayerID, BlockBreakAttempt> block_break_attempts_;
  HashMap<SessionID, PlayerCombatState> combat_states_;
  HashSet<SessionID> logged_chunk_interest_sessions_;
  HashSet<SessionID> logged_chunk_delivery_sessions_;
  ServerPlayerSaveCache player_saves_;
  HashMap<SessionID, EntityReplicationState> entity_replication_;
  HashMap<PlayerID, InventoryReplicationState> inventory_replication_;
  HashMap<SessionID, SkinTransferSessionState> skin_transfers_;
  HashMap<SessionID, ChatRateLimitState> chat_rate_limits_;
  HashMap<std::string, SessionID> online_profile_sessions_;
  ChatValidationConfig chat_validation_{};
  arv::text::ChatFormatter chat_formatter_{};
  const arv::text::LanguageCatalog* language_{};
  plugin::PluginManager* plugins_{};
  protocol::ChatMessageId next_chat_message_id_{1U};
  u32 active_skin_uploads_global_{};
  u64 skin_storage_bytes_{};
  ServerWorldJobRuntime world_jobs_;
  i32 simulation_radius_{1};
  bool keep_alive_enabled_{true};
};

[[nodiscard]] Vec3f player_look_direction(const player::PlayerState& player_state) noexcept;
[[nodiscard]] ChunkSectionCoord player_chunk_coord(const player::PlayerState& player_state) noexcept;
[[nodiscard]] Vec3f player_eye_position(
    const player::PlayerState& player_state,
    const player::PlayerTuning& tuning) noexcept;
[[nodiscard]] net::NetworkResult send_protocol_message(
    net::ITransport& transport,
    net::ConnectionID connection_id,
    protocol::MessageType message_type,
    protocol::ReliabilityClass reliability,
    protocol::NetworkLane lane,
    std::vector<u8> payload);

void log_send_failure_if_dedicated(
    ServerMode mode,
    net::ConnectionID connection_id,
    const net::NetworkResult& send_result);

} // namespace arv::server::detail
