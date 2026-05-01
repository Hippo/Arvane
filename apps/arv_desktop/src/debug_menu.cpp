#include "debug_menu.hpp"

#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"

#include <algorithm>
#include <array>
#include <charconv>
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

[[nodiscard]] std::vector<const entity::EntityTypeRecord*> filtered_entity_types(
    const entity::EntityRegistry& registry,
    std::string_view filter) {
  std::vector<const entity::EntityTypeRecord*> types;
  types.reserve(registry.type_count());
  for (const entity::EntityTypeRecord& type : registry.records()) {
    if (!type.spawnable) {
      continue;
    }
    if (contains_case_insensitive(type.name, filter) ||
        contains_case_insensitive(type.display_name, filter) ||
        contains_case_insensitive(type.category, filter) ||
        contains_case_insensitive(type.model_asset, filter)) {
      types.emplace_back(&type);
    }
  }
  return types;
}

[[nodiscard]] const entity::EntityTypeRecord* selected_entity_type(
    const entity::EntityRegistry& registry,
    std::string_view name) {
  const std::optional<EntityTypeID> id = registry.find_type(name);
  return id.has_value() ? registry.type(id.value()) : nullptr;
}

[[nodiscard]] std::optional<EntityID> parse_entity_id(std::string_view text) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }

  u64 value = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const std::from_chars_result parsed = std::from_chars(begin, end, value);
  if (parsed.ec != std::errc{} || parsed.ptr != end || value == 0U) {
    return std::nullopt;
  }
  return EntityID{value};
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

void clear_queue_profile(DebugMenuState::QueueProfileState& queue) noexcept {
  queue.previous_submitted_jobs = 0;
  queue.previous_completed_jobs = 0;
  queue.current_queued_jobs = 0;
  queue.peak_queued_jobs_window = 0;
  queue.submitted_jobs_this_frame = 0;
  queue.completed_jobs_this_frame = 0;
  queue.active_workers = 0;
  queue.idle_workers = 0;
  queue.idle_percent = 0.0;
  queue.backlog_growing = false;
  queue.queued_history_index = 0;
  queue.queued_history_count = 0;
  queue.queued_history.fill(0U);
}

void clear_snapshot_profile(DebugMenuState& state) noexcept {
  state.snapshot_created_this_frame = 0;
  state.snapshot_reused_this_frame = 0;
  state.snapshot_copies_this_frame = 0;
  state.snapshot_bytes_copied_this_frame = 0;
  state.peak_snapshot_bytes_copied_window = 0;
  state.previous_snapshots_created_total = 0;
  state.previous_snapshots_reused_total = 0;
  state.previous_snapshot_copies_total = 0;
  state.previous_snapshot_bytes_copied_total = 0;
  state.snapshot_bytes_history_index = 0;
  state.snapshot_bytes_history_count = 0;
  state.snapshot_bytes_history.fill(0U);
}

[[nodiscard]] u32 delta_u64_to_u32(u64 current, u64 previous) noexcept {
  const u64 delta = current >= previous ? current - previous : current;
  return delta > static_cast<u64>(~u32{0}) ? ~u32{0} : static_cast<u32>(delta);
}

void record_queue_profile_sample(
    DebugMenuState::QueueProfileState& queue,
    const sched::SchedulerPressureSnapshot& snapshot) noexcept {
  const u32 previous_queued = queue.current_queued_jobs;
  queue.current_queued_jobs = snapshot.current_queued_jobs;
  queue.submitted_jobs_this_frame =
      delta_u64_to_u32(snapshot.submitted_jobs_total, queue.previous_submitted_jobs);
  queue.completed_jobs_this_frame =
      delta_u64_to_u32(snapshot.completed_jobs_total, queue.previous_completed_jobs);
  queue.previous_submitted_jobs = snapshot.submitted_jobs_total;
  queue.previous_completed_jobs = snapshot.completed_jobs_total;
  queue.active_workers = snapshot.active_workers;
  queue.idle_workers = snapshot.idle_workers;
  queue.idle_percent = snapshot.idle_percent;
  queue.backlog_growing = snapshot.current_queued_jobs > previous_queued;

  constexpr u32 Capacity =
      static_cast<u32>(DebugMenuState::ProfileFrameHistoryCapacity);
  queue.queued_history[queue.queued_history_index] = snapshot.current_queued_jobs;
  queue.queued_history_index = (queue.queued_history_index + 1U) % Capacity;
  if (queue.queued_history_count < Capacity) {
    ++queue.queued_history_count;
  }
  queue.peak_queued_jobs_window = snapshot.current_queued_jobs;
  for (u32 index = 0; index < queue.queued_history_count; ++index) {
    queue.peak_queued_jobs_window =
        std::max(queue.peak_queued_jobs_window, queue.queued_history[index]);
  }
}

