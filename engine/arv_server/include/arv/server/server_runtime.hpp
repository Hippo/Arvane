#pragma once

#include "arv/core/types.hpp"
#include "arv/core/time.hpp"
#include "arv/entity/components.hpp"
#include "arv/net/transport.hpp"
#include "arv/physics/collision.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/player/player_state.hpp"
#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/inventory_sync.hpp"
#include "arv/protocol/world_join.hpp"
#include "arv/sched/scheduler.hpp"
#include "arv/server/chunk_streamer.hpp"
#include "arv/server/session_registry.hpp"
#include "arv/server/world_registry.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/world/world_runtime.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::server {

enum class ServerMode {
  Integrated,
  Dedicated,
};

enum class ServerRuntimeState {
  Stopped,
  Running,
  Stopping,
};

struct ServerPlayerSaveState {
  std::string profile_name{"local_player"};
  std::string world_name{"default"};
  Vec3f position{};
  Vec3f velocity{};
  f32 yaw_radians{};
  f32 pitch_radians{};
  bool grounded{};
  player::MovementMode movement_mode{player::MovementMode::Walking};
  std::string game_mode_id{std::string{player::SurvivalGameModeID}};
  u8 selected_hotbar_slot{};
  player::Inventory inventory{player::DefaultPlayerInventorySlotCount};
};

struct BlockBreakingConfig {
  f32 base_seconds_per_hardness{1.0F};
  f32 creative_inventory_multiplier{0.0F};
  f32 hand_speed_multiplier{0.2F};
  f32 wrong_tool_speed_multiplier{0.2F};
};

struct DroppedItemsConfig {
  f32 pickup_radius{1.25F};
  f32 pickup_delay_seconds{0.5F};
};

struct ServerStartInfo {
  ServerMode mode{ServerMode::Integrated};
  TickRate base_tps{DefaultBaseTps};
  net::ListenAddress listen_address{};
  const voxel::BlockRegistry* block_registry{};
  const player::ItemRegistry* item_registry{};
  player::PlayerTuning player_tuning{};
  voxel::FluidDefinition fluid_definition{voxel::make_water_fluid_definition()};
  ServerChunkStreamingConfig chunk_streaming{};
  std::optional<ServerChunkGenerationSettings> chunk_generation{};
  u32 max_generation_results_per_tick{2};
  u32 max_lighting_results_per_tick{1};
  u32 max_pending_generation_jobs{4};
  sched::WorkerThreadConfig worker_threads{};
  world::WorldTickPolicy world_tick_policy{};
  u32 max_fluid_results_per_tick{1};
  u32 max_pending_fluid_jobs{1};
  const entity::EntityRegistry* entity_registry{};
  TickIndex initial_tick_index{};
  std::string world_name{"default"};
  std::span<const ServerPlayerSaveState> saved_players{};
  BlockBreakingConfig block_breaking{};
  DroppedItemsConfig dropped_items{};
};

struct DroppedItemStack {
  EntityNetID drop_id{};
  WorldID world_id{};
  Vec3f position{};
  Vec3f velocity{};
  player::ItemStack stack{};
};

class ServerRuntime {
public:
  explicit ServerRuntime(std::unique_ptr<net::ITransport> transport);

  net::NetworkResult start(const ServerStartInfo& start_info);

  void tick();

  void shutdown();

  [[nodiscard]] ServerRuntimeState state() const noexcept {
    return state_;
  }

  [[nodiscard]] TickIndex tick_index() const noexcept {
    return tick_index_;
  }

  [[nodiscard]] TickRate base_tps() const noexcept {
    return base_tps_;
  }

  [[nodiscard]] ServerMode mode() const noexcept {
    return mode_;
  }

  [[nodiscard]] const SessionRegistry& sessions() const noexcept {
    return sessions_;
  }

  [[nodiscard]] const WorldRegistry& worlds() const noexcept {
    return worlds_;
  }

  [[nodiscard]] std::vector<DroppedItemStack> dropped_item_stacks() const;

  [[nodiscard]] const std::vector<ServerPlayerSaveState>& saved_player_states() const noexcept {
    return saved_player_states_;
  }

  [[nodiscard]] const world::WorldRuntime& voxel_world() const noexcept {
    return default_world_;
  }

