#pragma once

#include "chunk_view.hpp"

#include "arv/client/client_runtime.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/core/perf_stats.hpp"
#include "arv/core/types.hpp"
#include "arv/entity/components.hpp"
#include "arv/player/item.hpp"
#include "arv/render/render_commands.hpp"
#include "arv/sched/job_system.hpp"
#include "arv/server/server_runtime.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace arv::desktop {

struct DebugMenuState {
  static constexpr usize ProfileFrameHistoryCapacity = 240;

  struct QueueProfileState {
    u64 previous_submitted_jobs{};
    u64 previous_completed_jobs{};
    u32 current_queued_jobs{};
    u32 peak_queued_jobs_window{};
    u32 submitted_jobs_this_frame{};
    u32 completed_jobs_this_frame{};
    u32 active_workers{};
    u32 idle_workers{};
    double idle_percent{};
    bool backlog_growing{};
    u32 queued_history_index{};
    u32 queued_history_count{};
    std::array<u32, ProfileFrameHistoryCapacity> queued_history{};
  };

  bool open{};
  std::string item_filter{};
  std::string selected_item_name{};
  int stack_size{1};
  bool replace_selected_hotbar_slot{};
  std::string selected_item_animation_name{std::string{client::DefaultHeldItemSwingAnimationName}};
  std::string item_animation_export_text{};
  std::string item_animation_status{};
  std::string entity_filter{};
  std::string selected_entity_type_name{};
  std::string selected_entity_id{};
  float entity_spawn_offset[3]{0.0F, 0.0F, 2.0F};
  float entity_spawn_position[3]{0.0F, 20.0F, 0.0F};
  std::string entity_persistent_uuid{};
  bool entity_spawn_at_player{true};
  bool entity_persistent{true};
  bool entity_replicated{true};
  bool show_chunk_boundaries{};
  bool show_collision_bounds{};
  bool show_lighting_view{};
  std::string console_command{};
  std::vector<std::string> console_log{};
  bool profile_sampling_paused{};
  int profile_frame_window_size{static_cast<int>(ProfileFrameHistoryCapacity)};
  int profile_max_event_rows{12};
  u32 profile_frame_sample_index{};
  u32 profile_frame_sample_count{};
  std::array<f32, ProfileFrameHistoryCapacity> profile_frame_delta_ms{};
  std::array<f32, ProfileFrameHistoryCapacity> profile_frame_work_ms{};
  QueueProfileState server_queue_profile{};
  QueueProfileState client_queue_profile{};
  bool current_biome_label_valid{};
  i32 current_biome_world_x{};
  i32 current_biome_world_z{};
  u64 current_biome_seed{};
  const void* current_biome_dimension{};
  std::string current_biome_label{};
  bool nearest_biome_label_valid{};
  i32 nearest_biome_chunk_x{};
  i32 nearest_biome_chunk_z{};
  u64 nearest_biome_seed{};
  const void* nearest_biome_dimension{};
  usize nearest_biome_source_index{};
  std::string nearest_biome_label{};
  u32 snapshot_created_this_frame{};
  u32 snapshot_reused_this_frame{};
  u32 snapshot_copies_this_frame{};
  u64 snapshot_bytes_copied_this_frame{};
  u64 peak_snapshot_bytes_copied_window{};
  u64 previous_snapshots_created_total{};
  u64 previous_snapshots_reused_total{};
  u64 previous_snapshot_copies_total{};
  u64 previous_snapshot_bytes_copied_total{};
  u32 snapshot_bytes_history_index{};
  u32 snapshot_bytes_history_count{};
  std::array<u64, ProfileFrameHistoryCapacity> snapshot_bytes_history{};
};

struct DebugMenuFrameInfo {
  u64 frame_index{};
  double frame_delta_ms{};
  double frame_work_ms{};
  u32 displayed_fps{};
  usize visible_chunk_count{};
  usize pending_client_chunk_count{};
  usize pending_client_unload_count{};
};

struct DebugMenuResult {
  bool inventory_changed{};
};

[[nodiscard]] DebugMenuResult render_debug_menu(
    DebugMenuState& state,
    const DebugMenuFrameInfo& frame_info,
    server::ServerRuntime& server,
    client::ClientRuntime& client,
    const player::ItemRegistry& item_registry,
    client::ItemAnimationLibrary& item_animation_library,
    client::HeldItemAnimationPlayer& held_item_animation_player,
    Seconds now_seconds,
    const std::filesystem::path& item_animation_export_path,
    const entity::EntityRegistry& entity_registry,
    sched::JobSystem& world_jobs,
    render::RenderCommandQueue& renderer,
    const ChunkViewState& chunk_view);

} // namespace arv::desktop
