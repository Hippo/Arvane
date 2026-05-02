#pragma once

#include "arv/server/server_runtime.hpp"

#include "arv/physics/collision.hpp"
#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/inventory_sync.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/world_join.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_lighting.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::server::detail {

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

struct PendingLightingJob {
  u64 source_revision{};
  sched::JobHandle<world::LightingResult> job;
};

struct PendingFluidJob {
  sched::JobHandle<world::FluidSimulationResult> job;
};

struct PendingGenerationJob {
  ChunkCoord coord{};
  sched::JobHandle<world::GeneratedChunkResult> job;
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

  [[nodiscard]] sched::Scheduler* scheduler() noexcept;
  [[nodiscard]] const sched::Scheduler* scheduler() const noexcept;
  [[nodiscard]] usize pending_generation_job_count() const noexcept;
  [[nodiscard]] usize pending_lighting_job_count() const noexcept;
  [[nodiscard]] usize pending_fluid_job_count() const noexcept;

private:
  std::unique_ptr<sched::Scheduler> scheduler_;
  std::vector<PendingGenerationJob> pending_generation_jobs_;
  std::vector<PendingLightingJob> pending_lighting_jobs_;
  std::vector<PendingFluidJob> pending_fluid_jobs_;
  u32 max_pending_generation_jobs_{4};
  u32 max_pending_lighting_jobs_{2};
  u32 max_pending_fluid_jobs_{1};
  u32 max_generation_results_per_tick_{2};
  u32 max_lighting_results_per_tick_{1};
  u32 max_fluid_results_per_tick_{1};
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
  [[nodiscard]] const std::vector<ServerPlayerSaveState>& states() const noexcept;

private:
  std::vector<ServerPlayerSaveState> states_;
};

struct ServerRuntimeImpl {
  explicit ServerRuntimeImpl(std::unique_ptr<net::ITransport> transport);

  net::NetworkResult start(const ServerStartInfo& start_info);
  void tick();
  void shutdown();

  [[nodiscard]] ServerRuntimeState state() const noexcept;
  [[nodiscard]] TickIndex tick_index() const noexcept;
  [[nodiscard]] TickRate base_tps() const noexcept;
  [[nodiscard]] ServerMode mode() const noexcept;
  [[nodiscard]] const SessionRegistry& sessions() const noexcept;
  [[nodiscard]] const WorldRegistry& worlds() const noexcept;
  [[nodiscard]] std::vector<DroppedItemStack> dropped_item_stacks() const;
  [[nodiscard]] const std::vector<ServerPlayerSaveState>& saved_player_states() const noexcept;
  [[nodiscard]] const world::WorldRuntime& voxel_world() const noexcept;
  [[nodiscard]] world::WorldRuntime& voxel_world() noexcept;
  [[nodiscard]] const entity::EntityRegistry* entity_registry() const noexcept;
  [[nodiscard]] const player::GameModeRegistry& game_modes() const noexcept;
  [[nodiscard]] player::GameModeRegistry& game_modes() noexcept;
  [[nodiscard]] const player::PermissionGroupRegistry& permission_groups() const noexcept;
  [[nodiscard]] player::PermissionGroupRegistry& permission_groups() noexcept;
  [[nodiscard]] const sched::Scheduler* world_scheduler() const noexcept;
  [[nodiscard]] sched::Scheduler* world_scheduler() noexcept;
  [[nodiscard]] usize pending_generation_job_count() const noexcept;
  [[nodiscard]] usize pending_lighting_job_count() const noexcept;
  [[nodiscard]] usize pending_fluid_job_count() const noexcept;

  [[nodiscard]] bool set_player_game_mode(PlayerID player_id, std::string_view mode_id);
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