  [[nodiscard]] world::WorldRuntime& voxel_world() noexcept {
    return default_world_;
  }

  [[nodiscard]] const entity::EntityRegistry* entity_registry() const noexcept {
    return entity_registry_;
  }

  [[nodiscard]] const player::GameModeRegistry& game_modes() const noexcept {
    return game_modes_;
  }

  [[nodiscard]] player::GameModeRegistry& game_modes() noexcept {
    return game_modes_;
  }

  [[nodiscard]] const player::PermissionGroupRegistry& permission_groups() const noexcept {
    return permission_groups_;
  }

  [[nodiscard]] player::PermissionGroupRegistry& permission_groups() noexcept {
    return permission_groups_;
  }

  [[nodiscard]] const sched::Scheduler* world_scheduler() const noexcept {
    return world_jobs_.get();
  }

  [[nodiscard]] sched::Scheduler* world_scheduler() noexcept {
    return world_jobs_.get();
  }

  [[nodiscard]] usize pending_generation_job_count() const noexcept {
    return pending_generation_jobs_.size();
  }

  [[nodiscard]] usize pending_lighting_job_count() const noexcept {
    return pending_lighting_jobs_.size();
  }

  [[nodiscard]] usize pending_fluid_job_count() const noexcept {
    return pending_fluid_jobs_.size();
  }

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

private:
  void handle_network_event(net::NetworkEvent event);

  void handle_message(net::ConnectionID connection_id, const std::vector<u8>& packet);
  void handle_input_batch(net::ConnectionID connection_id, const protocol::ClientInputBatch& batch);
  void handle_chunk_interest_update(
      net::ConnectionID connection_id,
      const protocol::ChunkInterestUpdate& interest);
  void handle_inventory_action_request(
      net::ConnectionID connection_id,
      const protocol::InventoryActionRequest& request);
  void tick_players();
  void tick_dropped_item_pickups(Seconds tick_seconds);
  void update_world_simulation_activity();
  void tick_chunk_streaming();
  void drain_generation_jobs(bool drain_all);
  void schedule_generation_jobs();
  void drain_lighting_jobs(bool drain_all);
  void schedule_lighting_jobs();
  void drain_fluid_jobs(bool drain_all);
  void schedule_fluid_jobs();
  void refresh_player_permissions(player::PlayerState& player_state);
  void apply_player_command(player::PlayerState& player_state, const player::PlayerCommand& command);
  void clear_block_break_attempt(PlayerID player_id);
  void tick_block_break_attempt(
      player::PlayerState& player_state,
      const player::PlayerCommand& command,
      const physics::ICollisionQuery& interaction_query,
      Vec3f eye_position,
      Vec3f look_direction,
      Seconds tick_seconds);
  void send_player_corrections();
  void send_world_join_result(const ServerSession& session, const player::PlayerState& player_state);
  void send_block_updates();
  void send_entity_replication();
  void send_dropped_item_replication();
  void send_inventory_updates();
  void spawn_dropped_item_stack(WorldID world_id, Vec3f position, player::ItemStack stack);
  bool apply_saved_player_state(const ServerSession& session, player::PlayerState& player_state);
  void record_player_save_state(const ServerSession& session, const player::PlayerState& player_state);
  [[nodiscard]] protocol::InventoryUpdate make_inventory_update(
      const player::PlayerState& player_state,
      u32 revision) const;
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

  struct BlockBreakAttempt {
    BlockPos block{};
    voxel::BlockVariantID variant{};
    player::ItemID held_item{};
    f32 elapsed_seconds{};
    f32 required_seconds{};
    f32 progress{};
    u8 stage{};
  };

  HashMap<PlayerID, BlockBreakAttempt> block_break_attempts_;
  HashSet<SessionID> logged_chunk_interest_sessions_;
  HashSet<SessionID> logged_chunk_delivery_sessions_;
  std::vector<ServerPlayerSaveState> saved_player_states_;

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

  HashMap<SessionID, EntityReplicationState> entity_replication_;
  HashMap<PlayerID, InventoryReplicationState> inventory_replication_;

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

  std::unique_ptr<sched::Scheduler> world_jobs_;
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
  i32 simulation_radius_{1};
};

} // namespace arv::server
