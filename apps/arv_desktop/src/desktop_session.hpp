#pragma once

#include "chunk_view.hpp"

#include "arv/asset/texture_atlas.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/core/fixed_timestep.hpp"
#include "arv/core/types.hpp"
#include "arv/entity/components.hpp"
#include "arv/io/config.hpp"
#include "arv/net/gns_transport.hpp"
#include "arv/player/item.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/render/render_types.hpp"
#include "arv/sched/scheduler.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/ui/ui.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"

#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace arv::render {
class OpenGLRenderer;
} // namespace arv::render

namespace arv::desktop {

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
  world::WorldRuntime client_world{server::DefaultWorldID};
  ChunkViewState chunk_view{};
  std::deque<protocol::ChunkUnload> pending_client_chunk_unloads{};
  std::deque<protocol::ChunkData> pending_client_chunk_data{};
  std::deque<protocol::BlockUpdateBatch> pending_client_block_updates{};
  core::FixedTimestepAccumulator ticks;
  TickIndex client_tick_index{};
  std::vector<ui::UiItemStackView> inventory_slots{};
  std::optional<ui::UiItemStackView> carried_inventory_stack{};
  std::vector<ui::PlayerListEntryView> tab_players{};
  std::vector<ui::ChatLineView> chat_lines{};
  bool ui_snapshot_initialized{};
};

struct DesktopSessionDependencies {
  const io::EngineConfig& config;
  render::OpenGLRenderer& renderer;
  sched::Scheduler& world_jobs;
  const voxel::BlockRegistry& block_registry;
  u64 block_registry_hash{};
  const player::ItemRegistry& item_registry;
  u64 item_registry_hash{};
  const entity::EntityRegistry& entity_registry;
  const asset::BlockTextureAtlas& block_atlas;
  const asset::TextureAtlas& item_atlas;
  const player::PlayerTuning& player_tuning;
  const voxel::FluidDefinition& water_fluid;
  const server::ServerChunkStreamingConfig& chunk_streaming;
  const world::WorldTickPolicy& world_tick_policy;
  const world::WorldGenerationConfig& default_generation_config;
  const sched::RuntimeThreadBudgetPlan& thread_budget;
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
      std::string save_display_name);

  [[nodiscard]] bool start_remote(const ui::JoinServerCommand& command);

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