void record_snapshot_profile_sample(DebugMenuState& state) noexcept {
  const core::SnapshotCostSnapshot snapshot = core::global_snapshot_cost_snapshot();
  state.snapshot_created_this_frame =
      delta_u64_to_u32(snapshot.snapshots_created_total, state.previous_snapshots_created_total);
  state.snapshot_reused_this_frame =
      delta_u64_to_u32(snapshot.snapshots_reused_total, state.previous_snapshots_reused_total);
  state.snapshot_copies_this_frame =
      delta_u64_to_u32(snapshot.snapshot_copies_total, state.previous_snapshot_copies_total);
  state.snapshot_bytes_copied_this_frame =
      snapshot.snapshot_bytes_copied_total >= state.previous_snapshot_bytes_copied_total
          ? snapshot.snapshot_bytes_copied_total - state.previous_snapshot_bytes_copied_total
          : snapshot.snapshot_bytes_copied_total;
  state.previous_snapshots_created_total = snapshot.snapshots_created_total;
  state.previous_snapshots_reused_total = snapshot.snapshots_reused_total;
  state.previous_snapshot_copies_total = snapshot.snapshot_copies_total;
  state.previous_snapshot_bytes_copied_total = snapshot.snapshot_bytes_copied_total;

  constexpr u32 Capacity =
      static_cast<u32>(DebugMenuState::ProfileFrameHistoryCapacity);
  state.snapshot_bytes_history[state.snapshot_bytes_history_index] =
      state.snapshot_bytes_copied_this_frame;
  state.snapshot_bytes_history_index = (state.snapshot_bytes_history_index + 1U) % Capacity;
  if (state.snapshot_bytes_history_count < Capacity) {
    ++state.snapshot_bytes_history_count;
  }
  state.peak_snapshot_bytes_copied_window = state.snapshot_bytes_copied_this_frame;
  for (u32 index = 0; index < state.snapshot_bytes_history_count; ++index) {
    state.peak_snapshot_bytes_copied_window =
        std::max(state.peak_snapshot_bytes_copied_window, state.snapshot_bytes_history[index]);
  }
}

