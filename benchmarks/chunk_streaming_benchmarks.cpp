#include "benchmark_support.hpp"
#include "chunk_view.hpp"

#include "arv/asset/pack.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/render/render_commands.hpp"
#include "arv/render/render_types.hpp"
#include "arv/sched/job_system.hpp"
#include "arv/sched/task_executor.hpp"
#include "arv/server/chunk_streamer.hpp"
#include "arv/server/session_registry.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"
#include "arv/world/worldgen/loader.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

enum class FlightPath {
  Straight,
  Serpentine,
};

struct StreamingFixture {
  arv::voxel::BlockRegistry registry{};
  arv::voxel::BlockRenderData block_render_data{};
  arv::world::WorldGenerationConfig generation_config{};
  arv::world::WorldGenerationPalette generation_palette{};
  arv::world::WorldHeightSettings height{};
};

struct StreamingTuning {
  arv::i32 view_distance{5};
  arv::i32 near_radius{3};
  arv::u32 generation_jobs_in_flight{4};
  arv::u32 generation_results_per_tick{8};
  arv::u32 generation_results_per_frame{1};
  arv::u32 generation_sections_published_per_frame{32};
  arv::u32 generation_schedules_per_frame{1};
  arv::u32 generation_request_columns_per_tick{4};
  arv::u32 chunks_sent_per_tick{8};
  arv::u32 ready_chunks_sent_per_frame{9};
  arv::u32 client_applies_per_frame{6};
  arv::u32 mesh_jobs_per_frame{13};
  arv::u32 mesh_sections_per_job{4};
  arv::u32 mesh_sections_prepared_per_frame{26};
  arv::u32 mesh_uploads_per_frame{26};
  bool defer_ready_sends_on_server_tick{true};
  double seconds{7.0};
  double warmup_seconds{1.5};
  double speed_blocks_per_second{64.0};
};

[[nodiscard]] arv::u32 env_u32(const char* name, arv::u32 fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value, &end, 10);
  if (end == value || *end != '\0' ||
      parsed > static_cast<unsigned long>(std::numeric_limits<arv::u32>::max())) {
    return fallback;
  }
  return static_cast<arv::u32>(parsed);
}

[[nodiscard]] double env_double(const char* name, double fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    return fallback;
  }
  char* end = nullptr;
  const double parsed = std::strtod(value, &end);
  if (end == value || *end != '\0') {
    return fallback;
  }
  return parsed;
}

[[nodiscard]] bool env_bool(const char* name, bool fallback) {
  return env_u32(name, fallback ? 1U : 0U) != 0U;
}

[[nodiscard]] StreamingTuning streaming_tuning_from_env() {
  StreamingTuning tuning;
  tuning.generation_jobs_in_flight =
      env_u32("ARV_BENCH_GENERATION_JOBS_IN_FLIGHT", tuning.generation_jobs_in_flight);
  tuning.generation_results_per_tick =
      env_u32("ARV_BENCH_GENERATION_RESULTS_PER_TICK", tuning.generation_results_per_tick);
  tuning.generation_results_per_frame =
      env_u32("ARV_BENCH_GENERATION_RESULTS_PER_FRAME", tuning.generation_results_per_frame);
  tuning.generation_sections_published_per_frame =
      env_u32(
          "ARV_BENCH_GENERATION_SECTIONS_PUBLISHED_PER_FRAME",
          tuning.generation_sections_published_per_frame);
  tuning.generation_schedules_per_frame =
      env_u32("ARV_BENCH_GENERATION_SCHEDULES_PER_FRAME", tuning.generation_schedules_per_frame);
  tuning.generation_request_columns_per_tick =
      env_u32("ARV_BENCH_GENERATION_REQUEST_COLUMNS_PER_TICK", tuning.generation_request_columns_per_tick);
  tuning.chunks_sent_per_tick =
      env_u32("ARV_BENCH_CHUNKS_SENT_PER_TICK", tuning.chunks_sent_per_tick);
  tuning.ready_chunks_sent_per_frame =
      env_u32("ARV_BENCH_READY_CHUNKS_SENT_PER_FRAME", tuning.ready_chunks_sent_per_frame);
  tuning.client_applies_per_frame =
      env_u32("ARV_BENCH_CLIENT_APPLIES_PER_FRAME", tuning.client_applies_per_frame);
  tuning.mesh_jobs_per_frame =
      env_u32("ARV_BENCH_MESH_JOBS_PER_FRAME", tuning.mesh_jobs_per_frame);
  tuning.mesh_sections_per_job =
      env_u32("ARV_BENCH_MESH_SECTIONS_PER_JOB", tuning.mesh_sections_per_job);
  tuning.mesh_sections_prepared_per_frame =
      env_u32("ARV_BENCH_MESH_SECTIONS_PREPARED_PER_FRAME", tuning.mesh_sections_prepared_per_frame);
  tuning.mesh_uploads_per_frame =
      env_u32("ARV_BENCH_MESH_UPLOADS_PER_FRAME", tuning.mesh_uploads_per_frame);
  tuning.defer_ready_sends_on_server_tick =
      env_bool("ARV_BENCH_DEFER_READY_SENDS_ON_SERVER_TICK", tuning.defer_ready_sends_on_server_tick);
  tuning.speed_blocks_per_second =
      env_double("ARV_BENCH_SPEED_BLOCKS_PER_SECOND", tuning.speed_blocks_per_second);
  return tuning;
}

struct StreamingCounters {
  arv::u64 requested{};
  arv::u64 generated{};
  arv::u64 sent{};
  arv::u64 applied{};
  arv::u64 meshed{};
  arv::u64 uploaded{};
  arv::u64 renderable{};
  arv::u64 focus_renderable{};
  arv::u64 evicted{};
  arv::u64 cancelled{};
  arv::u64 dropped{};
};

struct DemandRecord {
  double first_demand_ms{};
  double near_demand_ms{-1.0};
  double focus_near_demand_ms{-1.0};
  double renderable_ms{-1.0};
  bool activation_recorded{};
  bool focus_activation_recorded{};
};

struct PendingGenerationJob {
  arv::ChunkCoord coord{};
  arv::sched::JobFuture<arv::world::GeneratedChunkResult> job{};
};

struct PendingGeneratedChunk {
  arv::world::GeneratedChunk chunk{};
  arv::usize next_section_index{};
};

