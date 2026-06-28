#include "debug_menu.hpp"

#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"
#include "arv/core/string.hpp"
#include "arv/world/worldgen/chunk_generator.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#include <imgui.h>
#include <imgui_stdlib.h>

namespace arv::desktop {
namespace {

[[nodiscard]] world::WorldRuntime* active_session_world(
    server::ServerRuntime& server) noexcept {
  const server::ServerSession* session = server.sessions().first();
  if (session != nullptr) {
    if (world::WorldRuntime* runtime = server.world_runtime(session->world_id);
        runtime != nullptr) {
      return runtime;
    }
  }
  return &server.voxel_world();
}

[[nodiscard]] const world::WorldRuntime* active_session_world(
    const server::ServerRuntime& server) noexcept {
  const server::ServerSession* session = server.sessions().first();
  if (session != nullptr) {
    if (const world::WorldRuntime* runtime = server.world_runtime(session->world_id);
        runtime != nullptr) {
      return runtime;
    }
  }
  return &server.voxel_world();
}

[[nodiscard]] world::WorldRuntime* active_client_world(
    server::ServerRuntime& server,
    const client::ClientRuntime& client_runtime) noexcept {
  if (world::WorldRuntime* runtime =
          server.world_runtime(client_runtime.camera().world_id());
      runtime != nullptr) {
    return runtime;
  }
  return active_session_world(server);
}

struct LocalPlayerDebugContext {
  const server::ServerSession* session{};
  world::WorldRuntime* world{};
  player::PlayerState* player{};
  std::optional<PlayerID> player_id{};
  std::optional<Vec3f> position{};
};

[[nodiscard]] LocalPlayerDebugContext local_player_context(
    server::ServerRuntime& server) noexcept {
  LocalPlayerDebugContext context;
  context.session = server.sessions().first();
  if (context.session == nullptr) {
    return context;
  }
  context.world = server.world_runtime(context.session->world_id);
  if (context.world == nullptr) {
    return context;
  }
  context.player = context.world->find_player(context.session->player_id);
  context.player_id = context.session->player_id;
  if (context.player != nullptr) {
    context.position = context.player->position;
  }
  return context;
}

[[nodiscard]] LocalPlayerDebugContext local_player_context(
    server::ServerRuntime& server,
    const client::ClientRuntime& client_runtime) noexcept {
  LocalPlayerDebugContext context;
  context.session = server.sessions().first();
  context.world = server.world_runtime(client_runtime.camera().world_id());
  const bool using_client_world = context.world != nullptr;
  if (context.world == nullptr) {
    context.world = active_session_world(server);
  }

  if (using_client_world) {
    context.player_id = client_runtime.local_player_id();
    if (context.player_id.has_value()) {
      context.player = context.world->find_player(context.player_id.value());
      context.position = client_runtime.player_position();
    }
  }

  if (context.player == nullptr && context.session != nullptr && context.world != nullptr &&
      context.session->world_id == context.world->id()) {
    context.player = context.world->find_player(context.session->player_id);
    if (!context.player_id.has_value()) {
      context.player_id = context.session->player_id;
    }
  }

  if (!context.position.has_value() && context.player != nullptr) {
    context.position = context.player->position;
  }
  return context;
}

[[nodiscard]] player::PlayerState* local_player_state(
    server::ServerRuntime& server) noexcept {
  return local_player_context(server).player;
}

[[nodiscard]] std::optional<Vec3f> debug_entity_spawn_position(
    const DebugMenuState& state,
    const LocalPlayerDebugContext& player_context) noexcept {
  if (!state.entity_spawn_at_player) {
    return Vec3f{
        state.entity_spawn_position[0],
        state.entity_spawn_position[1],
        state.entity_spawn_position[2]};
  }
  if (!player_context.position.has_value()) {
    return std::nullopt;
  }
  const Vec3f& anchor = player_context.position.value();
  return Vec3f{
      anchor.x + state.entity_spawn_offset[0],
      anchor.y + state.entity_spawn_offset[1],
      anchor.z + state.entity_spawn_offset[2]};
}

[[nodiscard]] std::string biome_display_label(
    const world::worldgen::BiomeConfig& biome) {
  if (biome.display_name.empty() || biome.display_name == biome.id) {
    return biome.id;
  }
  return biome.display_name + " (" + biome.id + ")";
}

[[nodiscard]] const std::string& current_biome_label(
    DebugMenuState& state,
    const world::WorldRuntime& runtime,
    Vec3f position) {
  auto cache_label = [&](std::string label) -> const std::string& {
    state.current_biome_label = std::move(label);
    state.current_biome_label_valid = true;
    return state.current_biome_label;
  };

  const world::WorldGenerationConfig* config = runtime.generation_config();
  if (config == nullptr || config->resolved_dimension == nullptr) {
    state.current_biome_dimension = nullptr;
    return cache_label("Unavailable");
  }

  const i32 world_x = static_cast<i32>(std::floor(position.x));
  const i32 world_z = static_cast<i32>(std::floor(position.z));
  const auto* dimension = config->resolved_dimension.get();
  const u64 seed = runtime.generation_seed();
  if (state.current_biome_label_valid &&
      state.current_biome_world_x == world_x &&
      state.current_biome_world_z == world_z &&
      state.current_biome_seed == seed &&
      state.current_biome_dimension == dimension) {
    return state.current_biome_label;
  }

  state.current_biome_world_x = world_x;
  state.current_biome_world_z = world_z;
  state.current_biome_seed = seed;
  state.current_biome_dimension = dimension;

  const ChunkCoord coord{floor_div(world_x, ChunkSize), floor_div(world_z, ChunkSize)};
  const world::worldgen::TerrainProfile profile =
      world::worldgen::generate_terrain_profile(
          *dimension,
          seed,
          coord);
  const usize column =
      voxel::local_column_index({floor_mod(world_x, ChunkSize), floor_mod(world_z, ChunkSize)});
  const usize biome_index = profile.biome_by_column[column];
  if (biome_index >= dimension->biomes.size()) {
    return cache_label("Unknown");
  }
  return cache_label(biome_display_label(dimension->biomes[biome_index].raw));
}

[[nodiscard]] const std::string& nearest_different_biome_label(
    DebugMenuState& state,
    const world::WorldRuntime& runtime,
    Vec3f position) {
  auto cache_label = [&](std::string label) -> const std::string& {
    state.nearest_biome_label = std::move(label);
    state.nearest_biome_label_valid = true;
    return state.nearest_biome_label;
  };

  const world::WorldGenerationConfig* config = runtime.generation_config();
  if (config == nullptr || config->resolved_dimension == nullptr) {
    state.nearest_biome_dimension = nullptr;
    return cache_label("Unavailable");
  }

  const i32 world_x = static_cast<i32>(std::floor(position.x));
  const i32 world_z = static_cast<i32>(std::floor(position.z));
  const ChunkCoord origin_chunk{floor_div(world_x, ChunkSize), floor_div(world_z, ChunkSize)};
  const auto* dimension = config->resolved_dimension.get();
  const u64 seed = runtime.generation_seed();

  const world::worldgen::TerrainProfile origin_profile =
      world::worldgen::generate_terrain_profile(
          *dimension,
          seed,
          origin_chunk);
  const usize origin_column =
      voxel::local_column_index({floor_mod(world_x, ChunkSize), floor_mod(world_z, ChunkSize)});
  const usize source_biome_index = origin_profile.biome_by_column[origin_column];

  if (state.nearest_biome_label_valid &&
      state.nearest_biome_chunk_x == origin_chunk.x &&
      state.nearest_biome_chunk_z == origin_chunk.z &&
      state.nearest_biome_seed == seed &&
      state.nearest_biome_dimension == dimension &&
      state.nearest_biome_source_index == source_biome_index) {
    return state.nearest_biome_label;
  }

  state.nearest_biome_chunk_x = origin_chunk.x;
  state.nearest_biome_chunk_z = origin_chunk.z;
  state.nearest_biome_seed = seed;
  state.nearest_biome_dimension = dimension;
  state.nearest_biome_source_index = source_biome_index;

  if (source_biome_index >= dimension->biomes.size()) {
    return cache_label("Unknown");
  }

  struct Candidate {
    usize biome_index{};
    i32 world_x{};
    i32 world_z{};
    i64 distance_sq{std::numeric_limits<i64>::max()};
  };

  constexpr i32 MaxRadiusChunks = 64;
  constexpr std::array<ChunkLocalColumn, 5> SampleColumns{{
      {ChunkSize / 2, ChunkSize / 2},
      {0, 0},
      {ChunkSize - 1, 0},
      {0, ChunkSize - 1},
      {ChunkSize - 1, ChunkSize - 1},
  }};

  Candidate best;
  auto inspect_sample = [&](ChunkCoord coord, const world::worldgen::TerrainProfile& profile, ChunkLocalColumn local) {
    const usize column = voxel::local_column_index(local);
    const usize biome_index = profile.biome_by_column[column];
    if (biome_index == source_biome_index || biome_index >= dimension->biomes.size()) {
      return;
    }
    const i32 sample_x = coord.x * ChunkSize + local.x;
    const i32 sample_z = coord.z * ChunkSize + local.z;
    const i64 dx = static_cast<i64>(sample_x) - world_x;
    const i64 dz = static_cast<i64>(sample_z) - world_z;
    const i64 distance_sq = dx * dx + dz * dz;
    if (distance_sq < best.distance_sq) {
      best = {biome_index, sample_x, sample_z, distance_sq};
    }
  };

  auto inspect_chunk = [&](ChunkCoord coord) {
    const world::worldgen::TerrainProfile profile =
        coord == origin_chunk
            ? origin_profile
            : world::worldgen::generate_terrain_profile(*dimension, seed, coord);
    if (coord == origin_chunk) {
      for (usize column = 0; column < voxel::ChunkArea; ++column) {
        inspect_sample(coord, profile, voxel::local_column(column));
      }
      return;
    }
    for (const ChunkLocalColumn local : SampleColumns) {
      inspect_sample(coord, profile, local);
    }
  };

  inspect_chunk(origin_chunk);
  for (i32 radius = 1; radius <= MaxRadiusChunks && best.distance_sq == std::numeric_limits<i64>::max(); ++radius) {
    for (i32 dz = -radius; dz <= radius; ++dz) {
      inspect_chunk({origin_chunk.x - radius, origin_chunk.z + dz});
      inspect_chunk({origin_chunk.x + radius, origin_chunk.z + dz});
    }
    for (i32 dx = -radius + 1; dx <= radius - 1; ++dx) {
      inspect_chunk({origin_chunk.x + dx, origin_chunk.z - radius});
      inspect_chunk({origin_chunk.x + dx, origin_chunk.z + radius});
    }
  }

  if (best.distance_sq == std::numeric_limits<i64>::max()) {
    return cache_label("None within 1024 blocks");
  }

  const double distance = std::sqrt(static_cast<double>(best.distance_sq));
  std::ostringstream out;
  out << biome_display_label(dimension->biomes[best.biome_index].raw)
      << " at "
      << best.world_x
      << ", "
      << best.world_z
      << " (~"
      << static_cast<i32>(std::round(distance))
      << " blocks)";
  return cache_label(out.str());
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
    if (contains_ignore_ascii_case(item->name, filter) ||
        contains_ignore_ascii_case(item->display_name, filter)) {
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
    if (contains_ignore_ascii_case(type.name, filter) ||
        contains_ignore_ascii_case(type.display_name, filter) ||
        contains_ignore_ascii_case(type.category, filter) ||
        contains_ignore_ascii_case(type.model_asset, filter)) {
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

[[nodiscard]] const char* easing_label(client::ItemAnimationEasing easing) noexcept {
  switch (easing) {
    case client::ItemAnimationEasing::Linear:
      return "linear";
    case client::ItemAnimationEasing::Step:
      return "step";
    case client::ItemAnimationEasing::EaseIn:
      return "ease_in";
    case client::ItemAnimationEasing::EaseOut:
      return "ease_out";
    case client::ItemAnimationEasing::EaseInOut:
      return "ease_in_out";
  }
  return "linear";
}

[[nodiscard]] client::ItemAnimationEasing easing_from_index(int index) noexcept {
  switch (index) {
    case 1:
      return client::ItemAnimationEasing::Step;
    case 2:
      return client::ItemAnimationEasing::EaseIn;
    case 3:
      return client::ItemAnimationEasing::EaseOut;
    case 4:
      return client::ItemAnimationEasing::EaseInOut;
    default:
      return client::ItemAnimationEasing::Linear;
  }
}

[[nodiscard]] int easing_index(client::ItemAnimationEasing easing) noexcept {
  switch (easing) {
    case client::ItemAnimationEasing::Linear:
      return 0;
    case client::ItemAnimationEasing::Step:
      return 1;
    case client::ItemAnimationEasing::EaseIn:
      return 2;
    case client::ItemAnimationEasing::EaseOut:
      return 3;
    case client::ItemAnimationEasing::EaseInOut:
      return 4;
  }
  return 0;
}

void sort_item_animation_keyframes(
    std::vector<client::ItemAnimationKeyframeVec3>& frames) {
  std::sort(
      frames.begin(),
      frames.end(),
      [](const client::ItemAnimationKeyframeVec3& lhs,
         const client::ItemAnimationKeyframeVec3& rhs) {
        return lhs.time_seconds < rhs.time_seconds;
      });
}

void append_json_string(std::ostringstream& output, std::string_view value) {
  output << '"';
  for (const char ch : value) {
    switch (ch) {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        output << ch;
        break;
    }
  }
  output << '"';
}

void append_vec3_json(std::ostringstream& output, Vec3f value) {
  output << '['
         << value.x << ", "
         << value.y << ", "
         << value.z << ']';
}

void append_keyframes_json(
    std::ostringstream& output,
    const std::vector<client::ItemAnimationKeyframeVec3>& frames,
    int indent) {
  const std::string base(static_cast<usize>(indent), ' ');
  const std::string row(static_cast<usize>(indent + 2), ' ');
  output << "[\n";
  for (usize index = 0; index < frames.size(); ++index) {
    const client::ItemAnimationKeyframeVec3& frame = frames[index];
    output << row << "{ \"time\": " << frame.time_seconds << ", \"value\": ";
    append_vec3_json(output, frame.value);
    if (frame.easing != client::ItemAnimationEasing::Linear) {
      output << ", \"easing\": ";
      append_json_string(output, easing_label(frame.easing));
    }
    output << " }";
    if (index + 1U < frames.size()) {
      output << ',';
    }
    output << '\n';
  }
  output << base << ']';
}

void append_channel_json(
    std::ostringstream& output,
    const client::ItemAnimationTransformChannel& channel) {
  output << "{\n";
  output << "          \"position\": ";
  append_keyframes_json(output, channel.position, 10);
  output << ",\n          \"rotation_degrees\": ";
  append_keyframes_json(output, channel.rotation_degrees, 10);
  output << ",\n          \"scale\": ";
  append_keyframes_json(output, channel.scale, 10);
  output << "\n        }";
}

[[nodiscard]] std::string serialize_item_animation_library_json(
    const client::ItemAnimationLibrary& library) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(3);
  output << "{\n  \"animations\": [\n";
  const std::span<const client::ItemAnimationClip> clips = library.clips();
  for (usize index = 0; index < clips.size(); ++index) {
    const client::ItemAnimationClip& clip = clips[index];
    output << "    {\n      \"name\": ";
    append_json_string(output, clip.name);
    output << ",\n      \"duration_seconds\": " << clip.duration_seconds
           << ",\n      \"loop\": " << (clip.loop ? "true" : "false")
           << ",\n      \"blend_in_seconds\": " << clip.blend_in_seconds
           << ",\n      \"blend_out_seconds\": " << clip.blend_out_seconds
           << ",\n      \"channels\": {\n        \"first_person\": ";
    append_channel_json(output, clip.first_person);
    output << "\n      }\n    }";
    if (index + 1U < clips.size()) {
      output << ',';
    }
    output << '\n';
  }
  output << "  ]\n}\n";
  return output.str();
}

[[nodiscard]] client::ItemAnimationClip* selected_item_animation_clip(
    DebugMenuState& state,
    client::ItemAnimationLibrary& library) noexcept {
  if (library.clip_count() == 0U) {
    return nullptr;
  }
  client::ItemAnimationClip* clip = library.find(state.selected_item_animation_name);
  if (clip != nullptr) {
    return clip;
  }
  clip = &library.clips().front();
  state.selected_item_animation_name = clip->name;
  return clip;
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
    const sched::JobSystemPressureSnapshot& snapshot) noexcept {
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

[[nodiscard]] const char* vehicle_physics_label(
    entity::VehiclePhysicsKind physics) noexcept {
  switch (physics) {
    case entity::VehiclePhysicsKind::LivingMount:
      return "living_mount";
    case entity::VehiclePhysicsKind::SurfaceWaterVehicle:
      return "surface_water";
    case entity::VehiclePhysicsKind::GroundVehicle:
      return "ground";
    case entity::VehiclePhysicsKind::Aircraft:
      return "aircraft";
    case entity::VehiclePhysicsKind::Submersible:
      return "submersible";
  }
  return "unknown";
}

[[nodiscard]] const char* crash_severity_label(
    entity::CrashSeverity severity) noexcept {
  switch (severity) {
    case entity::CrashSeverity::None:
      return "none";
    case entity::CrashSeverity::Bump:
      return "bump";
    case entity::CrashSeverity::HardImpact:
      return "hard";
    case entity::CrashSeverity::Ejection:
      return "ejection";
    case entity::CrashSeverity::Destruction:
      return "destruction";
  }
  return "unknown";
}

[[nodiscard]] const char* vehicle_correction_reason_label(
    protocol::VehicleCorrectionReason reason) noexcept {
  switch (reason) {
    case protocol::VehicleCorrectionReason::InvalidNumber:
      return "invalid_number";
    case protocol::VehicleCorrectionReason::StalePacket:
      return "stale";
    case protocol::VehicleCorrectionReason::NotMounted:
      return "not_mounted";
    case protocol::VehicleCorrectionReason::NotController:
      return "not_controller";
    case protocol::VehicleCorrectionReason::TooFast:
      return "too_fast";
    case protocol::VehicleCorrectionReason::TooMuchAcceleration:
      return "too_much_accel";
    case protocol::VehicleCorrectionReason::TooMuchAngularVelocity:
      return "too_much_angular";
    case protocol::VehicleCorrectionReason::TooMuchVerticalVelocity:
      return "too_much_vertical";
    case protocol::VehicleCorrectionReason::Collision:
      return "collision";
    case protocol::VehicleCorrectionReason::MissingGroundSupport:
      return "missing_ground";
    case protocol::VehicleCorrectionReason::MissingWaterSupport:
      return "missing_water";
    case protocol::VehicleCorrectionReason::AircraftEnteredUnloadedChunk:
      return "aircraft_unloaded_chunk";
    case protocol::VehicleCorrectionReason::InvalidSubmersibleMedium:
      return "invalid_sub_medium";
    case protocol::VehicleCorrectionReason::Unknown:
      return "unknown";
  }
  return "unknown";
}

[[nodiscard]] std::string vehicle_move_flags_label(
    protocol::VehicleMoveFlags flags) {
  std::string out;
  auto append = [&](protocol::VehicleMoveFlags flag, std::string_view label) {
    if (!protocol::has_vehicle_move_flag(flags, flag)) {
      return;
    }
    if (!out.empty()) {
      out += ", ";
    }
    out += label;
  };
  append(protocol::VehicleMoveFlags::Jump, "jump");
  append(protocol::VehicleMoveFlags::Brake, "brake");
  append(protocol::VehicleMoveFlags::Sprint, "sprint");
  append(protocol::VehicleMoveFlags::OnGround, "ground");
  append(protocol::VehicleMoveFlags::InWater, "water");
  return out.empty() ? std::string{"none"} : out;
}

void imgui_vec3_line(const char* label, Vec3f value) {
  ImGui::Text(
      "%s: %.2f, %.2f, %.2f",
      label,
      static_cast<double>(value.x),
      static_cast<double>(value.y),
      static_cast<double>(value.z));
}

void render_vehicle_debug_overlay(
    const DebugMenuState& state,
    const server::ServerRuntime& server,
    const client::ClientRuntime& client) {
  if (!state.show_vehicle_debug_overlay) {
    return;
  }
  const std::optional<client::VehicleDebugTelemetry> telemetry =
      client.vehicle_debug_telemetry();
  if (!telemetry.has_value()) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2{14.0F, 72.0F}, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.68F);
  constexpr ImGuiWindowFlags Flags =
      ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoNav |
      ImGuiWindowFlags_NoInputs;
  if (!ImGui::Begin("Vehicle Debug Overlay", nullptr, Flags)) {
    ImGui::End();
    return;
  }

  const client::VehicleDebugTelemetry& info = telemetry.value();
  ImGui::Text(
      "Vehicle %s #%u (%s)",
      info.definition.c_str(),
      info.vehicle_id.value,
      vehicle_physics_label(info.physics));
  ImGui::Text(
      "Speed %.2f  horizontal %.2f",
      static_cast<double>(info.speed),
      static_cast<double>(info.horizontal_speed));
  imgui_vec3_line("Velocity", info.velocity);
  imgui_vec3_line("Acceleration", info.acceleration);
  imgui_vec3_line("Angular velocity", info.angular_velocity);
  ImGui::Text(
      "Input throttle %.2f  steer %.2f  dt %.3f",
      static_cast<double>(info.throttle),
      static_cast<double>(info.steer),
      static_cast<double>(info.dt_seconds));
  const std::string flags = vehicle_move_flags_label(info.flags);
  ImGui::Text(
      "Client tick %llu  seq %u%s  flags %s",
      static_cast<unsigned long long>(info.client_tick),
      info.last_sent_sequence,
      info.sent_this_tick ? " sent" : "",
      flags.c_str());
  ImGui::Text(
      "Client hit %s  impact %.2f  memory %u",
      info.hit_collision_this_tick ? "yes" : "no",
      static_cast<double>(info.collision_normal_impact_speed),
      info.impact_memory_ticks);
  if (info.hit_collision_this_tick) {
    imgui_vec3_line("Client normal", info.collision_normal);
  }

  if (info.has_crash_tuning) {
    ImGui::Separator();
    ImGui::Text(
        "Thresholds bump %.1f  hard %.1f  eject %.1f  destroy %.1f",
        static_cast<double>(info.crash.bump_threshold),
        static_cast<double>(info.crash.hard_impact_threshold),
        static_cast<double>(info.crash.ejection_threshold),
        static_cast<double>(info.crash.destruction_threshold));
    ImGui::Text(
        "Direct dot min %.2f  retention %.2f  cooldown %u",
        static_cast<double>(info.crash.min_direct_impact_dot),
        static_cast<double>(info.crash.velocity_retention),
        info.crash.event_cooldown_ticks);
  }

  const std::optional<server::VehicleCrashDebugInfo> crash =
      server.last_vehicle_crash_debug(info.vehicle_id);
  ImGui::Separator();
  if (!crash.has_value()) {
    ImGui::TextUnformatted("Server crash: no move processed");
  } else {
    const server::VehicleCrashDebugInfo& server_info = crash.value();
    ImGui::Text(
        "Server tick %llu  seq %u  %s",
        static_cast<unsigned long long>(server_info.server_tick),
        server_info.sequence,
        server_info.accepted_move ? "accepted" : "rejected");
    ImGui::Text(
        "Reason %s  correction %s",
        vehicle_correction_reason_label(server_info.validation_reason),
        server_info.sent_correction ? "yes" : "no");
    ImGui::Text(
        "Sweeps prev %s  proposed %s  final-solid %s  contact %s",
        server_info.previous_velocity_sweep_hit_solid ? "yes" : "no",
        server_info.proposed_sweep_hit_solid ? "yes" : "no",
        server_info.final_aabb_overlaps_solid ? "yes" : "no",
        server_info.had_collision_contact ? "yes" : "no");
    ImGui::Text(
        "Severity %s  impact %.2f  dot %.2f",
        crash_severity_label(server_info.severity),
        static_cast<double>(server_info.normal_impact_speed),
        static_cast<double>(server_info.direct_impact_dot));
    if (server_info.had_collision_contact) {
      imgui_vec3_line("Server normal", server_info.contact_normal);
      imgui_vec3_line("Pre-impact velocity", server_info.pre_impact_velocity);
    }
    imgui_vec3_line("Accepted velocity", server_info.accepted_velocity);
    ImGui::Text(
        "Outcome applied %s  cooldown %s  damage %.1f  ejected %u  destroyed %s  explosion %s",
        server_info.consequence_applied ? "yes" : "no",
        server_info.cooldown_suppressed ? "yes" : "no",
        static_cast<double>(server_info.damage_applied),
        server_info.ejected_occupants,
        server_info.vehicle_destroyed ? "yes" : "no",
        server_info.explosion_broadcast ? "yes" : "no");
    if (server_info.cooldown_suppressed) {
      ImGui::Text(
          "Cooldown until tick %llu",
          static_cast<unsigned long long>(server_info.cooldown_until));
    }
  }

  ImGui::End();
}

void render_space_debug_overlay(
    const DebugMenuState& state,
    const client::ClientRuntime& client) {
  if (!state.show_space_debug_overlay) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2{14.0F, 270.0F}, ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.68F);
  constexpr ImGuiWindowFlags Flags =
      ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoNav |
      ImGuiWindowFlags_NoInputs;
  if (!ImGui::Begin("Space Debug Overlay", nullptr, Flags)) {
    ImGui::End();
    return;
  }

  const client::CameraShell& camera = client.camera();
  ImGui::Text("World #%u", camera.world_id().value);
  imgui_vec3_line("Position", camera.position());
  imgui_vec3_line("Velocity", client.player_velocity());
  ImGui::Text("Altitude Y %.2f", static_cast<double>(camera.position().y));
  ImGui::Text("Environment %s", client.space_environment() ? "space" : "planet");

  const auto bodies = client.space_bodies();
  if (bodies.empty()) {
    ImGui::TextUnformatted("Nearest body: none");
    ImGui::End();
    return;
  }

  const protocol::ClientboundSpaceBodyView* nearest = nullptr;
  f32 nearest_distance_squared = std::numeric_limits<f32>::max();
  for (const protocol::ClientboundSpaceBodyView& body : bodies) {
    const f32 current_distance_squared =
        distance_squared(camera.position(), body.position);
    if (current_distance_squared < nearest_distance_squared) {
      nearest = &body;
      nearest_distance_squared = current_distance_squared;
    }
  }
  if (nearest != nullptr) {
    const f32 distance = std::sqrt(nearest_distance_squared);
    ImGui::Text("Nearest body %s", nearest->display_name.c_str());
    ImGui::Text(
        "Distance %.1f  entry %.1f%s",
        static_cast<double>(distance),
        static_cast<double>(nearest->entry_radius),
        nearest->entry_enabled ? "" : " disabled");
  }
  ImGui::End();
}

void render_player_tab(
    DebugMenuState& state,
    server::ServerRuntime& server,
    client::ClientRuntime& client) {
  LocalPlayerDebugContext player_context = local_player_context(server, client);
  player::PlayerState* player = player_context.player;
  if (player == nullptr) {
    ImGui::TextUnformatted("No local player session.");
    return;
  }
  world::WorldRuntime& player_world = *player_context.world;
  const Vec3f player_position = player_context.position.value_or(player->position);

  ImGui::Text("Player ID: %u", player->player_id.value);
  ImGui::Text("World: #%u", player_world.id().value);
  ImGui::Text(
      "Position: %.2f, %.2f, %.2f",
      static_cast<double>(player_position.x),
      static_cast<double>(player_position.y),
      static_cast<double>(player_position.z));
  const std::string& biome_label =
      current_biome_label(state, player_world, player_position);
  ImGui::Text("Biome: %s", biome_label.c_str());
  const world::WorldGenerationConfig* generation_config =
      player_world.generation_config();
  const i32 biome_chunk_x =
      floor_div(static_cast<i32>(std::floor(player_position.x)), ChunkSize);
  const i32 biome_chunk_z =
      floor_div(static_cast<i32>(std::floor(player_position.z)), ChunkSize);
  const void* biome_dimension =
      generation_config == nullptr ? nullptr : generation_config->resolved_dimension.get();
  bool nearest_biome_cache_current =
      state.nearest_biome_label_valid &&
      state.nearest_biome_chunk_x == biome_chunk_x &&
      state.nearest_biome_chunk_z == biome_chunk_z &&
      state.nearest_biome_seed == player_world.generation_seed() &&
      state.nearest_biome_dimension == biome_dimension;
  if (ImGui::Button("Locate nearest different biome")) {
    (void)nearest_different_biome_label(state, player_world, player_position);
    nearest_biome_cache_current = true;
  }
  ImGui::Text(
      "Nearest biome: %s",
      nearest_biome_cache_current ? state.nearest_biome_label.c_str() : "Not located");
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
  ImGui::Checkbox("Vehicle debug overlay", &state.show_vehicle_debug_overlay);
  ImGui::Checkbox("Space debug overlay", &state.show_space_debug_overlay);
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

  const server::ServerSession* session = server.sessions().first();
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

void render_item_animation_keyframes(
    const char* label,
    std::vector<client::ItemAnimationKeyframeVec3>& frames,
    Vec3f default_value,
    Seconds duration_seconds) {
  ImGui::PushID(label);
  ImGui::SeparatorText(label);
  bool changed = false;
  int remove_index = -1;
  if (ImGui::BeginTable(
          "Keyframes",
          6,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 82.0F);
    ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, 86.0F);
    ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, 86.0F);
    ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthFixed, 86.0F);
    ImGui::TableSetupColumn("Ease", ImGuiTableColumnFlags_WidthFixed, 116.0F);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 52.0F);
    ImGui::TableHeadersRow();
    for (usize index = 0; index < frames.size(); ++index) {
      client::ItemAnimationKeyframeVec3& frame = frames[index];
      ImGui::PushID(static_cast<int>(index));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      float time = frame.time_seconds;
      if (ImGui::DragFloat("##time", &time, 0.005F, 0.0F, duration_seconds, "%.3f")) {
        frame.time_seconds = std::clamp(time, 0.0F, duration_seconds);
        changed = true;
      }
      ImGui::TableNextColumn();
      changed = ImGui::DragFloat("##x", &frame.value.x, 0.01F, -2.0F, 2.0F, "%.3f") || changed;
      ImGui::TableNextColumn();
      changed = ImGui::DragFloat("##y", &frame.value.y, 0.01F, -2.0F, 2.0F, "%.3f") || changed;
      ImGui::TableNextColumn();
      changed = ImGui::DragFloat("##z", &frame.value.z, 0.01F, -2.0F, 2.0F, "%.3f") || changed;
      ImGui::TableNextColumn();
      int ease = easing_index(frame.easing);
      if (ImGui::Combo("##ease", &ease, "linear\0step\0ease_in\0ease_out\0ease_in_out\0")) {
        frame.easing = easing_from_index(ease);
        changed = true;
      }
      ImGui::TableNextColumn();
      if (ImGui::SmallButton("Del")) {
        remove_index = static_cast<int>(index);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  if (remove_index >= 0) {
    frames.erase(frames.begin() + remove_index);
    changed = true;
  }

  if (ImGui::Button("Add keyframe")) {
    const Seconds next_time =
        frames.empty()
            ? 0.0F
            : std::min(frames.back().time_seconds + 0.05F, duration_seconds);
    frames.push_back({next_time, default_value, client::ItemAnimationEasing::Linear});
    changed = true;
  }

  if (changed) {
    sort_item_animation_keyframes(frames);
  }
  ImGui::PopID();
}

void render_item_animations_tab(
    DebugMenuState& state,
    client::ItemAnimationLibrary& library,
    client::HeldItemAnimationPlayer& player,
    Seconds now_seconds,
    const std::filesystem::path& export_path) {
  if (library.clip_count() == 0U) {
    ImGui::TextUnformatted("No item animations are loaded.");
    return;
  }

  client::ItemAnimationClip* clip = selected_item_animation_clip(state, library);
  if (clip == nullptr) {
    ImGui::TextUnformatted("No item animation is selected.");
    return;
  }

  const char* preview = state.selected_item_animation_name.empty()
                            ? "Select animation"
                            : state.selected_item_animation_name.c_str();
  if (ImGui::BeginCombo("Animation", preview)) {
    for (client::ItemAnimationClip& candidate : library.clips()) {
      const bool selected = candidate.name == state.selected_item_animation_name;
      if (ImGui::Selectable(candidate.name.c_str(), selected)) {
        state.selected_item_animation_name = candidate.name;
        clip = &candidate;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  ImGui::SeparatorText("Clip");
  float duration = clip->duration_seconds;
  if (ImGui::DragFloat("Duration", &duration, 0.005F, 0.01F, 5.0F, "%.3f")) {
    clip->duration_seconds = std::max(duration, 0.01F);
    for (auto* frames : {
             &clip->first_person.position,
             &clip->first_person.rotation_degrees,
             &clip->first_person.scale,
         }) {
      for (client::ItemAnimationKeyframeVec3& frame : *frames) {
        frame.time_seconds = std::min(frame.time_seconds, clip->duration_seconds);
      }
    }
  }
  ImGui::Checkbox("Loop", &clip->loop);
  float blend_in = clip->blend_in_seconds;
  float blend_out = clip->blend_out_seconds;
  if (ImGui::DragFloat("Blend in", &blend_in, 0.005F, 0.0F, 1.0F, "%.3f")) {
    clip->blend_in_seconds = std::max(blend_in, 0.0F);
  }
  if (ImGui::DragFloat("Blend out", &blend_out, 0.005F, 0.0F, 1.0F, "%.3f")) {
    clip->blend_out_seconds = std::max(blend_out, 0.0F);
  }

  if (ImGui::Button("Preview")) {
    player.play(*clip, now_seconds);
  }
  ImGui::SameLine();
  if (ImGui::Button("Stop")) {
    player.stop(now_seconds);
  }
  ImGui::SameLine();
  ImGui::Text(
      "Active: %s",
      player.active_clip() == nullptr ? "none" : player.active_clip()->name.c_str());

  render_item_animation_keyframes(
      "Position (X right, Y up, Z forward)",
      clip->first_person.position,
      {},
      clip->duration_seconds);
  render_item_animation_keyframes(
      "Rotation Degrees (pitch, yaw, roll)",
      clip->first_person.rotation_degrees,
      {},
      clip->duration_seconds);
  render_item_animation_keyframes(
      "Scale",
      clip->first_person.scale,
      {1.0F, 1.0F, 1.0F},
      clip->duration_seconds);

  ImGui::SeparatorText("Export");
  if (ImGui::Button("Generate JSON")) {
    state.item_animation_export_text = serialize_item_animation_library_json(library);
    state.item_animation_status = "Generated item animation JSON.";
  }
  ImGui::SameLine();
  if (ImGui::Button("Copy")) {
    if (state.item_animation_export_text.empty()) {
      state.item_animation_export_text = serialize_item_animation_library_json(library);
    }
    ImGui::SetClipboardText(state.item_animation_export_text.c_str());
    state.item_animation_status = "Copied item animation JSON to clipboard.";
  }
  ImGui::SameLine();
  if (ImGui::Button("Export file")) {
    state.item_animation_export_text = serialize_item_animation_library_json(library);
    std::ofstream output(export_path);
    if (output) {
      output << state.item_animation_export_text;
      state.item_animation_status = "Exported " + export_path.string();
    } else {
      state.item_animation_status = "Failed to write " + export_path.string();
    }
  }
  if (!state.item_animation_status.empty()) {
    ImGui::TextUnformatted(state.item_animation_status.c_str());
  }
  ImGui::InputTextMultiline(
      "JSON",
      &state.item_animation_export_text,
      ImVec2{0.0F, 180.0F},
      ImGuiInputTextFlags_ReadOnly);
}

void render_entities_tab(
    DebugMenuState& state,
    server::ServerRuntime& server,
    const client::ClientRuntime& client_runtime,
    const entity::EntityRegistry& entity_registry) {
  world::WorldRuntime& world = *active_client_world(server, client_runtime);
  const LocalPlayerDebugContext local_player =
      local_player_context(server, client_runtime);
  const std::vector<EntityID> entity_ids = world.entity_ids();
  ImGui::Text("Runtime entities: %zu", entity_ids.size());
  ImGui::Text("Target world: #%u", world.id().value);
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

  const std::optional<Vec3f> spawn_position =
      debug_entity_spawn_position(state, local_player);
  const bool can_spawn =
      selected_type != nullptr && spawn_position.has_value();
  if (!can_spawn) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Spawn entity")) {
    world::EntityCreateInfo info;
    info.type_id = selected_type->id;
    info.type_name = selected_type->name;
    info.position = spawn_position.value();
    info.persistent = state.entity_persistent;
    info.persistent_uuid = state.entity_persistent_uuid;
    info.replicated = state.entity_replicated;
    info.authored_components = selected_type->components;
    info.vehicle_definitions = server.vehicle_registry();
    const EntityID spawned = world.create_entity(std::move(info));
    state.selected_entity_id = std::to_string(spawned.value);
    state.console_log.emplace_back("Spawned entity " + state.selected_entity_id + " (" +
                                   selected_type->name + ").");
  }
  if (!can_spawn) {
    ImGui::EndDisabled();
  }
  if (state.entity_spawn_at_player && !local_player.position.has_value()) {
    ImGui::TextUnformatted(
        "No local player is available in the current client world for player-relative spawning.");
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

  const bool can_move_selected =
      selected_exists && !selected_is_player && spawn_position.has_value();
  if (!can_move_selected) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Move selected to spawn position")) {
    if (world.set_entity_position(selected_entity.value(), spawn_position.value())) {
      state.console_log.emplace_back("Moved entity " + state.selected_entity_id + ".");
    }
  }
  if (!can_move_selected) {
    ImGui::EndDisabled();
  }
  ImGui::SameLine();
  if (!selected_exists || selected_is_player) {
    ImGui::BeginDisabled();
  }
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
      const std::optional<ChunkSectionCoord> chunk = world.entity_chunk(entity_id);
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
    server::ServerRuntime& server,
    const client::ClientRuntime& client_runtime) {
  world::WorldRuntime& world_runtime = *active_client_world(server, client_runtime);
  const world::WorldClock& clock = world_runtime.clock();
  const world::CelestialState celestial = world::build_celestial_state(clock);
  const world::ClockTime24 time_24 = world::to_clock_time_24(celestial.day_fraction);

  ImGui::Text("Server tick: %llu", static_cast<unsigned long long>(server.tick_index()));
  ImGui::Text("World tick: %llu", static_cast<unsigned long long>(clock.world_tick));
  ImGui::Text("Base TPS: %u", server.base_tps());
  ImGui::Text("Visible chunks: %zu", frame_info.visible_chunk_count);
  ImGui::Text("Pending client chunks: %zu", frame_info.pending_client_chunk_count);
  ImGui::Text("Pending client unloads: %zu", frame_info.pending_client_unload_count);

  ImGui::SeparatorText("Time");
  ImGui::Text("Day time: %02u:%02u", time_24.hour, time_24.minute);
  ImGui::Text("Day fraction: %.3f", static_cast<double>(celestial.day_fraction));
  ImGui::Text(
      "Day tick: %u / %u",
      clock.day_time_tick,
      clock.day_length_ticks);
  ImGui::Text(
      "Sun direction: %.2f, %.2f, %.2f",
      static_cast<double>(celestial.sun_direction.x),
      static_cast<double>(celestial.sun_direction.y),
      static_cast<double>(celestial.sun_direction.z));
  ImGui::Text(
      "Daylight strength: %.2f",
      static_cast<double>(celestial.daylight_strength));
  ImGui::Text(
      "Star visibility: %.2f",
      static_cast<double>(celestial.star_visibility));

  float day_fraction_value = celestial.day_fraction;
  if (ImGui::SliderFloat("Time of day", &day_fraction_value, 0.0F, 0.999F, "%.3f")) {
    world_runtime.set_day_fraction(day_fraction_value);
  }

  int hour = static_cast<int>(time_24.hour);
  int minute = static_cast<int>(time_24.minute);
  bool time_input_changed = ImGui::InputInt("Hour", &hour);
  ImGui::SameLine();
  time_input_changed = ImGui::InputInt("Minute", &minute) || time_input_changed;
  if (time_input_changed) {
    hour = std::clamp(hour, 0, 23);
    minute = std::clamp(minute, 0, 59);
    world_runtime.set_day_time_tick(world::day_tick_from_hour_minute(
        static_cast<u32>(hour),
        static_cast<u32>(minute),
        clock.day_length_ticks));
  }

  bool daylight_cycle = clock.daylight_cycle;
  if (ImGui::Checkbox("Daylight cycle", &daylight_cycle)) {
    world_runtime.set_daylight_cycle(daylight_cycle);
  }
  float time_scale = clock.time_scale;
  if (ImGui::DragFloat("Time scale", &time_scale, 0.05F, 0.0F, 64.0F, "%.2f")) {
    world_runtime.set_time_scale(std::max(time_scale, 0.0F));
  }
  float day_length_seconds = clock.day_length_seconds;
  if (ImGui::DragFloat(
          "Day length seconds",
          &day_length_seconds,
          5.0F,
          1.0F,
          86400.0F,
          "%.0f")) {
    world_runtime.set_day_length_seconds(
        std::max(day_length_seconds, 1.0F),
        server.base_tps());
  }

  if (ImGui::Button("Midnight")) {
    world_runtime.set_day_fraction(0.0F);
  }
  ImGui::SameLine();
  if (ImGui::Button("Sunrise")) {
    world_runtime.set_day_fraction(0.25F);
  }
  ImGui::SameLine();
  if (ImGui::Button("Noon")) {
    world_runtime.set_day_fraction(0.5F);
  }
  ImGui::SameLine();
  if (ImGui::Button("Sunset")) {
    world_runtime.set_day_fraction(0.75F);
  }

  ImGui::SeparatorText("Controls");
  ImGui::BeginDisabled();
  ImGui::Button("Reload chunks");
  ImGui::Button("Teleport");
  ImGui::EndDisabled();
}

void render_performance_tab(
    const DebugMenuFrameInfo& frame_info,
    const sched::JobSystem& world_jobs,
    render::RenderCommandQueue& renderer) {
  ImGui::Text("FPS: %u", frame_info.displayed_fps);
  ImGui::Text("Frame delta: %.2f ms", frame_info.frame_delta_ms);
  ImGui::Text("CPU frame work: %.2f ms", frame_info.frame_work_ms);
  ImGui::Text("Client job workers: %zu", world_jobs.total_worker_count());
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
    const sched::JobSystem& client_jobs,
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
    render::RenderCommandQueue& renderer,
    const ChunkViewState& chunk_view) {
  const world::ChunkWorkloadSnapshot workload =
      active_session_world(server)->chunk_workload_snapshot();
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
    row("Opaque chunk pass items", chunk_view.opaque_chunks_last_frame, "client culling");
    row("Transparent chunk pass items", chunk_view.transparent_chunks_last_frame, "client culling");
    row("Distance-culled chunks", chunk_view.distance_culled_chunks_last_frame, "client culling");
    row("Frustum-culled chunks", chunk_view.frustum_culled_chunks_last_frame, "client culling");
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
    row("Mesh job batches in flight", chunk_view.pending_meshes.size(), "client workers", true);
    row("Mesh sections in flight", chunk_view.mesh_in_flight.size(), "client workers", true);
    row("Mesh sections prepared", chunk_view.mesh_sections_prepared_last_frame, "last frame", true);
    row("Empty mesh skips", chunk_view.empty_mesh_skips_last_frame, "last frame", true);
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

void render_gpu_render_metrics(render::RenderCommandQueue& renderer) {
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
    sched::JobSystem& world_jobs,
    render::RenderCommandQueue& renderer,
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
    if (sched::JobSystem* job_system = server.world_job_system(); job_system != nullptr) {
      job_system->reset_pressure_stats();
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

void render_particles_tab(const client::ClientRuntime& client) {
  const client::ParticleStats stats = client.particle_stats();
  ImGui::SeparatorText("Particle Counters");
  if (ImGui::BeginTable(
          "ParticleCounters",
          2,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Counter");
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 140.0F);
    ImGui::TableHeadersRow();
    auto row = [](const char* label, u64 value) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(label);
      ImGui::TableNextColumn();
      ImGui::Text("%llu", static_cast<unsigned long long>(value));
    };
    row("Live", stats.live_particles);
    row("Rendered last frame", stats.rendered_last_frame);
    row("Spawned total", stats.spawned_total);
    row("Expired total", stats.expired_total);
    row("Rejected unknown type", stats.rejected_unknown_type);
    row("Rejected budget", stats.rejected_budget);
    row("Rejected distance", stats.rejected_distance);
    row("Rejected invalid", stats.rejected_invalid);
    ImGui::EndTable();
  }
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
    client::ItemAnimationLibrary& item_animation_library,
    client::HeldItemAnimationPlayer& held_item_animation_player,
    Seconds now_seconds,
    const std::filesystem::path& item_animation_export_path,
    const entity::EntityRegistry& entity_registry,
    sched::JobSystem& world_jobs,
    render::RenderCommandQueue& renderer,
    const ChunkViewState& chunk_view) {
  DebugMenuResult result;
  core::set_global_timing_paused(state.profile_sampling_paused);
  record_profile_frame_sample(state, frame_info);
  if (core::profiling_enabled() && !state.profile_sampling_paused) {
    if (const sched::JobSystem* job_system = server.world_job_system(); job_system != nullptr) {
      record_queue_profile_sample(
          state.server_queue_profile,
          job_system->pressure_snapshot());
    }
    record_queue_profile_sample(
        state.client_queue_profile,
        world_jobs.pressure_snapshot());
    record_snapshot_profile_sample(state);
  }
  render_vehicle_debug_overlay(state, server, client);
  render_space_debug_overlay(state, client);
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
      render_world_tab(frame_info, server, client);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Chunks")) {
      render_world_tab(frame_info, server, client);
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
    if (ImGui::BeginTabItem("Item Animations")) {
      render_item_animations_tab(
          state,
          item_animation_library,
          held_item_animation_player,
          now_seconds,
          item_animation_export_path);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Entities")) {
      render_entities_tab(state, server, client, entity_registry);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Networking")) {
      render_network_tab(server, client);
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Particles")) {
      render_particles_tab(client);
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
