#pragma once

#include "arv/client/client_runtime.hpp"
#include "arv/core/types.hpp"
#include "arv/player/item.hpp"
#include "arv/render/opengl_renderer.hpp"
#include "arv/sched/scheduler.hpp"
#include "arv/server/server_runtime.hpp"

#include <array>
#include <string>
#include <vector>

namespace arv::desktop {

struct DebugMenuState {
  static constexpr usize ProfileFrameHistoryCapacity = 240;

  bool open{};
  std::string item_filter{};
  std::string selected_item_name{};
  int stack_size{1};
  bool replace_selected_hotbar_slot{};
  bool show_chunk_boundaries{};
  bool show_collision_bounds{};
  bool show_lighting_view{};
  std::string console_command{};
  std::vector<std::string> console_log{};
  bool profile_sampling_paused{};
  u32 profile_frame_sample_index{};
  u32 profile_frame_sample_count{};
  std::array<f32, ProfileFrameHistoryCapacity> profile_frame_delta_ms{};
  std::array<f32, ProfileFrameHistoryCapacity> profile_frame_work_ms{};
};

struct DebugMenuFrameInfo {
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
    const sched::Scheduler& world_jobs,
    const render::OpenGLRenderer& renderer);

} // namespace arv::desktop