struct StreamingResult {
  StreamingCounters counters{};
  double wall_seconds{};
  double renderable_per_second{};
  double activation_p50_ms{};
  double activation_p95_ms{};
  double activation_p99_ms{};
  double focus_activation_p95_ms{};
  double focus_activation_p99_ms{};
  double main_thread_p95_ms{};
  double main_thread_p99_ms{};
  double server_tick_p95_ms{};
  double generation_drain_p95_ms{};
  double stream_send_p95_ms{};
  double generation_schedule_p95_ms{};
  double client_apply_p95_ms{};
  double chunk_view_p95_ms{};
  double upload_p95_ms{};
  double max_visible_hole_ms{};
  double focus_max_visible_hole_ms{};
  double average_active_workers{};
  double peak_queue_depth{};
  arv::u64 frames{};
  arv::u64 warm_frames{};
};

class BenchmarkRenderTarget final : public arv::render::IRenderCommandTarget {
public:
  void resize(arv::u32, arv::u32) override {
  }

  void submit_frame(arv::render::FramePacket packet) override {
    submitted_chunk_draws_last_frame_ =
        static_cast<arv::u32>(packet.opaque_chunks.size() + packet.transparent_chunks.size());
  }

  void render() override {
    process_chunk_mesh_uploads();
  }

  void begin_shutdown() override {
  }

  void set_chunk_shader_sources(std::string, std::string) override {
  }

  void set_entity_shader_sources(std::string, std::string) override {
  }

  void set_ui_shader_sources(std::string, std::string) override {
  }

  void set_sky_shader_sources(std::string, std::string) override {
  }

  void set_particle_shader_sources(std::string, std::string) override {
  }

  void set_contact_shadow_shader_sources(std::string, std::string) override {
  }

  void set_chunk_texture_atlas(arv::render::TextureArrayUploadData) override {
  }

  void set_entity_texture_atlas(arv::render::TextureArrayUploadData) override {
  }

  void set_particle_texture_atlas(arv::render::TextureArrayUploadData) override {
  }

  void set_ui_font_atlas(arv::render::TextureArrayUploadData) override {
  }

  void set_ui_item_atlas(arv::render::TextureArrayUploadData) override {
  }

  void configure_player_skin_atlas(arv::render::PlayerSkinAtlasConfig) override {
  }

  std::optional<arv::render::TextureRegionHandle> upload_player_skin_texture(
      std::string,
      arv::render::TextureUploadData) override {
    return std::nullopt;
  }

  std::optional<arv::render::PlayerSkinTextureSnapshot> player_skin_texture_snapshot(
      std::string_view,
      arv::render::TextureRegionHandle) const override {
    return std::nullopt;
  }

  bool mark_player_skin_texture_used(arv::render::TextureRegionHandle, bool) override {
    return false;
  }

  void set_entity_model(arv::render::EntityModelAsset) override {
  }

  void set_entity_animation(arv::render::EntityAnimationAsset) override {
  }

  void clear_entity_models() override {
  }

  void clear_entity_animations() override {
  }

  void set_chunk_mesh_upload_budget(arv::u32 max_uploads_per_frame) override {
    max_uploads_per_frame_ = std::max<arv::u32>(max_uploads_per_frame, 1U);
  }

  void enqueue_chunk_mesh_upload(arv::render::ChunkMeshUpload upload) override {
    const arv::render::ChunkMeshKey key{upload.mesh.world_id, upload.mesh.coord};
    const auto resident = chunks_.find(key);
    if (resident != chunks_.end() &&
        !arv::render::should_accept_chunk_mesh_upload(&resident->second, upload)) {
      ++total_discards_;
      return;
    }

    for (auto pending = pending_uploads_.begin(); pending != pending_uploads_.end();) {
      const arv::render::ChunkMeshKey pending_key{
          pending->mesh.world_id,
          pending->mesh.coord};
      if (!(pending_key == key)) {
        ++pending;
        continue;
      }
      if (arv::render::chunk_mesh_upload_at_least_as_new(
              pending->mesh.source_version,
              pending->mesh.source_light_version,
              upload.mesh.source_version,
              upload.mesh.source_light_version)) {
        return;
      }
      pending = pending_uploads_.erase(pending);
    }

    pending_uploads_.emplace_back(std::move(upload));
  }

  void remove_chunk_mesh(arv::render::ChunkMeshKey key) override {
    for (auto pending = pending_uploads_.begin(); pending != pending_uploads_.end();) {
      const arv::render::ChunkMeshKey pending_key{
          pending->mesh.world_id,
          pending->mesh.coord};
      if (pending_key == key) {
        pending = pending_uploads_.erase(pending);
      } else {
        ++pending;
      }
    }
    chunks_.erase(key);
  }

  std::vector<arv::render::ChunkMeshUploadReceipt> drain_chunk_mesh_upload_receipts() override {
    std::vector<arv::render::ChunkMeshUploadReceipt> receipts;
    receipts.swap(receipts_);
    return receipts;
  }

  std::vector<arv::render::RenderCommandDiagnostic> drain_diagnostics() override {
    return {};
  }

  arv::render::RenderMetricsSnapshot metrics_snapshot() const override {
    arv::render::RenderMetricsSnapshot metrics;
    metrics.pending_mesh_upload_count = pending_uploads_.size();
    metrics.mesh_uploads_last_frame = uploads_last_frame_;
    metrics.mesh_upload_time_ms_last_frame = upload_ms_last_frame_;
    metrics.mesh_upload_discards_last_frame = discards_last_frame_;
    metrics.draw_calls_last_frame = submitted_chunk_draws_last_frame_;
    metrics.chunk_draw_calls_last_frame = submitted_chunk_draws_last_frame_;
    metrics.gpu_buffer_uploads_last_frame = buffer_uploads_last_frame_;
    metrics.gpu_buffer_upload_bytes_last_frame = buffer_upload_bytes_last_frame_;
    return metrics;
  }

  [[nodiscard]] bool resident(arv::render::ChunkMeshKey key) const {
    const auto found = chunks_.find(key);
    return found != chunks_.end() && found->second.resident;
  }

  [[nodiscard]] arv::u64 total_uploads() const noexcept {
    return total_uploads_;
  }

  [[nodiscard]] arv::u64 total_discards() const noexcept {
    return total_discards_;
  }

  [[nodiscard]] arv::usize pending_upload_count() const noexcept {
    return pending_uploads_.size();
  }

private:
  static arv::u64 mesh_upload_bytes(const arv::render::ChunkMeshData& mesh) noexcept {
    return mesh.opaque.vertex_bytes.size() +
           mesh.transparent.vertex_bytes.size() +
           mesh.opaque.indices.size() * sizeof(arv::u32) +
           mesh.transparent.indices.size() * sizeof(arv::u32);
  }

