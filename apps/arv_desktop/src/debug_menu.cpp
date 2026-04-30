#include "debug_menu.hpp"

#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string_view>

#include <imgui.h>
#include <imgui_stdlib.h>

namespace arv::desktop {
namespace {

[[nodiscard]] const server::ServerSession* local_session(
    const server::ServerRuntime& server) noexcept {
  return server.sessions().first();
}

[[nodiscard]] player::PlayerState* local_player_state(server::ServerRuntime& server) noexcept {
  const server::ServerSession* session = local_session(server);
  return session == nullptr ? nullptr : server.voxel_world().find_player(session->player_id);
}

[[nodiscard]] bool contains_case_insensitive(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  auto lower = [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  };
  return std::search(
             haystack.begin(),
             haystack.end(),
             needle.begin(),
             needle.end(),
             [&](char lhs, char rhs) {
               return lower(static_cast<unsigned char>(lhs)) ==
                      lower(static_cast<unsigned char>(rhs));
             }) != haystack.end();
}

[[nodiscard]] std::vector<const player::ItemRecord*> filtered_items(
    const player::ItemRegistry& registry,
    std::string_view filter) {
  std::vector<const player::ItemRecord*> items;
  items.reserve(registry.item_count());
  for (usize index = 0; index < registry.item_count(); ++index) {
    const auto* item = registry.item(player::ItemID{static_cast<u32>(index)});
    if (item == nullptr || item->id == player::AirItemID) {
      continue;
    }
    if (contains_case_insensitive(item->name, filter) ||
        contains_case_insensitive(item->display_name, filter)) {
      items.emplace_back(item);
    }
  }
  return items;
}

[[nodiscard]] const player::ItemRecord* selected_item(
    const player::ItemRegistry& registry,
    std::string_view name) {
  const std::optional<player::ItemID> id = registry.find_item(name);
  return id.has_value() ? registry.item(id.value()) : nullptr;
}

[[nodiscard]] f32 frame_sample(double milliseconds) noexcept {
  return static_cast<f32>(std::clamp(milliseconds, 0.0, 60000.0));
}

void record_profile_frame_sample(DebugMenuState& state, const DebugMenuFrameInfo& frame_info) noexcept {
  if (!core::profiling_enabled() || state.profile_sampling_paused) {
    return;
  }

  constexpr u32 Capacity =
      static_cast<u32>(DebugMenuState::ProfileFrameHistoryCapacity);
  const u32 index = state.profile_frame_sample_index % Capacity;
  state.profile_frame_delta_ms[index] = frame_sample(frame_info.frame_delta_ms);
  state.profile_frame_work_ms[index] = frame_sample(frame_info.frame_work_ms);
  state.profile_frame_sample_index = (index + 1U) % Capacity;
  if (state.profile_frame_sample_count < Capacity) {
    ++state.profile_frame_sample_count;
  }
}

void clear_profile_frame_samples(DebugMenuState& state) noexcept {
  state.profile_frame_delta_ms.fill(0.0F);
  state.profile_frame_work_ms.fill(0.0F);
  state.profile_frame_sample_index = 0;
  state.profile_frame_sample_count = 0;
}

[[nodiscard]] core::TimingPercentiles frame_percentiles(
    const std::array<f32, DebugMenuState::ProfileFrameHistoryCapacity>& samples,
    u32 sample_count) {
  core::TimingPercentiles result;
  result.sample_count = sample_count;
  if (sample_count == 0U) {
    return result;
  }

  std::array<f32, DebugMenuState::ProfileFrameHistoryCapacity> sorted{};
  for (u32 index = 0; index < sample_count; ++index) {
    sorted[index] = samples[index];
  }
  std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(sample_count));

  auto percentile_value = [&](u32 percentile) {
    const u32 rank = static_cast<u32>(
        (static_cast<u64>(sample_count - 1U) * static_cast<u64>(percentile) + 99ULL) /
        100ULL);
    return static_cast<double>(sorted[rank]);
  };