  void handle_network_event(net::NetworkEvent event);
  void handle_connection_lost(net::ConnectionID connection_id);
  void handle_message(net::ConnectionID connection_id, const std::vector<u8>& packet);
  void handle_client_hello(net::ConnectionID connection_id, const protocol::ClientHello& client_hello);
  void handle_input_batch(net::ConnectionID connection_id, const protocol::ClientInputBatch& batch);
  void handle_chunk_interest_update(
      net::ConnectionID connection_id,
      const protocol::ChunkInterestUpdate& interest);
  void handle_inventory_action_request(
      net::ConnectionID connection_id,
      const protocol::InventoryActionRequest& request);

  void tick_players();
  void update_world_simulation_activity();
  void refresh_player_permissions(player::PlayerState& player_state);
  void apply_player_command(player::PlayerState& player_state, const player::PlayerCommand& command);

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

  void tick_chunk_streaming();

  void send_player_corrections();
  void send_world_join_result(const ServerSession& session, const player::PlayerState& player_state);
  void send_block_updates();
  void send_entity_replication();
  void send_dropped_item_replication();
  void send_inventory_updates();
  [[nodiscard]] protocol::InventoryUpdate make_inventory_update(
      const player::PlayerState& player_state,
      u32 revision) const;

  bool apply_saved_player_state(const ServerSession& session, player::PlayerState& player_state);
  void record_player_save_state(const ServerSession& session, const player::PlayerState& player_state);
  void seed_player_inventory(player::PlayerState& player_state);
  [[nodiscard]] bool inventory_is_empty(const player::Inventory& inventory) const noexcept;

  std::unique_ptr<net::ITransport> transport_;
  ServerRuntimeState state_{ServerRuntimeState::Stopped};
  ServerMode mode_{ServerMode::Integrated};
  TickRate base_tps_{DefaultBaseTps};
  TickIndex tick_index_{};
  i32 max_view_distance_{};
  std::string world_name_{"default"};
  protocol::HandshakePolicy handshake_policy_{};
  SessionRegistry sessions_;
  WorldRegistry worlds_;
  world::WorldRuntime default_world_{DefaultWorldID};
  std::vector<net::NetworkEvent> inbound_events_;
  const voxel::BlockRegistry* block_registry_{};
  const player::ItemRegistry* item_registry_{};
  const entity::EntityRegistry* entity_registry_{};
  player::PlayerTuning player_tuning_{};
  BlockBreakingConfig block_breaking_{};
  DroppedItemsConfig dropped_items_{};
  voxel::FluidDefinition fluid_definition_{voxel::make_water_fluid_definition()};
  ServerChunkStreamer chunk_streamer_{};
  std::optional<ServerChunkGenerationSettings> chunk_generation_{};
  std::vector<QueuedChunkStreamMessage> queued_chunk_stream_messages_;
  player::GameModeRegistry game_modes_{player::GameModeRegistry::with_builtins()};
  player::PermissionGroupRegistry permission_groups_{};
  HashMap<PlayerID, player::PlayerCommand> pending_player_commands_;
  HashMap<PlayerID, protocol::PlayerCorrection> last_sent_player_corrections_;
  HashMap<PlayerID, BlockBreakAttempt> block_break_attempts_;
  HashSet<SessionID> logged_chunk_interest_sessions_;
  HashSet<SessionID> logged_chunk_delivery_sessions_;
  ServerPlayerSaveCache player_saves_;
  HashMap<SessionID, EntityReplicationState> entity_replication_;
  HashMap<PlayerID, InventoryReplicationState> inventory_replication_;
  ServerWorldJobRuntime world_jobs_;
  i32 simulation_radius_{1};
};

[[nodiscard]] Vec3f player_look_direction(const player::PlayerState& player_state) noexcept;
[[nodiscard]] ChunkCoord player_chunk_coord(const player::PlayerState& player_state) noexcept;
[[nodiscard]] Vec3f player_eye_position(
    const player::PlayerState& player_state,
    const player::PlayerTuning& tuning) noexcept;
void step_player_without_collision(
    player::PlayerState& player_state,
    const player::PlayerCommand& command,
    const player::PlayerTuning& tuning,
    Seconds tick_seconds) noexcept;

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