  void process_chunk_mesh_uploads() {
    const auto start = Clock::now();
    uploads_last_frame_ = 0;
    discards_last_frame_ = 0;
    buffer_uploads_last_frame_ = 0;
    buffer_upload_bytes_last_frame_ = 0;
    arv::u32 attempts = 0;
    while (attempts < max_uploads_per_frame_ && !pending_uploads_.empty()) {
      arv::render::ChunkMeshUpload upload = std::move(pending_uploads_.front());
      pending_uploads_.pop_front();
      ++attempts;

      const arv::render::ChunkMeshKey key{upload.mesh.world_id, upload.mesh.coord};
      const auto resident = chunks_.find(key);
      const bool accepted =
          resident == chunks_.end() ||
          arv::render::should_accept_chunk_mesh_upload(&resident->second, upload);
      if (!accepted) {
        ++discards_last_frame_;
        ++total_discards_;
        receipts_.push_back({
            key,
            upload.mesh.source_version,
            upload.mesh.source_content_version,
            upload.mesh.source_light_version,
            false});
        continue;
      }

      arv::render::ChunkRenderState state;
      state.opaque_mesh = upload.mesh.opaque.indices.empty()
                              ? arv::render::MeshID{}
                              : arv::render::MeshID{next_mesh_id_++};
      state.transparent_mesh = upload.mesh.transparent.indices.empty()
                                   ? arv::render::MeshID{}
                                   : arv::render::MeshID{next_mesh_id_++};
      state.uploaded_version = upload.mesh.source_version;
      state.uploaded_light_version = upload.mesh.source_light_version;
      state.resident = true;
      buffer_upload_bytes_last_frame_ += mesh_upload_bytes(upload.mesh);
      buffer_uploads_last_frame_ +=
          (upload.mesh.opaque.vertex_bytes.empty() && upload.mesh.opaque.indices.empty() ? 0U : 1U) +
          (upload.mesh.transparent.vertex_bytes.empty() && upload.mesh.transparent.indices.empty() ? 0U : 1U);
      chunks_[key] = state;
      ++uploads_last_frame_;
      ++total_uploads_;
      receipts_.push_back({
          key,
          upload.mesh.source_version,
          upload.mesh.source_content_version,
          upload.mesh.source_light_version,
          true});
    }

    upload_ms_last_frame_ =
        std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  }

  arv::u32 max_uploads_per_frame_{8};
  std::deque<arv::render::ChunkMeshUpload> pending_uploads_{};
  arv::HashMap<arv::render::ChunkMeshKey, arv::render::ChunkRenderState> chunks_{};
  std::vector<arv::render::ChunkMeshUploadReceipt> receipts_{};
  arv::u32 next_mesh_id_{1};
  arv::u64 total_uploads_{};
  arv::u64 total_discards_{};
  arv::u32 uploads_last_frame_{};
  arv::u32 discards_last_frame_{};
  arv::u32 buffer_uploads_last_frame_{};
  arv::u64 buffer_upload_bytes_last_frame_{};
  arv::u32 submitted_chunk_draws_last_frame_{};
  double upload_ms_last_frame_{};
};

[[nodiscard]] arv::voxel::BlockRegistry make_pack_block_registry(
    const arv::asset::PackManager& packs) {
  auto block_definition_text = packs.load_merged_json_array_text("blocks", "blocks");
  if (!block_definition_text) {
    throw std::runtime_error(block_definition_text.error().describe());
  }
  auto block_definitions =
      arv::voxel::parse_block_definitions_json(block_definition_text.value());
  if (!block_definitions) {
    throw std::runtime_error(block_definitions.error().message);
  }
  auto registry = arv::voxel::BlockRegistry::build(std::move(block_definitions.value()));
  if (!registry) {
    throw std::runtime_error(registry.error().message);
  }
  return std::move(registry.value());
}

[[nodiscard]] StreamingFixture make_streaming_fixture() {
  auto packs = arv::asset::PackManager::load_from_packs_root("arvane_home/packs");
  if (!packs) {
    throw std::runtime_error(packs.error().describe());
  }

  StreamingFixture fixture;
  fixture.registry = make_pack_block_registry(packs.value());
  auto worldgen_registry =
      arv::world::worldgen::load_worldgen_registry_from_packs(packs.value());
  if (!worldgen_registry) {
    throw std::runtime_error(worldgen_registry.error().describe());
  }
  auto resolved = arv::world::worldgen::resolve_dimension(
      worldgen_registry.value(),
      "arvane:worldgen/dimension/overworld",
      fixture.registry);
  if (!resolved) {
    throw std::runtime_error(resolved.error().describe());
  }
  fixture.generation_config =
      arv::world::config_from_resolved_dimension(std::move(resolved.value()));
  auto palette =
      arv::world::resolve_world_generation_palette(
          fixture.generation_config,
          fixture.registry);
  if (!palette) {
    throw std::runtime_error(palette.error().message);
  }
  fixture.generation_palette = std::move(palette.value());
  fixture.height.min_chunk_y =
      arv::floor_div(fixture.generation_config.min_y, arv::ChunkSize);
  fixture.height.max_chunk_y =
      arv::floor_div(fixture.generation_config.max_y, arv::ChunkSize);
  fixture.block_render_data =
      arv::voxel::BlockRenderData::make_empty(fixture.registry.variant_count());
  return fixture;
}

[[nodiscard]] const StreamingFixture& streaming_fixture() {
  static const StreamingFixture fixture = make_streaming_fixture();
  return fixture;
}

[[nodiscard]] arv::Vec3f flight_position(
    FlightPath path,
    double seconds,
    const StreamingTuning& tuning) noexcept {
  const double flight_seconds = std::max(0.0, seconds - tuning.warmup_seconds);
  const float x = static_cast<float>(flight_seconds * tuning.speed_blocks_per_second);
  switch (path) {
    case FlightPath::Straight:
      return {x, 52.0F, 0.0F};
    case FlightPath::Serpentine:
      return {
          x,
          52.0F,
          static_cast<float>(std::sin(flight_seconds * 1.65) * 56.0)};
  }
  return {x, 52.0F, 0.0F};
}

[[nodiscard]] arv::ChunkSectionCoord chunk_center_for_position(arv::Vec3f position) {
  return arv::desktop::world_position_chunk_section_coord(position);
}

