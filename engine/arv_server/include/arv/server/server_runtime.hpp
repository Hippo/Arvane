#pragma once

#include "arv/command/command.hpp"
#include "arv/command/command_registry.hpp"
#include "arv/core/types.hpp"
#include "arv/core/time.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/entity/components.hpp"
#include "arv/io/config.hpp"
#include "arv/net/transport.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/player/player_state.hpp"
#include "arv/sched/scheduler.hpp"
#include "arv/server/chunk_streamer.hpp"
#include "arv/server/session_registry.hpp"
#include "arv/server/world_registry.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/world/world_runtime.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::server {

namespace detail {
struct ServerRuntimeImpl;
}

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
  crypto::PlayerProfileID profile_id{};
  std::string profile_name{"local_player"};
  std::string display_name{"local_player"};
  std::string world_name{"default"};
  Vec3f position{};
  Vec3f velocity{};
  f32 yaw_radians{};
  f32 pitch_radians{};
  bool grounded{};
  player::MovementMode movement_mode{player::MovementMode::Walking};
  std::string game_mode_id{std::string{player::SurvivalGameModeID}};
  std::string selected_skin_hash{};
  bool selected_skin_slim_model{};
  player::PlayerPermissions permission_overrides{};
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
  std::optional<world::WorldHeightSettings> world_height{};
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
  std::filesystem::path server_save_root{};
  std::span<const ServerPlayerSaveState> saved_players{};
  BlockBreakingConfig block_breaking{};
  DroppedItemsConfig dropped_items{};
  io::SkinConfig skins{};
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
  ~ServerRuntime();

  ServerRuntime(ServerRuntime&&) noexcept;
  ServerRuntime& operator=(ServerRuntime&&) noexcept;

  ServerRuntime(const ServerRuntime&) = delete;
  ServerRuntime& operator=(const ServerRuntime&) = delete;

  net::NetworkResult start(const ServerStartInfo& start_info);

  void tick();

  void shutdown();

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
  [[nodiscard]] command::CommandResult execute_command(
      command::CommandSender sender,
      std::string_view command_text);
  [[nodiscard]] std::optional<command::CommandSender> command_sender_for_session(
      SessionID session_id) const;

private:
  std::unique_ptr<detail::ServerRuntimeImpl> impl_;
};

} // namespace arv::server
