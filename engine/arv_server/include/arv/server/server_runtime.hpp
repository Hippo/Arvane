#pragma once

#include "arv/core/time.hpp"
#include "arv/net/transport.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/player/player_state.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/sched/scheduler.hpp"
#include "arv/server/chunk_streamer.hpp"
#include "arv/server/session_registry.hpp"
#include "arv/server/world_registry.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/world/world_runtime.hpp"

#include <optional>
#include <memory>
#include <string_view>
#include <unordered_map>
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

  [[nodiscard]] const world::WorldRuntime& voxel_world() const noexcept {
    return default_world_;
  }

  [[nodiscard]] world::WorldRuntime& voxel_world() noexcept {
    return default_world_;
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

  [[nodiscard]] bool set_player_game_mode(PlayerID player_id, std::string_view mode_id);

private:
  void handle_network_event(net::NetworkEvent event);

  void handle_message(net::ConnectionID connection_id, const std::vector<u8>& packet);
  void handle_input_batch(net::ConnectionID connection_id, const protocol::ClientInputBatch& batch);
  void handle_chunk_interest_update(
      net::ConnectionID connection_id,
      const protocol::ChunkInterestUpdate& interest);
  void tick_players();
  void tick_chunk_streaming();
  void drain_generation_jobs(bool drain_all);
  void schedule_generation_jobs();
  void drain_lighting_jobs(bool drain_all);
  void schedule_lighting_jobs();
  void refresh_player_permissions(player::PlayerState& player_state);
  void apply_player_command(player::PlayerState& player_state, const player::PlayerCommand& command);
  void send_player_corrections();
  void seed_player_inventory(player::PlayerState& player_state);
  [[nodiscard]] bool inventory_is_empty(const player::Inventory& inventory) const noexcept;

  std::unique_ptr<net::ITransport> transport_;
  ServerRuntimeState state_{ServerRuntimeState::Stopped};
  ServerMode mode_{ServerMode::Integrated};
  TickRate base_tps_{DefaultBaseTps};
  TickIndex tick_index_{};
  protocol::HandshakePolicy handshake_policy_{};
  SessionRegistry sessions_;
  WorldRegistry worlds_;
  world::WorldRuntime default_world_{DefaultWorldID};
  std::vector<net::NetworkEvent> inbound_events_;
  const voxel::BlockRegistry* block_registry_{};
  const player::ItemRegistry* item_registry_{};
  player::PlayerTuning player_tuning_{};
  voxel::FluidDefinition fluid_definition_{voxel::make_water_fluid_definition()};
  ServerChunkStreamer chunk_streamer_{};
  std::optional<ServerChunkGenerationSettings> chunk_generation_{};
  std::vector<QueuedChunkStreamMessage> queued_chunk_stream_messages_;
  player::GameModeRegistry game_modes_{player::GameModeRegistry::with_builtins()};
  player::PermissionGroupRegistry permission_groups_{};
  std::unordered_map<PlayerID, player::PlayerCommand> pending_player_commands_;
  std::unordered_map<PlayerID, protocol::PlayerCorrection> last_sent_player_corrections_;

  struct PendingLightingJob {
    u64 source_revision{};
    sched::JobHandle<world::LightingResult> job;
  };

  struct PendingGenerationJob {
    ChunkCoord coord{};
    sched::JobHandle<world::GeneratedChunkResult> job;
  };

  sched::Scheduler world_jobs_;
  std::vector<PendingGenerationJob> pending_generation_jobs_;
  std::vector<PendingLightingJob> pending_lighting_jobs_;
  u32 max_pending_generation_jobs_{4};
  u32 max_pending_lighting_jobs_{2};
  u32 max_generation_results_per_tick_{2};
  u32 max_lighting_results_per_tick_{1};
};

} // namespace arv::server