template<typename Fn>
void visit_sections_in_radius(
    arv::ChunkSectionCoord center,
    arv::world::WorldHeightSettings height,
    arv::i32 radius,
    Fn&& fn) {
  for (arv::i32 z = center.value.z - radius; z <= center.value.z + radius; ++z) {
    for (arv::i32 x = center.value.x - radius; x <= center.value.x + radius; ++x) {
      for (arv::i32 y = height.min_chunk_y; y <= height.max_chunk_y; ++y) {
        fn(arv::ChunkSectionCoord{{x, y, z}});
      }
    }
  }
}

void record_demands(
    arv::HashMap<arv::ChunkSectionCoord, DemandRecord>& demands,
    StreamingCounters& counters,
    arv::HashMap<arv::ChunkSectionCoord, double>& visible_holes,
    arv::HashMap<arv::ChunkSectionCoord, double>& focus_visible_holes,
    std::vector<double>& activation_ms,
    std::vector<double>& focus_activation_ms,
    arv::ChunkSectionCoord center,
    arv::world::WorldHeightSettings height,
    const StreamingTuning& tuning,
    double now_ms,
    bool after_warmup,
    bool full_view_changed,
    const arv::HashSet<arv::ChunkSectionCoord>& renderable) {
  if (full_view_changed) {
    visit_sections_in_radius(
        center,
        height,
        tuning.view_distance,
        [&](arv::ChunkSectionCoord coord) {
          auto [iter, inserted] = demands.try_emplace(coord);
          if (inserted) {
            iter->second.first_demand_ms = now_ms;
            ++counters.requested;
          }
        });
  }

  visit_sections_in_radius(
      center,
      height,
      tuning.near_radius,
      [&](arv::ChunkSectionCoord coord) {
        auto [iter, inserted] = demands.try_emplace(coord);
        if (inserted) {
          iter->second.first_demand_ms = now_ms;
          ++counters.requested;
        }
        if (iter->second.near_demand_ms < 0.0) {
          iter->second.near_demand_ms = now_ms;
        }
        if (after_warmup &&
            renderable.find(coord) != renderable.end() &&
            !iter->second.activation_recorded &&
            iter->second.near_demand_ms >= tuning.warmup_seconds * 1000.0) {
          activation_ms.push_back(0.0);
          iter->second.activation_recorded = true;
        }
      });

  if (!after_warmup) {
    return;
  }

  arv::HashSet<arv::ChunkSectionCoord> current_near;
  arv::HashSet<arv::ChunkSectionCoord> current_focus_near;
  visit_sections_in_radius(
      center,
      height,
      tuning.near_radius,
      [&](arv::ChunkSectionCoord coord) {
        current_near.insert(coord);
        const bool focus_section = std::abs(coord.value.y - center.value.y) <= 1;
        if (focus_section) {
          current_focus_near.insert(coord);
          auto demand = demands.find(coord);
          if (demand != demands.end() && demand->second.focus_near_demand_ms < 0.0) {
            demand->second.focus_near_demand_ms = now_ms;
          }
          if (demand != demands.end() &&
              renderable.find(coord) != renderable.end() &&
              !demand->second.focus_activation_recorded &&
              demand->second.focus_near_demand_ms >= tuning.warmup_seconds * 1000.0) {
            focus_activation_ms.push_back(0.0);
            demand->second.focus_activation_recorded = true;
            ++counters.focus_renderable;
          }
        }
        if (renderable.find(coord) != renderable.end()) {
          visible_holes.erase(coord);
          focus_visible_holes.erase(coord);
          return;
        }
        visible_holes.try_emplace(coord, now_ms);
        if (focus_section) {
          focus_visible_holes.try_emplace(coord, now_ms);
        }
      });
  for (auto iter = visible_holes.begin(); iter != visible_holes.end();) {
    if (current_near.find(iter->first) == current_near.end()) {
      iter = visible_holes.erase(iter);
    } else {
      ++iter;
    }
  }
  for (auto iter = focus_visible_holes.begin(); iter != focus_visible_holes.end();) {
    if (current_focus_near.find(iter->first) == current_focus_near.end()) {
      iter = focus_visible_holes.erase(iter);
    } else {
      ++iter;
    }
  }
}

[[nodiscard]] arv::world::ChunkGenerationRequest make_generation_request(
    const StreamingFixture& fixture,
    arv::ChunkCoord coord) {
  arv::world::ChunkGenerationRequest request;
  request.world_id = arv::WorldID{1};
  request.seed = 0x5157EAULL;
  request.coord = coord;
  request.min_chunk_y = fixture.height.min_chunk_y;
  request.max_chunk_y = fixture.height.max_chunk_y;
  request.resolved_dimension =
      fixture.generation_config.resolved_dimension != nullptr
          ? fixture.generation_config.resolved_dimension
          : fixture.generation_palette.resolved_dimension;
  if (request.resolved_dimension == nullptr) {
    request.config = fixture.generation_config;
    request.palette = fixture.generation_palette;
  }
  return request;
}

void drain_generation_jobs(
    arv::world::WorldRuntime& server_world,
    std::vector<PendingGenerationJob>& jobs,
    std::vector<PendingGeneratedChunk>& pending_generated_chunks,
    StreamingCounters& counters,
    arv::u32 max_results,
    arv::u32 max_publish_sections) {
  auto publish_pending_sections = [&](arv::u32 publish_budget) {
    arv::u32 published = 0U;
    for (arv::usize chunk_index = 0;
         chunk_index < pending_generated_chunks.size() && published < publish_budget;) {
      PendingGeneratedChunk& pending = pending_generated_chunks[chunk_index];
      while (pending.next_section_index < pending.chunk.sections.size() &&
             published < publish_budget) {
        arv::world::GeneratedChunkSection section =
            std::move(pending.chunk.sections[pending.next_section_index]);
        ++pending.next_section_index;
        if (server_world.publish_generated_chunk_section(
                pending.chunk.world_id,
                pending.chunk.coord,
                pending.chunk.surface_y_by_column,
                std::move(section))) {
          ++published;
          ++counters.generated;
        }
      }
      if (pending.next_section_index >= pending.chunk.sections.size()) {
        pending_generated_chunks.erase(
            pending_generated_chunks.begin() + static_cast<arv::isize>(chunk_index));
        continue;
      }
      ++chunk_index;
    }
    return published;
  };

  arv::u32 published_sections = publish_pending_sections(max_publish_sections);
  arv::u32 drained = 0;
  for (arv::usize index = 0;
       index < jobs.size() && drained < max_results;) {
    PendingGenerationJob& pending = jobs[index];
    if (!pending.job.is_complete()) {
      ++index;
      continue;
    }
    arv::world::GeneratedChunkResult generated = pending.job.get();
    if (generated) {
      pending_generated_chunks.push_back(
          PendingGeneratedChunk{std::move(generated.value()), 0U});
      if (published_sections < max_publish_sections) {
        published_sections +=
            publish_pending_sections(max_publish_sections - published_sections);
      }
    } else {
      ++counters.dropped;
    }
    jobs.erase(jobs.begin() + static_cast<arv::isize>(index));
    ++drained;
  }
}

