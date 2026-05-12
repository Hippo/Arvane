#pragma once

#include "chunk_view.hpp"

#include "arv/asset/texture_atlas.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/core/fixed_timestep.hpp"
#include "arv/core/types.hpp"
#include "arv/entity/components.hpp"
#include "arv/io/config.hpp"
#include "arv/net/gns_transport.hpp"
#include "arv/player/item.hpp"
#include "arv/player/permissions.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/render/render_commands.hpp"
#include "arv/render/render_types.hpp"
#include "arv/sched/job_system.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/ui/ui.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"

#include <chrono>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace arv::lang {
class LanguageCatalog;
} // namespace arv::lang

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
  client::HeldItemAnimationPlayer held_item_animation{};
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
  std::vector<server::StarterInventoryItem> starter_inventory{};
  server::FluidContainerInteractionConfig fluid_containers{};
  const entity::EntityRegistry& entity_registry;
  const asset::BlockTextureAtlas& block_atlas;
  const asset::TextureAtlas& item_atlas;
  const player::PlayerTuning& player_tuning;
  const voxel::FluidDefinition& water_fluid;
  const world::WorldTickPolicy& world_tick_policy;
  const world::WorldGenerationConfig& default_generation_config;
  const sched::RuntimeThreadBudgetPlan& thread_budget;
  const lang::LanguageCatalog& language;
  std::vector<asset::RequiredPack> required_packs{};
};

class DesktopSessionManager {
public:
  explicit DesktopSessionManager(DesktopSessionDependencies dependencies);
  ~DesktopSessionManager();

  DesktopSessionManager(const DesktopSessionManager&) = delete;
  DesktopSessionManager& operator=(const DesktopSessionManager&) = delete;

  [[nodiscard]] DesktopGameSession* active_session() noexcept;
  [[nodiscard]] const DesktopGameSession* active_session() const noexcept;

  [[nodiscard]] bool save_and_shutdown();

  [[nodiscard]] bool start_singleplayer(
      std::string server_save_name,
      std::string world_name,
      std::optional<u64> create_seed,
      bool create_new,
      std::string save_display_name,
      const client::LocalPlayerIdentity& local_identity);

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
  void remove_session_chunk_meshes(DesktopGameSession& session) const;

  DesktopSessionDependencies dependencies_;
  std::unique_ptr<DesktopGameSession> game_session_{};
  std::unique_ptr<net::GameNetworkingSocketsEnvironment> gns_environment_{};
};

} // namespace arv::desktop
