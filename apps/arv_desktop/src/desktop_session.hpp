#pragma once

#include "chunk_view.hpp"

#include "arv/asset/texture_atlas.hpp"
#include "arv/base/ui.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/client/particles.hpp"
#include "arv/command/command_registry.hpp"
#include "arv/core/fixed_timestep.hpp"
#include "arv/core/types.hpp"
#include "arv/entity/components.hpp"
#include "arv/io/config.hpp"
#include "arv/net/gns_transport.hpp"
#include "arv/player/item.hpp"
#include "arv/player/permissions.hpp"
#include "arv/plugin/plugin_manager.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/render/chunk_mesher.hpp"
#include "arv/render/render_commands.hpp"
#include "arv/render/render_types.hpp"
#include "arv/sched/job_system.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"

#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace arv::text {
class LanguageCatalog;
} // namespace arv::text

namespace arv::desktop {

struct BlockOutlineTarget {
  BlockPos block{};
  voxel::BlockVariantID variant{};
};

struct DesktopGameSession {
  explicit DesktopGameSession(TickRate base_tps);

  std::string server_save_name{};
  std::string server_save_display_name{};
  std::filesystem::path server_save_root{};
  std::string world_save_name{"default"};
  u64 world_seed{1};
  world::WorldGenerationConfig generation_config{};
  i32 min_chunk_y{};
  i32 max_chunk_y{};
  std::unique_ptr<server::ServerRuntime> integrated_server{};
  std::unique_ptr<client::ClientRuntime> client{};
  std::optional<ui::JoinServerCommand> remote_server{};
  world::WorldRuntime client_world{server::DefaultWorldID};
  ChunkViewState chunk_view{};
  std::deque<protocol::ClientboundChunkSectionUnload> pending_client_chunk_unloads{};
  std::deque<protocol::ClientboundChunkSectionData> pending_client_chunk_data{};
  std::deque<protocol::ClientboundBlockUpdateBatch> pending_client_block_updates{};
  std::optional<BlockOutlineTarget> block_outline_target{};
  core::FixedTimestepAccumulator ticks;
  TickIndex client_tick_index{};
  std::vector<ui::UiItemStackView> inventory_slots{};
  std::optional<ui::UiItemStackView> carried_inventory_stack{};
  std::vector<ui::PlayerListEntryView> tab_players{};
  std::vector<ui::ChatLineView> chat_lines{};
  usize chat_history_scroll_offset{};
  client::HeldItemAnimationPlayer held_item_animation{};
  client::InputFrame last_gameplay_input{};
  Seconds next_block_mining_particle_seconds{};
  Seconds sprint_particle_accumulator{};
  u32 particle_seed_counter{1U};
  std::optional<std::chrono::steady_clock::time_point> first_person_sneak_visual_updated_at{};
  f32 first_person_sneak_visual_progress{};
  bool held_item_secondary_use_active{};
  bool ui_snapshot_initialized{};
};

struct DesktopSessionDependencies {
  const io::EngineConfig& config;
  render::RenderCommandQueue& renderer;
  sched::JobSystem& world_jobs;
  const voxel::BlockRegistry& block_registry;
  u64 block_registry_hash{};
  const player::ItemRegistry& item_registry;
  u64 item_registry_hash{};
  const player::GameModeRegistry& game_modes;
  const server::BlockBreakBehaviorRegistry& block_break_behaviors;
  std::vector<server::StarterInventoryItem> starter_inventory{};
  server::FluidContainerInteractionConfig fluid_containers{};
  std::function<void(command::CommandRegistry&)> register_commands{};
  const entity::EntityRegistry& entity_registry;
  const entity::VehicleDefinitionRegistry& vehicle_registry;
  const world::EntityGoalRegistry& entity_goals;
  const asset::BlockTextureAtlas& block_atlas;
  const render::FluidRenderData& fluid_render_data;
  const asset::TextureAtlas& item_atlas;
  const client::ParticleRegistry& particle_registry;
  const player::PlayerTuning& player_tuning;
  const voxel::FluidDefinition& water_fluid;
  const world::WorldTickPolicy& world_tick_policy;
  const world::WorldGenerationConfig& default_generation_config;
  const sched::RuntimeThreadBudgetPlan& thread_budget;
  const arv::text::LanguageCatalog& language;
  std::vector<asset::RequiredPack> required_packs{};
  plugin::PluginManager* plugins{};
};

class DesktopSessionManager {
public:
  explicit DesktopSessionManager(DesktopSessionDependencies dependencies);
  ~DesktopSessionManager();

  DesktopSessionManager(const DesktopSessionManager&) = delete;
  DesktopSessionManager& operator=(const DesktopSessionManager&) = delete;

  [[nodiscard]] DesktopGameSession* active_session() noexcept;
  [[nodiscard]] const DesktopGameSession* active_session() const noexcept;

  [[nodiscard]] bool shutdown_pending() const noexcept;
  [[nodiscard]] bool shutdown_failed() const noexcept;
  [[nodiscard]] bool start_pending() const noexcept;
  void poll_shutdown();
  [[nodiscard]] std::optional<bool> poll_start();
  [[nodiscard]] bool begin_save_and_shutdown();
  [[nodiscard]] bool save_and_shutdown();

  [[nodiscard]] bool begin_start_singleplayer(
      std::string server_save_name,
      std::string world_name,
      std::optional<u64> create_seed,
      bool create_new,
      std::string save_display_name,
      client::LocalPlayerIdentity local_identity);

  [[nodiscard]] bool start_singleplayer(
      std::string server_save_name,
      std::string world_name,
      std::optional<u64> create_seed,
      bool create_new,
      std::string save_display_name,
      const client::LocalPlayerIdentity& local_identity);

  [[nodiscard]] bool begin_start_remote(
      ui::JoinServerCommand command,
      client::LocalPlayerIdentity local_identity);

  [[nodiscard]] bool start_remote(
      const ui::JoinServerCommand& command,
      const client::LocalPlayerIdentity& local_identity);

  void delete_singleplayer_save(const ui::DeleteSinglePlayerSaveCommand& command);

  void refresh_inventory_slots(DesktopGameSession& session) const;
  void collect_client_chunk_stream(DesktopGameSession& session) const;
  void apply_pending_client_chunk_stream(
      DesktopGameSession& session,
      u32 chunk_budget,
      u32 unload_budget) const;

private:
  struct PendingShutdown;
  struct PendingStart;

  void remove_session_chunk_meshes(DesktopGameSession& session) const;
  [[nodiscard]] bool shutdown_session(std::unique_ptr<DesktopGameSession> session);
  [[nodiscard]] bool finish_pending_shutdown();
  [[nodiscard]] bool finish_pending_start();
  void adopt_started_session(std::unique_ptr<DesktopGameSession> session);
  [[nodiscard]] std::unique_ptr<DesktopGameSession> build_singleplayer_session(
      std::string server_save_name,
      std::string world_name,
      std::optional<u64> create_seed,
      bool create_new,
      std::string save_display_name,
      const client::LocalPlayerIdentity& local_identity);
  [[nodiscard]] std::unique_ptr<DesktopGameSession> build_remote_session(
      const ui::JoinServerCommand& command,
      const client::LocalPlayerIdentity& local_identity);

  DesktopSessionDependencies dependencies_;
  std::unique_ptr<DesktopGameSession> game_session_{};
  std::unique_ptr<PendingShutdown> pending_shutdown_{};
  std::unique_ptr<PendingStart> pending_start_{};
  std::unique_ptr<net::GameNetworkingSocketsEnvironment> gns_environment_{};
  bool shutdown_failed_{};
};

} // namespace arv::desktop