void schedule_generation_jobs(
    const StreamingFixture& fixture,
    const StreamingTuning& tuning,
    arv::world::WorldRuntime& server_world,
    arv::server::ServerChunkStreamer& streamer,
    arv::sched::JobSystem& jobs,
    std::vector<PendingGenerationJob>& pending,
    arv::u32 max_schedules) {
  if (pending.size() >= tuning.generation_jobs_in_flight) {
    return;
  }
  const arv::usize available =
      std::min<arv::usize>(
          static_cast<arv::usize>(tuning.generation_jobs_in_flight) - pending.size(),
          static_cast<arv::usize>(max_schedules));
  if (available == 0U) {
    return;
  }
  const std::vector<arv::ChunkCoord> candidates =
      streamer.pending_generation_requests(server_world, available, true);
  for (const arv::ChunkCoord coord : candidates) {
    if (pending.size() >= tuning.generation_jobs_in_flight) {
      return;
    }
    const bool already_pending = std::any_of(
        pending.begin(),
        pending.end(),
        [coord](const PendingGenerationJob& job) {
          return job.coord == coord;
        });
    if (already_pending) {
      continue;
    }

    const bool marked_any = server_world.mark_chunk_column_generating(coord);
    if (!marked_any) {
      streamer.mark_generation_request_scheduled(server_world.id(), coord);
      continue;
    }

    arv::sched::JobDesc desc;
    desc.name = "benchmark_fresh_chunk_generation";
    desc.lane = arv::sched::JobLane::BackgroundCpu;
    desc.domain = arv::sched::JobDomain::ChunkGeneration;
    const arv::world::ChunkGenerationRequest request =
        make_generation_request(fixture, coord);
    auto future = jobs.submit_result(
        std::move(desc),
        [request](const arv::sched::CancellationToken& token) {
          if (token.is_canceled()) {
            return arv::world::GeneratedChunkResult::failure(
                arv::world::WorldGenerationError{"fresh chunk benchmark generation canceled"});
          }
          return arv::world::generate_chunk(request);
        });
    if (!future.valid()) {
      continue;
    }
    pending.push_back({coord, std::move(future)});
    streamer.mark_generation_request_scheduled(server_world.id(), coord);
  }
}

void apply_client_messages(
    arv::world::WorldRuntime& client_world,
    arv::desktop::ChunkViewState& chunk_view,
    arv::render::RenderCommandQueue& renderer,
    std::deque<arv::server::QueuedChunkStreamMessage>& messages,
    const StreamingTuning& tuning,
    StreamingCounters& counters) {
  arv::u32 applied = 0;
  while (applied < tuning.client_applies_per_frame && !messages.empty()) {
    arv::server::QueuedChunkStreamMessage message = std::move(messages.front());
    messages.pop_front();
    if (message.message_type == arv::protocol::MessageType::ClientboundChunkSectionUnload) {
      auto unload =
          arv::protocol::deserialize_clientbound_chunk_section_unload(message.payload);
      if (unload && unload.value().world_id == client_world.id()) {
        if (client_world.unload_chunk(unload.value().coord)) {
          renderer.remove_chunk_mesh({unload.value().world_id, unload.value().coord});
          arv::desktop::remove_chunk_view_chunk(chunk_view, unload.value().coord);
          ++counters.evicted;
        }
      } else {
        ++counters.dropped;
      }
      ++applied;
      continue;
    }

    arv::protocol::ClientboundChunkSectionData section;
    auto decoded =
        arv::protocol::deserialize_clientbound_chunk_section_data_into(
            message.payload,
            section);
    if (!decoded || section.world_id != client_world.id()) {
      ++counters.dropped;
      ++applied;
      continue;
    }

    std::optional<std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume>> lights;
    if (section.has_lighting && section.lights.has_value()) {
      lights = std::move(section.lights.value());
    }
    std::optional<std::array<arv::voxel::FluidCell, arv::voxel::ChunkVolume>> fluids;
    if (section.has_fluids && section.fluids.has_value()) {
      fluids = std::move(section.fluids.value());
    }
    client_world.apply_chunk_snapshot_owned(
        section.coord,
        std::move(section.blocks),
        std::move(lights),
        std::move(fluids),
        section.has_lighting && section.lighting_valid);
    ++counters.applied;
    ++applied;
  }
}

[[nodiscard]] arv::HashSet<arv::ChunkSectionCoord> collect_render_complete_sections(
    arv::world::WorldRuntime& client_world,
    arv::desktop::ChunkViewState& chunk_view,
    const BenchmarkRenderTarget& render_target) {
  arv::HashSet<arv::ChunkSectionCoord> complete;
  const std::vector<arv::render::ChunkDrawItem>& draw_items =
      arv::desktop::chunk_draw_items(chunk_view);
  for (const arv::render::ChunkDrawItem& item : draw_items) {
    if (render_target.resident(item.key)) {
      complete.insert(item.key.coord);
    }
  }
  arv::world::WorldSearch search{client_world};
  for (const auto& [coord, content_version] : chunk_view.empty_mesh_content_versions) {
    const arv::voxel::ChunkSection* chunk = search.find_chunk(coord);
    if (chunk != nullptr && chunk->content_version() == content_version) {
      complete.insert(coord);
    }
  }
  return complete;
}

[[nodiscard]] double percentile(std::vector<double> values, double p) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const double scaled =
      std::clamp(p, 0.0, 100.0) / 100.0 * static_cast<double>(values.size() - 1U);
  const auto index = static_cast<arv::usize>(std::llround(scaled));
  return values[std::min(index, values.size() - 1U)];
}