  result.p50_ms = percentile_value(50);
  result.p95_ms = percentile_value(95);
  result.p99_ms = percentile_value(99);
  return result;
}

void render_percentile_header(bool include_max) {
  ImGui::TableSetupColumn("Metric");
  ImGui::TableSetupColumn("Samples");
  ImGui::TableSetupColumn("Latest");
  if (include_max) {
    ImGui::TableSetupColumn("Max");
  }
  ImGui::TableSetupColumn("P50");
  ImGui::TableSetupColumn("P95");
  ImGui::TableSetupColumn("P99");
  ImGui::TableHeadersRow();
}

void render_percentile_cells(
    const char* label,
    const core::TimingPercentiles& percentiles,
    double latest_ms,
    std::optional<double> max_ms) {
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(label);
  ImGui::TableNextColumn();
  ImGui::Text("%u", percentiles.sample_count);
  ImGui::TableNextColumn();
  ImGui::Text("%.3f ms", latest_ms);
  if (max_ms.has_value()) {
    ImGui::TableNextColumn();
    ImGui::Text("%.3f ms", max_ms.value());
  }
  ImGui::TableNextColumn();
  ImGui::Text("%.3f ms", percentiles.p50_ms);
  ImGui::TableNextColumn();
  ImGui::Text("%.3f ms", percentiles.p95_ms);
  ImGui::TableNextColumn();
  ImGui::Text("%.3f ms", percentiles.p99_ms);
}

void render_player_tab(
    DebugMenuState& state,
    server::ServerRuntime& server,
    client::ClientRuntime& client) {
  player::PlayerState* player = local_player_state(server);
  if (player == nullptr) {
    ImGui::TextUnformatted("No local player session.");
    return;
  }

  ImGui::Text("Player ID: %u", player->player_id.value);
  ImGui::Text(
      "Position: %.2f, %.2f, %.2f",
      static_cast<double>(player->position.x),
      static_cast<double>(player->position.y),
      static_cast<double>(player->position.z));
  ImGui::Text(
      "Velocity: %.2f, %.2f, %.2f",
      static_cast<double>(player->velocity.x),
      static_cast<double>(player->velocity.y),
      static_cast<double>(player->velocity.z));
  ImGui::Text("Client mode: %s", client.game_mode_id().c_str());

  std::vector<std::string> mode_ids = server.game_modes().mode_ids();
  std::string current_mode = player->game_mode_id;
  if (ImGui::BeginCombo("Game mode", current_mode.c_str())) {
    for (const std::string& mode_id : mode_ids) {
      const bool selected = mode_id == current_mode;
      if (ImGui::Selectable(mode_id.c_str(), selected)) {
        (void)server.set_player_game_mode(player->player_id, mode_id);
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  ImGui::SeparatorText("Toggles");
  ImGui::Checkbox("Chunk boundaries", &state.show_chunk_boundaries);
  ImGui::Checkbox("Collision bounds", &state.show_collision_bounds);
  ImGui::Checkbox("Lighting view", &state.show_lighting_view);
}

[[nodiscard]] bool render_items_tab(
    DebugMenuState& state,
    server::ServerRuntime& server,
    const player::ItemRegistry& item_registry) {
  bool inventory_changed = false;
  ImGui::InputText("Search", &state.item_filter);
  std::vector<const player::ItemRecord*> items =
      filtered_items(item_registry, state.item_filter);
  if (state.selected_item_name.empty() && !items.empty()) {
    state.selected_item_name = items.front()->name;
  }

  const char* preview = state.selected_item_name.empty()
                            ? "Select item"
                            : state.selected_item_name.c_str();
  if (ImGui::BeginCombo("Item", preview)) {
    for (const player::ItemRecord* item : items) {
      const bool selected = item->name == state.selected_item_name;
      const std::string label = item->display_name.empty()
                                    ? item->name
                                    : item->display_name + " (" + item->name + ")";
      if (ImGui::Selectable(label.c_str(), selected)) {
        state.selected_item_name = item->name;
        state.stack_size = std::clamp<int>(state.stack_size, 1, item->max_stack_size);
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  const player::ItemRecord* item = selected_item(item_registry, state.selected_item_name);
  const int max_stack_size = item == nullptr ? 64 : static_cast<int>(item->max_stack_size);
  state.stack_size = std::clamp(state.stack_size, 1, max_stack_size);
  ImGui::SliderInt("Stack size", &state.stack_size, 1, max_stack_size);
  ImGui::Checkbox("Replace selected hotbar slot", &state.replace_selected_hotbar_slot);

  const server::ServerSession* session = local_session(server);
  player::PlayerState* player = local_player_state(server);
  const bool can_add = item != nullptr && session != nullptr && player != nullptr;
  if (!can_add) {
    ImGui::BeginDisabled();
  }

  if (ImGui::Button("Add to inventory")) {
    const player::InventoryInsertResult result = server.insert_player_inventory_stack(
        session->player_id,
        {item->id, static_cast<u16>(state.stack_size)});
    inventory_changed = true;
    state.console_log.emplace_back(
        result.complete() ? "Added item stack to inventory."
                          : "Inventory full; item stack was partially added.");
  }
  ImGui::SameLine();
  if (ImGui::Button("Add to selected hotbar")) {
    const player::InventoryInsertResult result = server.insert_player_inventory_stack(
        session->player_id,
        {item->id, static_cast<u16>(state.stack_size)},
        player->selected_hotbar_slot,
        state.replace_selected_hotbar_slot);
    inventory_changed = true;
    state.console_log.emplace_back(
        result.complete() ? "Added item stack to selected hotbar slot."
                          : "Hotbar/inventory full; item stack was partially added.");
  }

  if (!can_add) {
    ImGui::EndDisabled();
  }
  return inventory_changed;
}

void render_world_tab(
    const DebugMenuFrameInfo& frame_info,
    const server::ServerRuntime& server) {
  ImGui::Text("Server tick: %llu", static_cast<unsigned long long>(server.tick_index()));
  ImGui::Text("Base TPS: %u", server.base_tps());
  ImGui::Text("Visible chunks: %zu", frame_info.visible_chunk_count);
  ImGui::Text("Pending client chunks: %zu", frame_info.pending_client_chunk_count);
  ImGui::Text("Pending client unloads: %zu", frame_info.pending_client_unload_count);
  ImGui::SeparatorText("Controls");
  ImGui::BeginDisabled();
  ImGui::Button("Reload chunks");
  ImGui::Button("Teleport");
  ImGui::Button("Set time");
  ImGui::EndDisabled();
}

void render_performance_tab(
    const DebugMenuFrameInfo& frame_info,
    const sched::Scheduler& world_jobs,
    const render::OpenGLRenderer& renderer) {
  ImGui::Text("FPS: %u", frame_info.displayed_fps);
  ImGui::Text("Frame delta: %.2f ms", frame_info.frame_delta_ms);
  ImGui::Text("CPU frame work: %.2f ms", frame_info.frame_work_ms);
  ImGui::Text("Client workers: %zu", world_jobs.worker_count());
  ImGui::SeparatorText("Renderer");
  ImGui::Text("Pending mesh uploads: %zu", renderer.pending_mesh_upload_count());
  ImGui::Text("Mesh uploads last frame: %u", renderer.mesh_uploads_last_frame());
  ImGui::Text("Mesh upload discards last frame: %u", renderer.mesh_upload_discards_last_frame());
  ImGui::Text("Mesh upload time: %.3f ms", renderer.mesh_upload_time_ms_last_frame());
  ImGui::Text("Draw calls last frame: %u", renderer.draw_calls_last_frame());
  ImGui::Text("Chunk draw calls last frame: %u", renderer.chunk_draw_calls_last_frame());
  ImGui::Text("UI draw calls last frame: %u", renderer.ui_draw_calls_last_frame());
}

void render_profiling_tab(
    DebugMenuState& state,
    const DebugMenuFrameInfo& frame_info,
    const render::OpenGLRenderer& renderer) {
  ImGui::SeparatorText("Tracy");
  ImGui::Text(
      "Engine profiling: %s",
      core::profiling_enabled() ? "enabled" : "disabled");
  ImGui::Text(
      "CPU zones: %s",
      core::tracy_profiling_enabled() ? "enabled" : "disabled");
  ImGui::Text(
      "GPU zones: %s",
      core::tracy_profiling_enabled()
          ? (renderer.gpu_profile_context_initialized() ? "active" : "pending first render")
          : "disabled");

  if (!core::profiling_enabled()) {
    ImGui::TextUnformatted("Build with -DARVANE_ENABLE_PROFILING=ON to record profiling data.");
  } else if (!core::tracy_profiling_enabled()) {
    ImGui::TextUnformatted(
        "Build with -DARVANE_ENABLE_TRACY=ON to emit Tracy zones.");
  }

  ImGui::Checkbox("Pause frame sampling", &state.profile_sampling_paused);
  ImGui::SameLine();
  if (ImGui::Button("Reset timing stats")) {
    core::reset_global_timing_stats();
    clear_profile_frame_samples(state);
  }

  ImGui::SeparatorText("Frame timeline");
  if (state.profile_frame_sample_count == 0U) {
    ImGui::TextUnformatted("No frame samples recorded yet.");
  } else {
    constexpr u32 Capacity =
        static_cast<u32>(DebugMenuState::ProfileFrameHistoryCapacity);
    const int sample_count = static_cast<int>(state.profile_frame_sample_count);
    const int plot_offset = state.profile_frame_sample_count == Capacity
                                ? static_cast<int>(state.profile_frame_sample_index)
                                : 0;
    ImGui::PlotLines(
        "Frame delta",
        state.profile_frame_delta_ms.data(),
        sample_count,
        plot_offset,
        nullptr,
        0.0F,
        50.0F,
        ImVec2{0.0F, 58.0F});
    ImGui::PlotLines(
        "CPU frame work",
        state.profile_frame_work_ms.data(),
        sample_count,
        plot_offset,
        nullptr,
        0.0F,
        50.0F,
        ImVec2{0.0F, 58.0F});
  }

  const core::TimingPercentiles frame_delta =
      frame_percentiles(state.profile_frame_delta_ms, state.profile_frame_sample_count);
  const core::TimingPercentiles frame_work =
      frame_percentiles(state.profile_frame_work_ms, state.profile_frame_sample_count);
  if (ImGui::BeginTable(
          "FrameTimingPercentiles",
          6,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    render_percentile_header(false);
    render_percentile_cells("Frame delta", frame_delta, frame_info.frame_delta_ms, std::nullopt);
    render_percentile_cells("CPU frame work", frame_work, frame_info.frame_work_ms, std::nullopt);
    ImGui::EndTable();
  }

  ImGui::SeparatorText("Worker timing");
  if (ImGui::BeginTable(
          "WorkerTimingPercentiles",
          7,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    render_percentile_header(true);
    for (u8 metric_index = 0; metric_index < static_cast<u8>(core::PerfMetric::Count);
         ++metric_index) {
      const auto metric = static_cast<core::PerfMetric>(metric_index);
      const core::TimingMetricSnapshot snapshot = core::global_timing_snapshot(metric);
      render_percentile_cells(
          core::perf_metric_name(metric),
          snapshot.percentiles,
          snapshot.latest_ms,
          snapshot.max_ms);
    }
    ImGui::EndTable();
  }

  ImGui::SeparatorText("Zone coverage");
  struct ZoneLine {
    const char* zone;
    const char* lane;
  };
  constexpr std::array<ZoneLine, 11> zones{{
      {"Frame time", "main/render"},
      {"Simulation tick", "main/world"},
      {"Render submit", "render"},
      {"GPU wait time", "main/render"},
      {"GPU queue timing", "GPU"},
      {"Chunk generation", "worker/server"},
      {"Snapshot creation", "main/worker"},
      {"Chunk lookup / access", "world"},
      {"Chunk meshing", "worker"},
      {"Lighting propagation", "worker/server"},
      {"Job queue latency", "worker"},
  }};
  if (ImGui::BeginTable(
          "TracyZoneCoverage",
          2,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Zone");
    ImGui::TableSetupColumn("Timeline");
    ImGui::TableHeadersRow();
    for (const ZoneLine& zone : zones) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(zone.zone);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(zone.lane);
    }
    ImGui::EndTable();
  }
}

void render_network_tab(const server::ServerRuntime& server, const client::ClientRuntime& client) {
  ImGui::Text("Server mode: %s", server.mode() == server::ServerMode::Integrated ? "integrated" : "dedicated");
  ImGui::Text("Sessions: %zu", server.sessions().size());
  ImGui::Text("Connection state: %u", static_cast<unsigned>(client.connection_state()));
}

void render_console_tab(DebugMenuState& state) {
  ImGui::InputText("Command", &state.console_command);
  ImGui::SameLine();
  if (ImGui::Button("Run") && !state.console_command.empty()) {
    state.console_log.emplace_back("> " + state.console_command);
    state.console_log.emplace_back("Command runner is wired to the menu; command dispatch is pending.");
    state.console_command.clear();
  }
  ImGui::Separator();
  if (ImGui::BeginChild("Log", ImVec2{0.0F, 0.0F}, ImGuiChildFlags_Borders)) {
    for (const std::string& line : state.console_log) {
      ImGui::TextUnformatted(line.c_str());
    }
  }
  ImGui::EndChild();
}

} // namespace

DebugMenuResult render_debug_menu(
    DebugMenuState& state,
    const DebugMenuFrameInfo& frame_info,
    server::ServerRuntime& server,
    client::ClientRuntime& client,
    const player::ItemRegistry& item_registry,
    const sched::Scheduler& world_jobs,
    const render::OpenGLRenderer& renderer) {
  DebugMenuResult result;
  record_profile_frame_sample(state, frame_info);
  if (!state.open) {
    return result;
  }

  ImGui::SetNextWindowSize(ImVec2{760.0F, 520.0F}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Arvane Debug", &state.open)) {
    ImGui::End();
    return result;
  }

  if (ImGui::BeginTabBar("DebugTabs")) {
    if (ImGui::BeginTabItem("Player")) {
      render_player_tab(state, server, client);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("World")) {
      render_world_tab(frame_info, server);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Chunks")) {
      render_world_tab(frame_info, server);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Rendering")) {
      render_performance_tab(frame_info, world_jobs, renderer);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Items")) {
      result.inventory_changed =
          render_items_tab(state, server, item_registry) || result.inventory_changed;
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Entities")) {
      ImGui::Text("Dropped item stacks: %zu", server.dropped_item_stacks().size());
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Networking")) {
      render_network_tab(server, client);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Performance")) {
      render_performance_tab(frame_info, world_jobs, renderer);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Profiling")) {
      render_profiling_tab(state, frame_info, renderer);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Console")) {
      render_console_tab(state);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  ImGui::End();
  return result;
}

} // namespace arv::desktop