[[nodiscard]] core::TimingPercentiles frame_percentiles(
    const std::array<f32, DebugMenuState::ProfileFrameHistoryCapacity>& samples,
    u32 sample_index,
    u32 sample_count,
    u32 requested_window) {
  core::TimingPercentiles result;
  constexpr u32 Capacity =
      static_cast<u32>(DebugMenuState::ProfileFrameHistoryCapacity);
  const u32 stored_count = std::min<u32>(sample_count, Capacity);
  const u32 window_count = std::min<u32>(stored_count, requested_window);
  result.sample_count = window_count;
  if (window_count == 0U) {
    return result;
  }

  std::array<f32, DebugMenuState::ProfileFrameHistoryCapacity> sorted{};
  const u32 oldest = stored_count == Capacity ? sample_index : 0U;
  const u32 first_recent_offset = stored_count - window_count;
  for (u32 index = 0; index < window_count; ++index) {
    const u32 sample_slot =
        (oldest + first_recent_offset + index) % Capacity;
    sorted[index] = samples[sample_slot];
  }
  std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(window_count));

  auto percentile_value = [&](u32 percentile) {
    const u32 rank = static_cast<u32>(
        (static_cast<u64>(window_count - 1U) * static_cast<u64>(percentile) + 99ULL) /
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

[[nodiscard]] u32 collect_frame_window(
    const std::array<f32, DebugMenuState::ProfileFrameHistoryCapacity>& samples,
    u32 sample_index,
    u32 sample_count,
    u32 requested_window,
    std::array<f32, DebugMenuState::ProfileFrameHistoryCapacity>& out) noexcept {
  constexpr u32 Capacity =
      static_cast<u32>(DebugMenuState::ProfileFrameHistoryCapacity);
  const u32 stored_count = std::min<u32>(sample_count, Capacity);
  const u32 window_count = std::min<u32>(stored_count, requested_window);
  if (window_count == 0U) {
    return 0U;
  }

  const u32 oldest = stored_count == Capacity ? sample_index : 0U;
  const u32 first_recent_offset = stored_count - window_count;
  for (u32 index = 0; index < window_count; ++index) {
    const u32 sample_slot =
        (oldest + first_recent_offset + index) % Capacity;
    out[index] = samples[sample_slot];
  }
  return window_count;
}

[[nodiscard]] ImVec4 severity_color(core::PerfMetric metric, double milliseconds) noexcept {
  const ImVec4 green{0.45F, 0.86F, 0.52F, 1.0F};
  const ImVec4 yellow{1.0F, 0.78F, 0.28F, 1.0F};
  const ImVec4 red{1.0F, 0.36F, 0.32F, 1.0F};

  switch (metric) {
    case core::PerfMetric::FrameDelta:
    case core::PerfMetric::CpuFrameWork:
    case core::PerfMetric::SimulationTick:
    case core::PerfMetric::RenderSubmit:
    case core::PerfMetric::GpuWaitTime:
      if (milliseconds > 16.6) {
        return red;
      }
      if (milliseconds > 8.0) {
        return yellow;
      }
      return green;
    case core::PerfMetric::JobQueueLatency:
      if (milliseconds > 4.0) {
        return red;
      }
      if (milliseconds > 1.0) {
        return yellow;
      }
      return green;
    case core::PerfMetric::MeshingTask:
    case core::PerfMetric::LightingTask:
    case core::PerfMetric::GenerationTask:
    case core::PerfMetric::FluidTask:
    case core::PerfMetric::SnapshotCreation:
    case core::PerfMetric::ChunkLookupAccess:
      if (milliseconds > 8.0) {
        return red;
      }
      if (milliseconds > 2.0) {
        return yellow;
      }
      return green;
    case core::PerfMetric::GpuQueueTiming:
    case core::PerfMetric::Count:
      break;
  }
  return green;
}

void render_ms_cell(core::PerfMetric metric, double milliseconds) {
  ImGui::TextColored(severity_color(metric, milliseconds), "%.3f ms", milliseconds);
}

void render_u32_cell(u32 value, bool warning, bool danger = false) {
  if (danger) {
    ImGui::TextColored(ImVec4{1.0F, 0.36F, 0.32F, 1.0F}, "%u", value);
  } else if (warning) {
    ImGui::TextColored(ImVec4{1.0F, 0.78F, 0.28F, 1.0F}, "%u", value);
  } else {
    ImGui::Text("%u", value);
  }
}

void render_timing_histogram(
    const char* label,
    const std::array<f32, DebugMenuState::ProfileFrameHistoryCapacity>& samples,
    u32 sample_count) {
  struct Bucket {
    const char* label;
    f32 min_ms;
    f32 max_ms;
  };
  constexpr std::array<Bucket, 7> Buckets{{
      {"< 0.25 ms", 0.0F, 0.25F},
      {"0.25 - 0.5 ms", 0.25F, 0.5F},
      {"0.5 - 1 ms", 0.5F, 1.0F},
      {"1 - 2 ms", 1.0F, 2.0F},
      {"2 - 5 ms", 2.0F, 5.0F},
      {"5 - 10 ms", 5.0F, 10.0F},
      {"> 10 ms", 10.0F, 60000.0F},
  }};
  std::array<u32, Buckets.size()> counts{};
  for (u32 index = 0; index < sample_count; ++index) {
    const f32 sample = samples[index];
    for (usize bucket_index = 0; bucket_index < Buckets.size(); ++bucket_index) {
      const Bucket& bucket = Buckets[bucket_index];
      if (sample >= bucket.min_ms && sample < bucket.max_ms) {
        ++counts[bucket_index];
        break;
      }
    }
  }

  ImGui::TextUnformatted(label);
  if (ImGui::BeginTable(
          label,
          3,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Bucket", ImGuiTableColumnFlags_WidthFixed, 110.0F);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 70.0F);
    ImGui::TableSetupColumn("Share");
    ImGui::TableHeadersRow();
    for (usize bucket_index = 0; bucket_index < Buckets.size(); ++bucket_index) {
      const float fraction = sample_count == 0U
                                 ? 0.0F
                                 : static_cast<float>(counts[bucket_index]) /
                                       static_cast<float>(sample_count);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(Buckets[bucket_index].label);
      ImGui::TableNextColumn();
      ImGui::Text("%u", counts[bucket_index]);
      ImGui::TableNextColumn();
      ImGui::ProgressBar(fraction, ImVec2{-1.0F, 0.0F});
    }
    ImGui::EndTable();
  }
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

void render_entities_tab(
    DebugMenuState& state,
    server::ServerRuntime& server,
    const entity::EntityRegistry& entity_registry) {
  world::WorldRuntime& world = server.voxel_world();
  const std::vector<EntityID> entity_ids = world.entity_ids();
  ImGui::Text("Runtime entities: %zu", entity_ids.size());
  ImGui::Text("Dropped item stacks: %zu", server.dropped_item_stacks().size());

  ImGui::SeparatorText("Spawn");
  ImGui::InputText("Search types", &state.entity_filter);
  std::vector<const entity::EntityTypeRecord*> types =
      filtered_entity_types(entity_registry, state.entity_filter);
  if (state.selected_entity_type_name.empty() && !types.empty()) {
    const entity::EntityTypeRecord* type = types.front();
    state.selected_entity_type_name = type->name;
    state.entity_persistent = type->persistent_by_default;
    state.entity_replicated = type->replicated_by_default;
  }

  const char* preview = state.selected_entity_type_name.empty()
                            ? "Select entity type"
                            : state.selected_entity_type_name.c_str();
  if (ImGui::BeginCombo("Entity type", preview)) {
    for (const entity::EntityTypeRecord* type : types) {
      const bool selected = type->name == state.selected_entity_type_name;
      const std::string label = type->display_name.empty()
                                    ? type->name
                                    : type->display_name + " (" + type->name + ")";
      if (ImGui::Selectable(label.c_str(), selected)) {
        state.selected_entity_type_name = type->name;
        state.entity_persistent = type->persistent_by_default;
        state.entity_replicated = type->replicated_by_default;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  const entity::EntityTypeRecord* selected_type =
      selected_entity_type(entity_registry, state.selected_entity_type_name);
  if (selected_type != nullptr) {
    ImGui::Text("Display name: %s", selected_type->display_name.c_str());
    ImGui::Text("Category: %s", selected_type->category.empty() ? "none" : selected_type->category.c_str());
    ImGui::Text("Model: %s", selected_type->model_asset.empty() ? "none" : selected_type->model_asset.c_str());
    ImGui::Text("Type ID: %u", selected_type->id.value);
  }

  ImGui::Checkbox("Spawn at player", &state.entity_spawn_at_player);
  if (state.entity_spawn_at_player) {
    ImGui::InputFloat3("Player offset", state.entity_spawn_offset);
  } else {
    ImGui::InputFloat3("World position", state.entity_spawn_position);
  }
  ImGui::Checkbox("Persistent", &state.entity_persistent);
  ImGui::SameLine();
  ImGui::Checkbox("Replicated", &state.entity_replicated);
  if (state.entity_persistent) {
    ImGui::InputText("Persistent UUID", &state.entity_persistent_uuid);
  }

  const player::PlayerState* local_player = local_player_state(server);
  const bool can_spawn =
      selected_type != nullptr && (!state.entity_spawn_at_player || local_player != nullptr);
  if (!can_spawn) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Spawn entity")) {
    Vec3f position{
        state.entity_spawn_position[0],
        state.entity_spawn_position[1],
        state.entity_spawn_position[2]};
    if (state.entity_spawn_at_player && local_player != nullptr) {
      position = {
          local_player->position.x + state.entity_spawn_offset[0],
          local_player->position.y + state.entity_spawn_offset[1],
          local_player->position.z + state.entity_spawn_offset[2]};
    }

    world::EntityCreateInfo info;
    info.type_id = selected_type->id;
    info.type_name = selected_type->name;
    info.position = position;
    info.persistent = state.entity_persistent;
    info.persistent_uuid = state.entity_persistent_uuid;
    info.replicated = state.entity_replicated;
    const EntityID spawned = world.create_entity(std::move(info));
    state.selected_entity_id = std::to_string(spawned.value);
    state.console_log.emplace_back("Spawned entity " + state.selected_entity_id + " (" +
                                   selected_type->name + ").");
  }
  if (!can_spawn) {
    ImGui::EndDisabled();
  }
  if (state.entity_spawn_at_player && local_player == nullptr) {
    ImGui::TextUnformatted("No local player is available for player-relative spawning.");
  }

  ImGui::SeparatorText("Selected Entity");
  ImGui::InputText("Entity ID", &state.selected_entity_id);
  const std::optional<EntityID> selected_entity = parse_entity_id(state.selected_entity_id);
  const bool selected_exists =
      selected_entity.has_value() && world.entity_exists(selected_entity.value());
  const bool selected_is_player =
      selected_entity.has_value() && world.player_for_entity(selected_entity.value()).has_value();
  if (selected_exists) {
    const entity::EntityTransform* transform =
        world.find_entity_transform(selected_entity.value());
    if (transform != nullptr) {
      ImGui::Text(
          "Position: %.2f, %.2f, %.2f",
          static_cast<double>(transform->position.x),
          static_cast<double>(transform->position.y),
          static_cast<double>(transform->position.z));
    }
  } else if (!state.selected_entity_id.empty()) {
    ImGui::TextUnformatted("Selected entity is not loaded.");
  }

  if (!selected_exists || selected_is_player) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Move selected to spawn position")) {
    Vec3f position{
        state.entity_spawn_position[0],
        state.entity_spawn_position[1],
        state.entity_spawn_position[2]};
    if (state.entity_spawn_at_player && local_player != nullptr) {
      position = {
          local_player->position.x + state.entity_spawn_offset[0],
          local_player->position.y + state.entity_spawn_offset[1],
          local_player->position.z + state.entity_spawn_offset[2]};
    }
    if (world.set_entity_position(selected_entity.value(), position)) {
      state.console_log.emplace_back("Moved entity " + state.selected_entity_id + ".");
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Destroy selected")) {
    if (world.destroy_entity(selected_entity.value())) {
      state.console_log.emplace_back("Queued entity " + state.selected_entity_id + " for despawn.");
    }
  }
  if (!selected_exists || selected_is_player) {
    ImGui::EndDisabled();
  }

  if (selected_exists) {
    const entity::EntityTransform* transform =
        world.find_entity_transform(selected_entity.value());
    if (transform != nullptr && ImGui::Button("Use selected position")) {
      state.entity_spawn_position[0] = transform->position.x;
      state.entity_spawn_position[1] = transform->position.y;
      state.entity_spawn_position[2] = transform->position.z;
      state.entity_spawn_at_player = false;
    }
  }

  ImGui::SeparatorText("Loaded Entities");
  if (ImGui::BeginTable(
          "LoadedEntities",
          8,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
              ImGuiTableFlags_ScrollY,
          ImVec2{0.0F, 220.0F})) {
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 72.0F);
    ImGui::TableSetupColumn("Net", ImGuiTableColumnFlags_WidthFixed, 72.0F);
    ImGui::TableSetupColumn("Type");
    ImGui::TableSetupColumn("Position");
    ImGui::TableSetupColumn("Chunk");
    ImGui::TableSetupColumn("Owner", ImGuiTableColumnFlags_WidthFixed, 72.0F);
    ImGui::TableSetupColumn("Persistent", ImGuiTableColumnFlags_WidthFixed, 82.0F);
    ImGui::TableSetupColumn("Replicated", ImGuiTableColumnFlags_WidthFixed, 82.0F);
    ImGui::TableHeadersRow();

    for (const EntityID entity_id : entity_ids) {
      const std::string id_label = std::to_string(entity_id.value);
      const entity::EntityType* type = world.find_entity_type(entity_id);
      const entity::EntityTransform* transform = world.find_entity_transform(entity_id);
      const std::optional<EntityNetID> net_id = world.network_id_for_entity(entity_id);
      const std::optional<ChunkCoord> chunk = world.entity_chunk(entity_id);
      const std::optional<PlayerID> owner = world.player_for_entity(entity_id);
      const bool selected = state.selected_entity_id == id_label;

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (ImGui::Selectable(id_label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
        state.selected_entity_id = id_label;
      }
      ImGui::TableNextColumn();
      if (net_id.has_value()) {
        ImGui::Text("%u", net_id->value);
      } else {
        ImGui::TextUnformatted("-");
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(type == nullptr ? "unknown" : type->type_name.c_str());
      ImGui::TableNextColumn();
      if (transform != nullptr) {
        ImGui::Text(
            "%.1f, %.1f, %.1f",
            static_cast<double>(transform->position.x),
            static_cast<double>(transform->position.y),
            static_cast<double>(transform->position.z));
      } else {
        ImGui::TextUnformatted("-");
      }
      ImGui::TableNextColumn();
      if (chunk.has_value()) {
        ImGui::Text(
            "%d, %d, %d",
            chunk->value.x,
            chunk->value.y,
            chunk->value.z);
      } else {
        ImGui::TextUnformatted("-");
      }
      ImGui::TableNextColumn();
      if (owner.has_value()) {
        ImGui::Text("%llu", static_cast<unsigned long long>(owner->value));
      } else {
        ImGui::TextUnformatted("-");
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(world.entity_is_persistent(entity_id) ? "yes" : "no");
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(net_id.has_value() ? "yes" : "no");
    }
    ImGui::EndTable();
  }
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
  ImGui::Text(
      "Submitted: %llu vertices, %llu triangles",
      static_cast<unsigned long long>(renderer.vertices_submitted_last_frame()),
      static_cast<unsigned long long>(renderer.triangles_submitted_last_frame()));
  ImGui::Text(
      "GPU buffer uploads: %u (%llu bytes)",
      renderer.gpu_buffer_uploads_last_frame(),
      static_cast<unsigned long long>(renderer.gpu_buffer_upload_bytes_last_frame()));
  ImGui::Text(
      "Texture uploads: %u (%llu bytes)",
      renderer.texture_uploads_last_frame(),
      static_cast<unsigned long long>(renderer.texture_upload_bytes_last_frame()));
}

void render_frame_breakdown(DebugMenuState& state, const DebugMenuFrameInfo& frame_info) {
  const u32 requested_window = static_cast<u32>(std::clamp(
      state.profile_frame_window_size,
      1,
      static_cast<int>(DebugMenuState::ProfileFrameHistoryCapacity)));
  const core::TimingPercentiles frame_delta = frame_percentiles(
      state.profile_frame_delta_ms,
      state.profile_frame_sample_index,
      state.profile_frame_sample_count,
      requested_window);
  const core::TimingPercentiles frame_work = frame_percentiles(
      state.profile_frame_work_ms,
      state.profile_frame_sample_index,
      state.profile_frame_sample_count,
      requested_window);

  struct BreakdownLine {
    core::PerfMetric metric;
    const core::TimingPercentiles* local_percentiles;
    double local_latest_ms;
  };
  const std::array<BreakdownLine, 8> lines{{
      {core::PerfMetric::FrameDelta, &frame_delta, frame_info.frame_delta_ms},
      {core::PerfMetric::CpuFrameWork, &frame_work, frame_info.frame_work_ms},
      {core::PerfMetric::SimulationTick, nullptr, 0.0},
      {core::PerfMetric::RenderSubmit, nullptr, 0.0},
      {core::PerfMetric::GpuWaitTime, nullptr, 0.0},
      {core::PerfMetric::GpuQueueTiming, nullptr, 0.0},
      {core::PerfMetric::SnapshotCreation, nullptr, 0.0},
      {core::PerfMetric::ChunkLookupAccess, nullptr, 0.0},
  }};

  ImGui::SeparatorText("Frame Breakdown");
  if (ImGui::BeginTable(
          "FrameBreakdown",
          7,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Metric");
    ImGui::TableSetupColumn("Samples", ImGuiTableColumnFlags_WidthFixed, 70.0F);
    ImGui::TableSetupColumn("Latest", ImGuiTableColumnFlags_WidthFixed, 90.0F);
    ImGui::TableSetupColumn("P50", ImGuiTableColumnFlags_WidthFixed, 80.0F);
    ImGui::TableSetupColumn("P95", ImGuiTableColumnFlags_WidthFixed, 80.0F);
    ImGui::TableSetupColumn("P99", ImGuiTableColumnFlags_WidthFixed, 80.0F);
    ImGui::TableSetupColumn("Timeline");
    ImGui::TableHeadersRow();
    for (const BreakdownLine& line : lines) {
      core::TimingMetricSnapshot snapshot{};
      core::TimingPercentiles percentiles{};
      double latest_ms = line.local_latest_ms;
      if (line.local_percentiles != nullptr) {
        percentiles = *line.local_percentiles;
      } else {
        snapshot = core::global_timing_snapshot(line.metric);
        percentiles = snapshot.percentiles;
        latest_ms = snapshot.latest_ms;
      }

      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(core::perf_metric_name(line.metric));
      ImGui::TableNextColumn();
      ImGui::Text("%u", percentiles.sample_count);
      ImGui::TableNextColumn();
      render_ms_cell(line.metric, latest_ms);
      ImGui::TableNextColumn();
      ImGui::Text("%.3f", percentiles.p50_ms);
      ImGui::TableNextColumn();
      ImGui::Text("%.3f", percentiles.p95_ms);
      ImGui::TableNextColumn();
      ImGui::Text("%.3f", percentiles.p99_ms);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(core::perf_metric_thread_category(line.metric));
    }
    ImGui::EndTable();
  }
}

void render_worst_events(
    const char* title,
    core::PerfEventGroup group,
    int max_rows) {
  ImGui::SeparatorText(title);
  std::array<core::PerfEventSample, 64> samples{};
  const usize count = core::global_worst_event_samples(samples, group);
  if (count == 0U) {
    ImGui::TextUnformatted("No samples recorded yet.");
    return;
  }

  const usize rows = std::min<usize>(static_cast<usize>(std::max(max_rows, 1)), count);
  if (ImGui::BeginTable(
          title,
          5,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Metric");
    ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 96.0F);
    ImGui::TableSetupColumn("Frame", ImGuiTableColumnFlags_WidthFixed, 74.0F);
    ImGui::TableSetupColumn("Thread / Timeline", ImGuiTableColumnFlags_WidthFixed, 130.0F);
    ImGui::TableSetupColumn("Context");
    ImGui::TableHeadersRow();
    for (usize index = 0; index < rows; ++index) {
      const core::PerfEventSample& sample = samples[index];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(core::perf_metric_name(sample.metric));
      ImGui::TableNextColumn();
      render_ms_cell(sample.metric, sample.duration_ms());
      ImGui::TableNextColumn();
      ImGui::Text("%llu", static_cast<unsigned long long>(sample.frame_index));
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(sample.thread[0] == '\0' ? "-" : sample.thread.data());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(sample.context[0] == '\0' ? "-" : sample.context.data());
    }
    ImGui::EndTable();
  }
}

void render_queue_pressure(
    const DebugMenuState& state,
    const sched::Scheduler& client_jobs,
    const server::ServerRuntime& server) {
  (void)client_jobs;
  ImGui::SeparatorText("Queue Pressure");
  if (ImGui::BeginTable(
          "QueuePressure",
          8,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Queue");
    ImGui::TableSetupColumn("Queued", ImGuiTableColumnFlags_WidthFixed, 70.0F);
    ImGui::TableSetupColumn("Peak", ImGuiTableColumnFlags_WidthFixed, 70.0F);
    ImGui::TableSetupColumn("Submitted", ImGuiTableColumnFlags_WidthFixed, 78.0F);
    ImGui::TableSetupColumn("Completed", ImGuiTableColumnFlags_WidthFixed, 78.0F);
    ImGui::TableSetupColumn("Active", ImGuiTableColumnFlags_WidthFixed, 64.0F);
    ImGui::TableSetupColumn("Idle", ImGuiTableColumnFlags_WidthFixed, 64.0F);
    ImGui::TableSetupColumn("Idle %", ImGuiTableColumnFlags_WidthFixed, 70.0F);
    ImGui::TableHeadersRow();

    auto row = [](const char* label, const DebugMenuState::QueueProfileState& queue) {
      const bool warning = queue.peak_queued_jobs_window > 0U;
      const bool danger = queue.backlog_growing && queue.current_queued_jobs > 0U;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      render_u32_cell(queue.current_queued_jobs, warning, danger);
      ImGui::TableNextColumn();
      render_u32_cell(queue.peak_queued_jobs_window, warning, danger);
      ImGui::TableNextColumn();
      ImGui::Text("%u", queue.submitted_jobs_this_frame);
      ImGui::TableNextColumn();
      ImGui::Text("%u", queue.completed_jobs_this_frame);
      ImGui::TableNextColumn();
      ImGui::Text("%u", queue.active_workers);
      ImGui::TableNextColumn();
      ImGui::Text("%u", queue.idle_workers);
      ImGui::TableNextColumn();
      ImGui::Text("%.0f%%", queue.idle_percent);
    };

    row("server_world", state.server_queue_profile);
    row("client_chunk_meshing", state.client_queue_profile);
    ImGui::EndTable();
  }

  ImGui::Text(
      "Server jobs: generation=%zu lighting=%zu fluids=%zu",
      server.pending_generation_job_count(),
      server.pending_lighting_job_count(),
      server.pending_fluid_job_count());
}

void render_chunk_workload(
    const DebugMenuFrameInfo& frame_info,
    const server::ServerRuntime& server,
    const render::OpenGLRenderer& renderer,
    const ChunkViewState& chunk_view) {
  const world::ChunkWorkloadSnapshot workload =
      server.voxel_world().chunk_workload_snapshot();
  ImGui::SeparatorText("Chunk Workload");
  if (ImGui::BeginTable(
          "ChunkWorkload",
          3,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Counter");
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 110.0F);
    ImGui::TableSetupColumn("Source");
    ImGui::TableHeadersRow();
    auto row = [](const char* label, usize value, const char* source, bool warn = false) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      if (warn && value > 0U) {
        ImGui::TextColored(
            ImVec4{1.0F, 0.78F, 0.28F, 1.0F},
            "%zu",
            value);
      } else {
        ImGui::Text("%zu", value);
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(source);
    };
    row("Loaded chunks", workload.loaded_chunks, "world lifecycle");
    row("Active / simulated chunks", workload.active_chunks, "world lifecycle");
    row("Visible / renderable chunks", frame_info.visible_chunk_count, "client chunk view");
    row(
        "Chunks awaiting generation",
        workload.chunks_awaiting_generation + server.pending_generation_job_count(),
        "world/server jobs",
        true);
    row(
        "Sections awaiting lighting",
        workload.chunk_sections_awaiting_lighting + server.pending_lighting_job_count(),
        "world/server jobs",
        true);
    row(
        "Dirty mesh sections",
        workload.chunk_sections_awaiting_meshing,
        "world dirty queue",
        true);
    row("Mesh candidates", chunk_view.mesh_candidates.size(), "client chunk view", true);
    row("Mesh jobs in flight", chunk_view.pending_meshes.size(), "client workers", true);
    row("Mesh uploads pending", renderer.pending_mesh_upload_count(), "renderer", true);
    ImGui::EndTable();
  }
}

void render_snapshot_costs(const DebugMenuState& state) {
  ImGui::SeparatorText("Snapshot Costs");
  if (ImGui::BeginTable(
          "SnapshotCosts",
          2,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Counter");
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 150.0F);
    ImGui::TableHeadersRow();
    auto row_u32 = [](const char* label, u32 value) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      ImGui::Text("%u", value);
    };
    auto row_u64 = [](const char* label, u64 value, bool warn = false) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      if (warn && value > 0U) {
        ImGui::TextColored(
            ImVec4{1.0F, 0.78F, 0.28F, 1.0F},
            "%llu",
            static_cast<unsigned long long>(value));
      } else {
        ImGui::Text("%llu", static_cast<unsigned long long>(value));
      }
    };
    row_u32("Snapshots created this frame", state.snapshot_created_this_frame);
    row_u32("Snapshots reused this frame", state.snapshot_reused_this_frame);
    row_u32("Snapshot copies this frame", state.snapshot_copies_this_frame);
    row_u64("Snapshot bytes copied this frame", state.snapshot_bytes_copied_this_frame, true);
    row_u64("Peak bytes copied in window", state.peak_snapshot_bytes_copied_window, true);
    ImGui::EndTable();
  }
}

void render_lighting_profile() {
  const core::LightingStatsSnapshot lighting = core::global_lighting_stats_snapshot();
  ImGui::SeparatorText("Lighting Jobs");
  if (ImGui::BeginTable(
          "LightingProfile",
          2,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Counter");
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 150.0F);
    ImGui::TableHeadersRow();
    auto row_u32 = [](const char* label, u32 value, bool warn = false) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      render_u32_cell(value, warn, warn && value > 8192U);
    };
    auto row_u64 = [](const char* label, u64 value, bool warn = false) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      if (warn && value > 0U) {
        ImGui::TextColored(
            ImVec4{1.0F, 0.78F, 0.28F, 1.0F},
            "%llu",
            static_cast<unsigned long long>(value));
      } else {
        ImGui::Text("%llu", static_cast<unsigned long long>(value));
      }
    };

    row_u64("Jobs submitted", lighting.jobs_submitted_total);
    row_u64("Jobs completed", lighting.jobs_completed_total);
    row_u64("Jobs continued / rescheduled", lighting.jobs_continued_total, true);
    row_u64("Nodes processed", lighting.nodes_processed_total);
    row_u64("Add nodes processed", lighting.add_nodes_processed_total);
    row_u64("Remove nodes processed", lighting.remove_nodes_processed_total);
    row_u64("Boundary deferrals", lighting.boundary_deferrals_total, true);
    row_u64("Sections touched", lighting.sections_touched_total);
    row_u64("Sections published", lighting.sections_published_total);
    row_u64("Lighting remeshes triggered", lighting.remeshes_triggered_total);
    row_u32(
        "Max queue depth",
        lighting.max_queue_depth,
        lighting.max_queue_depth > world::DefaultLightingWorkBudgetNodes);
    ImGui::EndTable();
  }
}

void render_gpu_render_metrics(const render::OpenGLRenderer& renderer) {
  ImGui::SeparatorText("GPU / Render Metrics");
  if (ImGui::BeginTable(
          "GpuRenderMetrics",
          2,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Metric");
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 150.0F);
    ImGui::TableHeadersRow();
    auto row_u32 = [](const char* label, u32 value) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      ImGui::Text("%u", value);
    };
    auto row_u64 = [](const char* label, u64 value) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      ImGui::Text("%llu", static_cast<unsigned long long>(value));
    };
    row_u32("Draw calls", renderer.draw_calls_last_frame());
    row_u32("Chunk draw calls", renderer.chunk_draw_calls_last_frame());
    row_u32("UI draw calls", renderer.ui_draw_calls_last_frame());
    row_u64("Vertices submitted", renderer.vertices_submitted_last_frame());
    row_u64("Triangles submitted", renderer.triangles_submitted_last_frame());
    row_u32("GPU buffer uploads", renderer.gpu_buffer_uploads_last_frame());
    row_u64("GPU buffer upload bytes", renderer.gpu_buffer_upload_bytes_last_frame());
    row_u32("Texture / atlas uploads", renderer.texture_uploads_last_frame());
    row_u64("Texture / atlas bytes", renderer.texture_upload_bytes_last_frame());
    row_u32("Mesh uploads", renderer.mesh_uploads_last_frame());
    row_u64("Pending mesh uploads", renderer.pending_mesh_upload_count());
    ImGui::EndTable();
  }
}

void render_profiling_tab(
    DebugMenuState& state,
    const DebugMenuFrameInfo& frame_info,
    server::ServerRuntime& server,
    sched::Scheduler& world_jobs,
    const render::OpenGLRenderer& renderer,
    const ChunkViewState& chunk_view) {
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

  ImGui::Checkbox("Pause sampling", &state.profile_sampling_paused);
  ImGui::SameLine();
  if (ImGui::Button("Reset timing stats")) {
    core::reset_global_timing_stats();
    clear_profile_frame_samples(state);
    clear_queue_profile(state.server_queue_profile);
    clear_queue_profile(state.client_queue_profile);
    clear_snapshot_profile(state);
    if (sched::Scheduler* scheduler = server.world_scheduler(); scheduler != nullptr) {
      scheduler->reset_pressure_stats();
    }
    world_jobs.reset_pressure_stats();
  }
  ImGui::SliderInt(
      "Rolling samples",
      &state.profile_frame_window_size,
      30,
      static_cast<int>(DebugMenuState::ProfileFrameHistoryCapacity));
  ImGui::SliderInt("Max event rows", &state.profile_max_event_rows, 4, 32);

  ImGui::SeparatorText("Frame timeline");
  if (state.profile_frame_sample_count == 0U) {
    ImGui::TextUnformatted("No frame samples recorded yet.");
  } else {
    std::array<f32, DebugMenuState::ProfileFrameHistoryCapacity> plot_samples{};
    const u32 requested_window = static_cast<u32>(std::clamp(
        state.profile_frame_window_size,
        1,
        static_cast<int>(DebugMenuState::ProfileFrameHistoryCapacity)));
    u32 sample_count = collect_frame_window(
        state.profile_frame_delta_ms,
        state.profile_frame_sample_index,
        state.profile_frame_sample_count,
        requested_window,
        plot_samples);
    ImGui::PlotLines(
        "Frame delta",
        plot_samples.data(),
        static_cast<int>(sample_count),
        0,
        nullptr,
        0.0F,
        50.0F,
        ImVec2{0.0F, 58.0F});
    sample_count = collect_frame_window(
        state.profile_frame_work_ms,
        state.profile_frame_sample_index,
        state.profile_frame_sample_count,
        requested_window,
        plot_samples);
    ImGui::PlotLines(
        "CPU frame work",
        plot_samples.data(),
        static_cast<int>(sample_count),
        0,
        nullptr,
        0.0F,
        50.0F,
        ImVec2{0.0F, 58.0F});
  }

  render_frame_breakdown(state, frame_info);

  if (state.profile_frame_sample_count > 0U) {
    std::array<f32, DebugMenuState::ProfileFrameHistoryCapacity> histogram_samples{};
    const u32 requested_window = static_cast<u32>(std::clamp(
        state.profile_frame_window_size,
        1,
        static_cast<int>(DebugMenuState::ProfileFrameHistoryCapacity)));
    u32 histogram_count = collect_frame_window(
        state.profile_frame_delta_ms,
        state.profile_frame_sample_index,
        state.profile_frame_sample_count,
        requested_window,
        histogram_samples);
    ImGui::SeparatorText("Timing Distribution");
    render_timing_histogram("Frame delta histogram", histogram_samples, histogram_count);
    histogram_count = collect_frame_window(
        state.profile_frame_work_ms,
        state.profile_frame_sample_index,
        state.profile_frame_sample_count,
        requested_window,
        histogram_samples);
    render_timing_histogram("CPU frame work histogram", histogram_samples, histogram_count);
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
      if (core::perf_metric_group(metric) != core::PerfEventGroup::Worker) {
        continue;
      }
      const core::TimingMetricSnapshot snapshot = core::global_timing_snapshot(metric);
      render_percentile_cells(
          core::perf_metric_name(metric),
          snapshot.percentiles,
          snapshot.latest_ms,
          snapshot.max_ms);
    }
    ImGui::EndTable();
  }

  render_worst_events(
      "Worst Frame Samples",
      core::PerfEventGroup::Frame,
      state.profile_max_event_rows);
  render_worst_events(
      "Worst Worker Jobs",
      core::PerfEventGroup::Worker,
      state.profile_max_event_rows);
  render_worst_events(
      "Worst Zone Samples",
      core::PerfEventGroup::Zone,
      state.profile_max_event_rows);

  render_queue_pressure(state, world_jobs, server);
  render_chunk_workload(frame_info, server, renderer, chunk_view);
  render_snapshot_costs(state);
  render_lighting_profile();
  render_gpu_render_metrics(renderer);

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
    const entity::EntityRegistry& entity_registry,
    sched::Scheduler& world_jobs,
    const render::OpenGLRenderer& renderer,
    const ChunkViewState& chunk_view) {
  DebugMenuResult result;
  core::set_global_timing_paused(state.profile_sampling_paused);
  record_profile_frame_sample(state, frame_info);
  if (core::profiling_enabled() && !state.profile_sampling_paused) {
    if (const sched::Scheduler* scheduler = server.world_scheduler(); scheduler != nullptr) {
      record_queue_profile_sample(
          state.server_queue_profile,
          scheduler->pressure_snapshot());
    }
    record_queue_profile_sample(
        state.client_queue_profile,
        world_jobs.pressure_snapshot());
    record_snapshot_profile_sample(state);
  }
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
      render_entities_tab(state, server, entity_registry);
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
      render_profiling_tab(state, frame_info, server, world_jobs, renderer, chunk_view);
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