[[nodiscard]] StreamingResult run_streaming_simulation(
    FlightPath path,
    const StreamingTuning& tuning) {
  const StreamingFixture& fixture = streaming_fixture();
  arv::world::WorldRuntime server_world{arv::WorldID{1}};
  arv::world::WorldRuntime client_world{arv::WorldID{1}};
  server_world.configure_height(fixture.height);
  client_world.configure_height(fixture.height);

  arv::server::ServerSession session;
  session.session_id = arv::SessionID{1};
  session.player_id = arv::PlayerID{1};
  session.connection_id = arv::net::ConnectionID{1};
  session.world_id = server_world.id();
  session.authenticated = true;

  arv::server::ServerChunkStreamer streamer;
  streamer.configure({
      .max_view_distance = tuning.view_distance,
      .simulation_distance = std::min<arv::i32>(2, tuning.view_distance),
      .unload_margin = 1,
      .max_chunks_sent_per_player_tick = tuning.chunks_sent_per_tick,
      .max_unloads_sent_per_player_tick = 128});

  arv::sched::JobSystemConfig job_config;
  job_config.critical_cpu_workers = 1;
  job_config.background_cpu_workers = 6;
  job_config.blocking_io_workers = 1;
  job_config.max_chunk_gen_in_flight = tuning.generation_jobs_in_flight;
  job_config.max_mesh_jobs_in_flight = tuning.mesh_jobs_per_frame;
  arv::sched::JobSystem jobs{job_config};
  jobs.start();
  jobs.reset_pressure_stats();
  arv::sched::JobSystemTaskExecutor tasks{jobs};

  BenchmarkRenderTarget render_target;
  arv::render::RenderCommandQueue renderer{render_target};
  renderer.set_chunk_mesh_upload_budget(tuning.mesh_uploads_per_frame);
  arv::desktop::ChunkViewState chunk_view;
  arv::io::ChunkDistanceConfig distances;
  distances.render_distance = tuning.view_distance;
  arv::io::ChunkPipelineConfig pipeline;
  pipeline.max_mesh_jobs_per_frame = tuning.mesh_jobs_per_frame;
  pipeline.max_mesh_sections_per_job = tuning.mesh_sections_per_job;
  pipeline.max_mesh_sections_prepared_per_frame = tuning.mesh_sections_prepared_per_frame;
  pipeline.frame_spike_log_ms = 1000;

  StreamingResult result;
  std::vector<PendingGenerationJob> generation_jobs;
  std::vector<PendingGeneratedChunk> pending_generated_chunks;
  std::vector<arv::server::QueuedChunkStreamMessage> sent_messages;
  std::deque<arv::server::QueuedChunkStreamMessage> network_queue;
  arv::HashMap<arv::ChunkSectionCoord, DemandRecord> demands;
  arv::HashSet<arv::ChunkSectionCoord> renderable_sections;
  arv::HashMap<arv::ChunkSectionCoord, double> visible_holes;
  arv::HashMap<arv::ChunkSectionCoord, double> focus_visible_holes;
  std::vector<double> activation_ms;
  std::vector<double> focus_activation_ms;
  std::vector<double> main_thread_ms;
  std::vector<double> server_tick_ms;
  std::vector<double> generation_drain_ms;
  std::vector<double> stream_send_ms;
  std::vector<double> generation_schedule_ms;
  std::vector<double> client_apply_ms;
  std::vector<double> chunk_view_ms;
  std::vector<double> upload_ms;
  double active_worker_samples = 0.0;
  double queue_depth_samples = 0.0;
  arv::u64 sampled_frames = 0;
  arv::u32 sequence = 1;
  double server_tick_accumulator = 0.0;
  std::optional<arv::ChunkSectionCoord> previous_interest_center;
  const bool pump_generation_per_frame =
      tuning.generation_results_per_frame > 0U ||
      tuning.generation_schedules_per_frame > 0U;
  sent_messages.reserve(
      static_cast<arv::usize>(tuning.chunks_sent_per_tick) +
      static_cast<arv::usize>(128U));

  const auto wall_start = Clock::now();
  constexpr double FrameDt = 1.0 / 60.0;
  constexpr double ServerTickDt = 1.0 / 20.0;
  const arv::u64 frame_count =
      static_cast<arv::u64>(std::ceil(tuning.seconds / FrameDt));

  for (arv::u64 frame = 0; frame < frame_count; ++frame) {
    const double now_seconds = static_cast<double>(frame) * FrameDt;
    const double now_ms = now_seconds * 1000.0;
    const bool after_warmup = now_seconds >= tuning.warmup_seconds;
    const arv::ChunkSectionCoord center =
        chunk_center_for_position(flight_position(path, now_seconds, tuning));
    const bool interest_changed =
        !previous_interest_center.has_value() ||
        previous_interest_center.value() != center;

    record_demands(
        demands,
        result.counters,
        visible_holes,
        focus_visible_holes,
        activation_ms,
        focus_activation_ms,
        center,
        fixture.height,
        tuning,
        now_ms,
        after_warmup,
        interest_changed,
        renderable_sections);

    const auto frame_start = Clock::now();

    if (interest_changed) {
      arv::protocol::ServerboundChunkInterestUpdate interest;
      interest.world_id = server_world.id();
      interest.center_chunk = center;
      interest.client_view_distance = tuning.view_distance;
      interest.previous_center_chunk = previous_interest_center;
      interest.sequence_id = sequence++;
      streamer.update_player_interest(session, center, fixture.height, interest);
      previous_interest_center = center;
    }

    double server_work_ms = 0.0;
    double generation_drain_work_ms = 0.0;
    double stream_send_work_ms = 0.0;
    double generation_schedule_work_ms = 0.0;
    bool server_tick_ran_this_frame = false;
    server_tick_accumulator += FrameDt;
    while (server_tick_accumulator >= ServerTickDt) {
      server_tick_ran_this_frame = true;
      const auto server_tick_start = Clock::now();
      if (!pump_generation_per_frame) {
        const auto generation_drain_start = Clock::now();
        drain_generation_jobs(
            server_world,
            generation_jobs,
            pending_generated_chunks,
            result.counters,
            tuning.generation_results_per_tick,
            tuning.generation_results_per_tick * tuning.generation_sections_published_per_frame);
        generation_drain_work_ms +=
            std::chrono::duration<double, std::milli>(
                Clock::now() - generation_drain_start)
                .count();
      }

      sent_messages.clear();
      const auto stream_send_start = Clock::now();
      if (tuning.generation_request_columns_per_tick > 0U) {
        streamer.tick_missing_chunk_requests(
            server_world,
            tuning.generation_request_columns_per_tick);
      } else {
        streamer.tick_streaming_budget(server_world, sent_messages);
        for (arv::server::QueuedChunkStreamMessage& message : sent_messages) {
          if (message.tracks_sent_chunk) {
            (void)streamer.confirm_chunk_sent(
                message.session_id,
                message.world_id,
                message.coord,
                message.block_version,
                message.light_version,
                message.lighting_valid);
            ++result.counters.sent;
          }
          network_queue.emplace_back(std::move(message));
        }
      }
      stream_send_work_ms +=
          std::chrono::duration<double, std::milli>(
              Clock::now() - stream_send_start)
              .count();

      if (!pump_generation_per_frame) {
        const auto generation_schedule_start = Clock::now();
        schedule_generation_jobs(
            fixture,
            tuning,
            server_world,
            streamer,
            jobs,
            generation_jobs,
            tuning.generation_jobs_in_flight);
        generation_schedule_work_ms +=
            std::chrono::duration<double, std::milli>(
                Clock::now() - generation_schedule_start)
                .count();
      }
      server_tick_accumulator -= ServerTickDt;
      server_work_ms +=
          std::chrono::duration<double, std::milli>(
              Clock::now() - server_tick_start)
              .count();
    }
    if (pump_generation_per_frame) {
      const auto generation_drain_start = Clock::now();
      drain_generation_jobs(
          server_world,
          generation_jobs,
          pending_generated_chunks,
          result.counters,
          tuning.generation_results_per_frame,
          tuning.generation_sections_published_per_frame);
      generation_drain_work_ms +=
          std::chrono::duration<double, std::milli>(
              Clock::now() - generation_drain_start)
              .count();

      const auto generation_schedule_start = Clock::now();
      schedule_generation_jobs(
          fixture,
          tuning,
          server_world,
          streamer,
          jobs,
          generation_jobs,
          tuning.generation_schedules_per_frame);
      generation_schedule_work_ms +=
          std::chrono::duration<double, std::milli>(
              Clock::now() - generation_schedule_start)
              .count();
    }
    double apply_work_ms = 0.0;

    if (tuning.ready_chunks_sent_per_frame > 0U &&
        !(tuning.defer_ready_sends_on_server_tick && server_tick_ran_this_frame)) {
      sent_messages.clear();
      const auto stream_send_start = Clock::now();
      streamer.tick_ready_chunk_budget(
          server_world,
          sent_messages,
          tuning.ready_chunks_sent_per_frame);
      for (arv::server::QueuedChunkStreamMessage& message : sent_messages) {
        if (message.tracks_sent_chunk) {
          (void)streamer.confirm_chunk_sent(
              message.session_id,
              message.world_id,
              message.coord,
              message.block_version,
              message.light_version,
              message.lighting_valid);
          ++result.counters.sent;
        }
        network_queue.emplace_back(std::move(message));
      }
      stream_send_work_ms +=
          std::chrono::duration<double, std::milli>(
              Clock::now() - stream_send_start)
              .count();
    }

    const auto apply_start = Clock::now();
    apply_client_messages(
        client_world,
        chunk_view,
        renderer,
        network_queue,
        tuning,
        result.counters);
    apply_work_ms +=
        std::chrono::duration<double, std::milli>(Clock::now() - apply_start).count();

    const auto chunk_view_start = Clock::now();
    (void)arv::desktop::update_chunk_view(
        client_world,
        renderer,
        tasks,
        chunk_view,
        distances,
        pipeline,
        center,
        fixture.height.min_chunk_y,
        fixture.height.max_chunk_y,
        fixture.registry,
        fixture.block_render_data,
        nullptr);
    result.counters.meshed += chunk_view.mesh_completed_last_frame;
    const double chunk_view_work_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - chunk_view_start).count();

    const auto upload_start = Clock::now();
    renderer.render();
    const double upload_work_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - upload_start).count();
    result.counters.uploaded = render_target.total_uploads();
    result.counters.dropped += render_target.total_discards();

    const double frame_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - frame_start).count();

    const arv::HashSet<arv::ChunkSectionCoord> complete_sections =
        collect_render_complete_sections(client_world, chunk_view, render_target);
    for (const arv::ChunkSectionCoord coord : complete_sections) {
      if (!renderable_sections.insert(coord).second) {
        continue;
      }
      ++result.counters.renderable;
      auto demand = demands.find(coord);
      if (demand != demands.end()) {
        demand->second.renderable_ms = now_ms;
        if (after_warmup &&
            !demand->second.activation_recorded &&
            demand->second.near_demand_ms >= tuning.warmup_seconds * 1000.0) {
          activation_ms.push_back(now_ms - demand->second.near_demand_ms);
          demand->second.activation_recorded = true;
        }
        if (after_warmup &&
            !demand->second.focus_activation_recorded &&
            demand->second.focus_near_demand_ms >= tuning.warmup_seconds * 1000.0) {
          focus_activation_ms.push_back(now_ms - demand->second.focus_near_demand_ms);
          demand->second.focus_activation_recorded = true;
          ++result.counters.focus_renderable;
        }
      }
    }
    if (after_warmup) {
      main_thread_ms.push_back(frame_ms);
      server_tick_ms.push_back(server_work_ms);
      generation_drain_ms.push_back(generation_drain_work_ms);
      stream_send_ms.push_back(stream_send_work_ms);
      generation_schedule_ms.push_back(generation_schedule_work_ms);
      client_apply_ms.push_back(apply_work_ms);
      chunk_view_ms.push_back(chunk_view_work_ms);
      upload_ms.push_back(upload_work_ms);
      for (auto& [coord, missing_since] : visible_holes) {
        (void)coord;
        result.max_visible_hole_ms =
            std::max(result.max_visible_hole_ms, now_ms - missing_since);
      }
      for (auto& [coord, missing_since] : focus_visible_holes) {
        (void)coord;
        result.focus_max_visible_hole_ms =
            std::max(result.focus_max_visible_hole_ms, now_ms - missing_since);
      }
      const arv::sched::JobSystemPressureSnapshot pressure = jobs.pressure_snapshot();
      active_worker_samples += static_cast<double>(pressure.active_workers);
      queue_depth_samples =
          std::max<double>(
              queue_depth_samples,
              static_cast<double>(
                  pressure.current_queued_jobs +
                  network_queue.size() +
                  generation_jobs.size() +
                  render_target.pending_upload_count() +
                  chunk_view.mesh_candidates.size()));
      ++sampled_frames;
      ++result.warm_frames;
    }
    ++result.frames;

    const auto next_frame_time =
        wall_start +
        std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>((static_cast<double>(frame) + 1.0) * FrameDt));
    std::this_thread::sleep_until(next_frame_time);
  }

  while (!generation_jobs.empty() || !pending_generated_chunks.empty()) {
    drain_generation_jobs(
        server_world,
        generation_jobs,
        pending_generated_chunks,
        result.counters,
        tuning.generation_results_per_tick,
        std::numeric_limits<arv::u32>::max());
    if (!generation_jobs.empty() || !pending_generated_chunks.empty()) {
      std::this_thread::yield();
    }
  }
  jobs.request_stop();
  jobs.join();

  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - wall_start).count();
  const double measured_seconds =
      std::max(0.001, tuning.seconds - tuning.warmup_seconds);
  result.renderable_per_second =
      static_cast<double>(result.counters.renderable) / measured_seconds;
  result.activation_p50_ms = percentile(activation_ms, 50.0);
  result.activation_p95_ms = percentile(activation_ms, 95.0);
  result.activation_p99_ms = percentile(activation_ms, 99.0);
  result.focus_activation_p95_ms = percentile(focus_activation_ms, 95.0);
  result.focus_activation_p99_ms = percentile(focus_activation_ms, 99.0);
  result.main_thread_p95_ms = percentile(main_thread_ms, 95.0);
  result.main_thread_p99_ms = percentile(main_thread_ms, 99.0);
  result.server_tick_p95_ms = percentile(server_tick_ms, 95.0);
  result.generation_drain_p95_ms = percentile(generation_drain_ms, 95.0);
  result.stream_send_p95_ms = percentile(stream_send_ms, 95.0);
  result.generation_schedule_p95_ms = percentile(generation_schedule_ms, 95.0);
  result.client_apply_p95_ms = percentile(client_apply_ms, 95.0);
  result.chunk_view_p95_ms = percentile(chunk_view_ms, 95.0);
  result.upload_p95_ms = percentile(upload_ms, 95.0);
  result.average_active_workers =
      sampled_frames == 0U ? 0.0 : active_worker_samples / static_cast<double>(sampled_frames);
  result.peak_queue_depth = queue_depth_samples;
  return result;
}

void publish_counters(benchmark::State& state, const StreamingResult& result) {
  state.counters["requested"] = static_cast<double>(result.counters.requested);
  state.counters["generated"] = static_cast<double>(result.counters.generated);
  state.counters["sent"] = static_cast<double>(result.counters.sent);
  state.counters["applied"] = static_cast<double>(result.counters.applied);
  state.counters["meshed"] = static_cast<double>(result.counters.meshed);
  state.counters["uploaded"] = static_cast<double>(result.counters.uploaded);
  state.counters["renderable"] = static_cast<double>(result.counters.renderable);
  state.counters["focus_renderable"] = static_cast<double>(result.counters.focus_renderable);
  state.counters["renderable/sec"] = result.renderable_per_second;
  state.counters["activation_p95_ms"] = result.activation_p95_ms;
  state.counters["activation_p99_ms"] = result.activation_p99_ms;
  state.counters["full_activation_p95_ms"] = result.activation_p95_ms;
  state.counters["full_activation_p99_ms"] = result.activation_p99_ms;
  state.counters["focus_activation_p95_ms"] = result.focus_activation_p95_ms;
  state.counters["focus_activation_p99_ms"] = result.focus_activation_p99_ms;
  state.counters["visible_activation_p95_ms"] = result.focus_activation_p95_ms;
  state.counters["visible_activation_p99_ms"] = result.focus_activation_p99_ms;
  state.counters["main_p95_ms"] = result.main_thread_p95_ms;
  state.counters["main_p99_ms"] = result.main_thread_p99_ms;
  state.counters["server_tick_p95_ms"] = result.server_tick_p95_ms;
  state.counters["generation_drain_p95_ms"] = result.generation_drain_p95_ms;
  state.counters["stream_send_p95_ms"] = result.stream_send_p95_ms;
  state.counters["generation_schedule_p95_ms"] = result.generation_schedule_p95_ms;
  state.counters["client_apply_p95_ms"] = result.client_apply_p95_ms;
  state.counters["chunk_view_p95_ms"] = result.chunk_view_p95_ms;
  state.counters["upload_p95_ms"] = result.upload_p95_ms;
  state.counters["hole_max_ms"] = result.max_visible_hole_ms;
  state.counters["focus_hole_max_ms"] = result.focus_max_visible_hole_ms;
  state.counters["full_hole_max_ms"] = result.max_visible_hole_ms;
  state.counters["visible_hole_max_ms"] = result.focus_max_visible_hole_ms;
  state.counters["avg_active_workers"] = result.average_active_workers;
  state.counters["peak_queue"] = result.peak_queue_depth;

  std::ostringstream label;
  label << "fresh_sections requested=" << result.counters.requested
        << " generated=" << result.counters.generated
        << " sent=" << result.counters.sent
        << " applied=" << result.counters.applied
        << " meshed=" << result.counters.meshed
        << " uploaded=" << result.counters.uploaded
        << " renderable=" << result.counters.renderable
        << " focus_renderable=" << result.counters.focus_renderable
        << " full_p95_activation_ms=" << result.activation_p95_ms
        << " visible_p95_activation_ms=" << result.focus_activation_p95_ms
        << " p95_main_ms=" << result.main_thread_p95_ms
        << " server_tick_p95_ms=" << result.server_tick_p95_ms
        << " generation_drain_p95_ms=" << result.generation_drain_p95_ms
        << " stream_send_p95_ms=" << result.stream_send_p95_ms
        << " generation_schedule_p95_ms=" << result.generation_schedule_p95_ms
        << " client_apply_p95_ms=" << result.client_apply_p95_ms
        << " chunk_view_p95_ms=" << result.chunk_view_p95_ms
        << " upload_p95_ms=" << result.upload_p95_ms
        << " full_max_hole_ms=" << result.max_visible_hole_ms
        << " visible_max_hole_ms=" << result.focus_max_visible_hole_ms;
  state.SetLabel(label.str());
}

void BM_FreshChunkStreamingStraightFlight(benchmark::State& state) {
  StreamingResult last;
  const StreamingTuning tuning = streaming_tuning_from_env();
  for (auto _ : state) {
    last = run_streaming_simulation(FlightPath::Straight, tuning);
    state.SetIterationTime(last.wall_seconds);
  }
  publish_counters(state, last);
}

void BM_FreshChunkStreamingSerpentineFlight(benchmark::State& state) {
  StreamingResult last;
  const StreamingTuning tuning = streaming_tuning_from_env();
  for (auto _ : state) {
    last = run_streaming_simulation(FlightPath::Serpentine, tuning);
    state.SetIterationTime(last.wall_seconds);
  }
  publish_counters(state, last);
}

} // namespace

BENCHMARK(BM_FreshChunkStreamingStraightFlight)
    ->Iterations(1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_FreshChunkStreamingSerpentineFlight)
    ->Iterations(1)
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);
