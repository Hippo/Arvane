#include "chunk_view.hpp"

#include "arv/render/chunk_mesher.hpp"
#include "arv/render/render_commands.hpp"

#include "arv/core/profiling.hpp"
#include "arv/core/time.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace arv::desktop {
namespace {

struct MeshJobInput {
  ChunkSectionCoord coord{};
  voxel::ChunkSnapshot snapshot;
};

constexpr auto SpikeLogCooldown = std::chrono::milliseconds{500};

[[nodiscard]] bool target_matches(
    const ChunkViewState& view,
    const io::ChunkDistanceConfig& distances,
    ChunkSectionCoord center,
    i32 min_chunk_y,
    i32 max_chunk_y) noexcept {
  return view.target_center.has_value() &&
         view.target_center.value() == center &&
         view.target_min_chunk_y == min_chunk_y &&
         view.target_max_chunk_y == max_chunk_y &&
         view.target_render_distance == distances.render_distance;
}

[[nodiscard]] bool is_inside_target_y(const ChunkViewState& view, ChunkSectionCoord coord) noexcept {
  return coord.value.y >= view.target_min_chunk_y &&
         coord.value.y <= view.target_max_chunk_y;
}

[[nodiscard]] bool is_inside_render_view(const ChunkViewState& view, ChunkSectionCoord coord) noexcept {
  return view.target_center.has_value() &&
         is_inside_target_y(view, coord) &&
         chunk_horizontal_chebyshev_distance(coord, view.target_center.value()) <=
             view.target_render_distance;
}

[[nodiscard]] i32 chunk_priority_distance(ChunkSectionCoord coord, ChunkSectionCoord center) noexcept {
  const i32 horizontal = chunk_horizontal_chebyshev_distance(coord, center);
  const i32 vertical = std::abs(coord.value.y - center.value.y);
  return horizontal * 4 + vertical;
}

[[nodiscard]] u32 mesh_candidate_rank(
    const ChunkViewState& view,
    const voxel::ChunkSection* chunk,
    ChunkSectionCoord coord) noexcept {
  if (chunk == nullptr) {
    return 4U;
  }
  if (chunk->is_dirty(voxel::ChunkDirtyFlags::Geometry)) {
    return 0U;
  }
  if (view.draw_items.find(coord) == view.draw_items.end()) {
    return 1U;
  }
  if (chunk->is_dirty(voxel::ChunkDirtyFlags::LightMesh)) {
    return 2U;
  }
  return 3U;
}

void sort_mesh_candidates(world::WorldRuntime& world, ChunkViewState& view) {
  if (!view.mesh_candidates_sort_dirty) {
    return;
  }
  if (!view.target_center.has_value() || view.mesh_candidates.size() < 2U) {
    view.mesh_candidates_sort_dirty = false;
    return;
  }

  const ChunkSectionCoord center = view.target_center.value();
  const world::WorldSearch search{world};
  std::stable_sort(
      view.mesh_candidates.begin(),
      view.mesh_candidates.end(),
      [&search, &view, center](ChunkSectionCoord lhs, ChunkSectionCoord rhs) {
        const voxel::ChunkSection* lhs_chunk = search.find_chunk(lhs);
        const voxel::ChunkSection* rhs_chunk = search.find_chunk(rhs);
        const i32 lhs_distance = chunk_priority_distance(lhs, center);
        const i32 rhs_distance = chunk_priority_distance(rhs, center);
        if (lhs_distance != rhs_distance) {
          return lhs_distance < rhs_distance;
        }

        const u32 lhs_rank = mesh_candidate_rank(view, lhs_chunk, lhs);
        const u32 rhs_rank = mesh_candidate_rank(view, rhs_chunk, rhs);
        if (lhs_rank != rhs_rank) {
          return lhs_rank < rhs_rank;
        }

        if (lhs.value.z != rhs.value.z) {
          return lhs.value.z < rhs.value.z;
        }
        if (lhs.value.x != rhs.value.x) {
          return lhs.value.x < rhs.value.x;
        }
        return lhs.value.y < rhs.value.y;
      });
  view.mesh_candidates_sort_dirty = false;
}

void set_draw_items_dirty(ChunkViewState& view) noexcept {
  view.draw_items_dirty = true;
}

void set_mesh_candidates_sort_dirty(ChunkViewState& view) noexcept {
  view.mesh_candidates_sort_dirty = true;
}

[[nodiscard]] bool chunk_section_is_empty_for_rendering(
    const voxel::ChunkSection& chunk) noexcept {
  for (const voxel::BlockVariantID block : chunk.blocks()) {
    if (block != voxel::AirBlockVariantID) {
      return false;
    }
  }
  for (const voxel::FluidCell fluid : chunk.fluids()) {
    if (!fluid.empty()) {
      return false;
    }
  }
  return true;
}

void remove_empty_chunk_mesh(
    render::RenderCommandQueue& renderer,
    ChunkViewState& view,
    WorldID world_id,
    ChunkSectionCoord coord) {
  renderer.remove_chunk_mesh({world_id, coord});
  if (view.draw_items.erase(coord) > 0U) {
    set_draw_items_dirty(view);
  }
}

[[nodiscard]] bool is_known_empty_mesh(
    const ChunkViewState& view,
    ChunkSectionCoord coord,
    u64 content_version) {
  const auto iter = view.empty_mesh_content_versions.find(coord);
  return iter != view.empty_mesh_content_versions.end() && iter->second == content_version;
}

[[nodiscard]] bool chunk_waiting_for_mesh_upload(
    const voxel::ChunkSection& chunk) noexcept {
  return chunk.mesh_state() == voxel::ChunkMeshState::WaitingForUpload;
}

void enqueue_mesh_candidate(ChunkViewState& view, ChunkSectionCoord coord) {
  if (!is_inside_render_view(view, coord)) {
    return;
  }
  if (view.mesh_candidate_set.insert(coord).second) {
    view.mesh_candidates.emplace_back(coord);
    set_mesh_candidates_sort_dirty(view);
  }
}

void clear_mesh_candidates(ChunkViewState& view) {
  view.mesh_candidates.clear();
  view.mesh_candidate_set.clear();
  view.mesh_candidates_sort_dirty = false;
}

void remove_mesh_candidates_outside_view(ChunkViewState& view) {
  bool removed_any = false;
  for (auto iter = view.mesh_candidates.begin(); iter != view.mesh_candidates.end();) {
    if (!is_inside_render_view(view, *iter)) {
      view.mesh_candidate_set.erase(*iter);
      iter = view.mesh_candidates.erase(iter);
      removed_any = true;
    } else {
      ++iter;
    }
  }
  if (removed_any) {
    set_mesh_candidates_sort_dirty(view);
  }
}

void enqueue_loaded_mesh_candidate(
    const world::WorldSearch& search,
    ChunkViewState& view,
    ChunkSectionCoord coord) {
  if (search.find_chunk(coord) != nullptr) {
    enqueue_mesh_candidate(view, coord);
  }
}

void rebuild_mesh_candidates(world::WorldRuntime& world, ChunkViewState& view) {
  clear_mesh_candidates(view);

  const ChunkSectionCoord center = view.target_center.value();
  const i32 render_distance = view.target_render_distance;
  const world::WorldSearch search{world};
  for (i32 z = center.value.z - render_distance; z <= center.value.z + render_distance; ++z) {
    for (i32 x = center.value.x - render_distance; x <= center.value.x + render_distance; ++x) {
      for (i32 y = view.target_min_chunk_y; y <= view.target_max_chunk_y; ++y) {
        enqueue_loaded_mesh_candidate(search, view, {{x, y, z}});
      }
    }
  }
}

void enqueue_entered_mesh_candidates(
    world::WorldRuntime& world,
    ChunkViewState& view,
    ChunkSectionCoord previous_center) {
  const ChunkSectionCoord center = view.target_center.value();
  const i32 render_distance = view.target_render_distance;
  const world::WorldSearch search{world};
  for (i32 z = center.value.z - render_distance; z <= center.value.z + render_distance; ++z) {
    for (i32 x = center.value.x - render_distance; x <= center.value.x + render_distance; ++x) {
      const ChunkSectionCoord horizontal_coord{{x, center.value.y, z}};
      if (chunk_horizontal_chebyshev_distance(horizontal_coord, previous_center) <=
          render_distance) {
        continue;
      }
      for (i32 y = view.target_min_chunk_y; y <= view.target_max_chunk_y; ++y) {
        enqueue_loaded_mesh_candidate(search, view, {{x, y, z}});
      }
    }
  }
}

void enqueue_dirty_mesh_chunks_in_view(world::WorldRuntime& world, ChunkViewState& view) {
  for (const ChunkSectionCoord coord : world.consume_dirty_mesh_chunks(
           voxel::ChunkDirtyFlags::Geometry | voxel::ChunkDirtyFlags::LightMesh)) {
    enqueue_mesh_candidate(view, coord);
  }
  view.mesh_rescan_requested = false;
}

void retarget_chunk_view(
    world::WorldRuntime& world,
    ChunkViewState& view,
    const io::ChunkDistanceConfig& distances,
    ChunkSectionCoord center,
    i32 min_chunk_y,
    i32 max_chunk_y) {
  const std::optional<ChunkSectionCoord> previous_center = view.target_center;
  const bool same_shape =
      previous_center.has_value() &&
      view.target_min_chunk_y == min_chunk_y &&
      view.target_max_chunk_y == max_chunk_y &&
      view.target_render_distance == distances.render_distance;

  view.target_center = center;
  view.target_min_chunk_y = min_chunk_y;
  view.target_max_chunk_y = max_chunk_y;
  view.target_render_distance = distances.render_distance;
  set_mesh_candidates_sort_dirty(view);

  if (same_shape) {
    remove_mesh_candidates_outside_view(view);
    enqueue_entered_mesh_candidates(world, view, previous_center.value());
  } else {
    rebuild_mesh_candidates(world, view);
  }
  enqueue_dirty_mesh_chunks_in_view(world, view);
}

void drain_mesh_jobs(
    world::WorldRuntime& world,
    render::RenderCommandQueue& renderer,
    ChunkViewState& view) {
  ARV_PROFILE_ZONE("Chunk meshing drain");
  world::WorldSearch search{world};
  for (usize index = 0; index < view.pending_meshes.size();) {
    auto& pending = view.pending_meshes[index];
    if (!pending.job.is_complete()) {
      ++index;
      continue;
    }

    auto batch = pending.job.get();
    for (const ChunkViewState::PendingMeshEntry& entry : pending.entries) {
      view.mesh_in_flight.erase(entry.coord);
    }

    for (auto& timed : batch.meshes) {
      view.mesh_completed_last_frame++;
      view.mesh_ms_last_frame += timed.duration_ms;
      const bool mesh_matches_current_version =
          world.validate_mesh_result(timed.mesh.coord, timed.mesh.source_version) &&
          world.validate_mesh_content_result(
              timed.mesh.coord,
              timed.mesh.source_content_version);
      if (mesh_matches_current_version) {
        render::ChunkDrawItem draw_item;
        draw_item.key = {timed.mesh.world_id, timed.mesh.coord};
        draw_item.expected_version = timed.mesh.source_version;
        draw_item.expected_light_version = timed.mesh.source_light_version;
        draw_item.has_opaque_mesh = !timed.mesh.opaque.indices.empty();
        draw_item.has_transparent_mesh = !timed.mesh.transparent.indices.empty();
        render::ChunkMeshUpload upload;
        upload.mesh = std::move(timed.mesh);
        renderer.enqueue_chunk_mesh_upload(std::move(upload));
        if (voxel::ChunkSection* upload_chunk = search.find_mutable_chunk(draw_item.key.coord);
            upload_chunk != nullptr) {
          upload_chunk->mark_mesh_waiting_for_upload();
        }
        const auto existing_draw_item = view.draw_items.find(draw_item.key.coord);
        if (existing_draw_item == view.draw_items.end() ||
            render::chunk_mesh_upload_at_least_as_new(
                draw_item.expected_version,
                draw_item.expected_light_version,
                existing_draw_item->second.expected_version,
                existing_draw_item->second.expected_light_version)) {
          view.draw_items[draw_item.key.coord] = draw_item;
          set_draw_items_dirty(view);
        }
      } else if (is_inside_render_view(view, timed.mesh.coord)) {
        enqueue_mesh_candidate(view, timed.mesh.coord);
      }
    }

    view.pending_meshes.erase(
        view.pending_meshes.begin() + static_cast<isize>(index));
  }
}

void drain_mesh_upload_receipts(
    world::WorldRuntime& world,
    render::RenderCommandQueue& renderer,
    ChunkViewState& view) {
  world::WorldSearch search{world};
  for (const render::ChunkMeshUploadReceipt& receipt :
       renderer.drain_chunk_mesh_upload_receipts()) {
    if (!receipt.accepted) {
      if (voxel::ChunkSection* chunk = search.find_mutable_chunk(receipt.key.coord);
          chunk != nullptr) {
        const voxel::ChunkDirtyFlags mesh_flags =
            chunk->dirty_flags() &
            (voxel::ChunkDirtyFlags::Geometry | voxel::ChunkDirtyFlags::LightMesh);
        chunk->mark_dirty(
            voxel::any(mesh_flags) ? mesh_flags : voxel::ChunkDirtyFlags::Geometry);
      }
      if (is_inside_render_view(view, receipt.key.coord)) {
        enqueue_mesh_candidate(view, receipt.key.coord);
      }
      continue;
    }

    const bool published = world.publish_mesh_result(
        receipt.key.coord,
        receipt.source_version,
        receipt.source_light_version,
        receipt.source_content_version);
    if (!published && is_inside_render_view(view, receipt.key.coord)) {
      enqueue_mesh_candidate(view, receipt.key.coord);
      continue;
    }
    if (published) {
      const voxel::ChunkSection* chunk = search.find_chunk(receipt.key.coord);
      if (chunk != nullptr &&
          chunk->is_dirty(voxel::ChunkDirtyFlags::Geometry | voxel::ChunkDirtyFlags::LightMesh) &&
          is_inside_render_view(view, receipt.key.coord)) {
        enqueue_mesh_candidate(view, receipt.key.coord);
      }
    }
  }
}

void schedule_mesh_jobs(
    world::WorldRuntime& world,
    render::RenderCommandQueue& renderer,
    sched::JobSystem& job_system,
    ChunkViewState& view,
    const io::ChunkPipelineConfig& pipeline,
    const voxel::BlockRegistry& block_registry,
    const voxel::BlockRenderData& block_render_data,
    const render::FluidRenderData* fluid_render_data) {
  ARV_PROFILE_ZONE("Chunk meshing schedule");
  if (pipeline.max_mesh_jobs_per_frame == 0U) {
    return;
  }
  sort_mesh_candidates(world, view);

  u32 mesh_batches_scheduled = 0;
  u32 mesh_sections_prepared = 0;
  const u32 max_sections_per_job =
      std::max<u32>(pipeline.max_mesh_sections_per_job, 1U);
  const u32 max_sections_prepared_per_frame =
      std::max<u32>(pipeline.max_mesh_sections_prepared_per_frame, 1U);
  const usize soft_upload_backlog =
      std::max<usize>(
          static_cast<usize>(pipeline.max_mesh_jobs_per_frame) *
              static_cast<usize>(max_sections_per_job) * 4U,
          16U);
  const bool upload_backlog_high = renderer.pending_mesh_upload_count() >= soft_upload_backlog;
  world::WorldSearch search{world};
  while (!view.mesh_candidates.empty() &&
         mesh_sections_prepared < max_sections_prepared_per_frame &&
         mesh_batches_scheduled < pipeline.max_mesh_jobs_per_frame &&
         view.pending_meshes.size() < pipeline.max_mesh_jobs_per_frame) {
    std::vector<MeshJobInput> inputs;
    std::vector<ChunkViewState::PendingMeshEntry> entries;
    inputs.reserve(max_sections_per_job);
    entries.reserve(max_sections_per_job);
    bool stop_after_batch = false;

    while (!view.mesh_candidates.empty() &&
           mesh_sections_prepared < max_sections_prepared_per_frame &&
           inputs.size() < static_cast<usize>(max_sections_per_job)) {
      const ChunkSectionCoord coord = view.mesh_candidates.front();
      view.mesh_candidates.pop_front();
      view.mesh_candidate_set.erase(coord);

      if (!is_inside_render_view(view, coord) ||
          view.mesh_in_flight.find(coord) != view.mesh_in_flight.end()) {
        continue;
      }

      voxel::ChunkSection* chunk = search.find_mutable_chunk(coord);
      if (chunk == nullptr) {
        continue;
      }
      if (chunk_waiting_for_mesh_upload(*chunk)) {
        continue;
      }
      const bool geometry_dirty = chunk->is_dirty(voxel::ChunkDirtyFlags::Geometry);
      const bool light_mesh_dirty = chunk->is_dirty(voxel::ChunkDirtyFlags::LightMesh);
      const bool mesh_dirty = geometry_dirty || light_mesh_dirty;
      const bool has_draw_item = view.draw_items.find(coord) != view.draw_items.end();
      const u64 content_version = chunk->content_version();
      const bool chunk_known_empty =
          is_known_empty_mesh(view, coord, content_version) ||
          chunk_section_is_empty_for_rendering(*chunk);
      if (chunk_known_empty) {
        ++mesh_sections_prepared;
        view.empty_mesh_content_versions[coord] = content_version;
        if (mesh_dirty) {
          const bool published = world.publish_mesh_result(
              coord,
              chunk->block_version(),
              chunk->light_version(),
              chunk->content_version());
          if (!published) {
            enqueue_mesh_candidate(view, coord);
            continue;
          }
          remove_empty_chunk_mesh(renderer, view, world.id(), coord);
        } else if (has_draw_item) {
          remove_empty_chunk_mesh(renderer, view, world.id(), coord);
        }
        if (mesh_dirty || !has_draw_item) {
          ++view.empty_mesh_skips_last_frame;
        }
        continue;
      }
      if (!mesh_dirty && has_draw_item) {
        continue;
      }
      if (upload_backlog_high && !geometry_dirty && has_draw_item) {
        enqueue_mesh_candidate(view, coord);
        stop_after_batch = true;
        break;
      }
      view.empty_mesh_content_versions.erase(coord);
      ++mesh_sections_prepared;

      std::optional<voxel::ChunkSnapshot> snapshot =
          world.make_meshing_snapshot(
              coord,
              world::MissingNeighborPolicy::TreatMissingAsAir);
      if (!snapshot) {
        continue;
      }

      entries.push_back(
          {coord,
           snapshot->source_version,
           snapshot->source_content_version,
           snapshot->source_light_version});
      inputs.push_back({coord, std::move(*snapshot)});
    }

    if (inputs.empty()) {
      if (stop_after_batch) {
        break;
      }
      continue;
    }

    const WorldID world_id = world.id();
    sched::JobDesc desc;
    desc.name = inputs.size() == 1U ? "view_chunk_meshing" : "view_chunk_meshing_batch";
    desc.lane = sched::JobLane::BackgroundCpu;
    desc.domain = sched::JobDomain::Meshing;
    desc.completion_target = sched::JobCompletionTarget::Render;
    auto job = job_system.submit_result(
        std::move(desc),
        [world_id,
         inputs = std::move(inputs),
         &block_registry,
         &block_render_data,
         fluid_render_data] {
          ChunkViewState::TimedChunkMeshBatchResult batch;
          batch.meshes.reserve(inputs.size());
          for (const MeshJobInput& input : inputs) {
            const auto start = std::chrono::steady_clock::now();
            auto mesh = render::build_chunk_mesh(
                world_id,
                input.snapshot,
                block_registry,
                &block_render_data,
                fluid_render_data);
            double duration_ms = 0.0;
            if constexpr (core::profiling_enabled()) {
              const auto end = std::chrono::steady_clock::now();
              duration_ms =
                  std::chrono::duration<double, std::milli>(end - start).count();
            } else {
              (void)start;
            }
            batch.meshes.push_back({std::move(mesh), duration_ms});
          }
          return batch;
        });
    if (!job.valid()) {
      for (const auto& entry : entries) {
        enqueue_mesh_candidate(view, entry.coord);
      }
      break;
    }

    for (const auto& entry : entries) {
      if (voxel::ChunkSection* chunk = search.find_mutable_chunk(entry.coord);
          chunk != nullptr) {
        chunk->mark_mesh_queued();
      }
      view.mesh_in_flight.insert(entry.coord);
    }
    view.pending_meshes.emplace_back(std::move(entries), std::move(job));
    ++mesh_batches_scheduled;
    if (stop_after_batch) {
      break;
    }
  }
  view.mesh_sections_prepared_last_frame = mesh_sections_prepared;
}

void log_chunk_view_profile(
    ChunkViewState& view,
    render::RenderCommandQueue& renderer,
    const io::ChunkPipelineConfig& pipeline,
    std::chrono::steady_clock::time_point refresh_start) {
  const auto refresh_end = std::chrono::steady_clock::now();
  const double refresh_ms =
      std::chrono::duration<double, std::milli>(refresh_end - refresh_start).count();
  if (refresh_ms > static_cast<double>(pipeline.frame_spike_log_ms) &&
      cooldown_due(view.last_spike_log, refresh_end, SpikeLogCooldown)) {
    arv::core::warn(
        "chunk_profile",
        "chunk view update spike {}ms queued_mesh_batches={} mesh_sections_in_flight={} mesh_sections_prepared={} mesh_candidates={}",
        refresh_ms,
        view.pending_meshes.size(),
        view.mesh_in_flight.size(),
        view.mesh_sections_prepared_last_frame,
        view.mesh_candidates.size());
    view.last_spike_log = refresh_end;
  }

  if (view.last_profile_log.time_since_epoch().count() == 0) {
    view.last_profile_log = refresh_end;
  }
  if (pipeline.profile_log_interval_ms == 0U) {
    return;
  }
  if (std::chrono::duration_cast<std::chrono::milliseconds>(refresh_end - view.last_profile_log)
          .count() >= static_cast<long long>(pipeline.profile_log_interval_ms)) {
    const bool has_worker_activity =
        !view.pending_meshes.empty() ||
        view.mesh_completed_last_frame > 0U ||
        view.empty_mesh_skips_last_frame > 0U ||
        renderer.pending_mesh_upload_count() > 0U ||
        renderer.mesh_uploads_last_frame() > 0U;
    if (!has_worker_activity) {
      view.last_profile_log = refresh_end;
      return;
    }

    arv::core::info(
        "chunk_profile",
        "workers queued_mesh_batches={} mesh_sections_in_flight={} mesh_sections_prepared={} completed_mesh={} empty_mesh_skips={} mesh_ms={} pending_uploads={} "
        "uploads_last_frame={} upload_discards={} upload_ms={}",
        view.pending_meshes.size(),
        view.mesh_in_flight.size(),
        view.mesh_sections_prepared_last_frame,
        view.mesh_completed_last_frame,
        view.empty_mesh_skips_last_frame,
        view.mesh_ms_last_frame,
        renderer.pending_mesh_upload_count(),
        renderer.mesh_uploads_last_frame(),
        renderer.mesh_upload_discards_last_frame(),
        renderer.mesh_upload_time_ms_last_frame());
    (void)renderer;
    view.last_profile_log = refresh_end;
  }
}

} // namespace

ChunkSectionCoord world_position_chunk_section_coord(Vec3f position) {
  return block_to_chunk({{static_cast<i32>(std::floor(position.x)),
                          static_cast<i32>(std::floor(position.y)),
                          static_cast<i32>(std::floor(position.z))}});
}

ChunkSectionCoord camera_chunk_section_coord(const client::CameraShell& camera) {
  return world_position_chunk_section_coord(camera.position());
}

void request_chunk_view_mesh_rescan(ChunkViewState& view) noexcept {
  view.mesh_rescan_requested = true;
}

void remove_chunk_view_chunk(ChunkViewState& view, ChunkSectionCoord coord) {
  view.draw_items.erase(coord);
  view.empty_mesh_content_versions.erase(coord);
  view.mesh_candidate_set.erase(coord);
  for (auto iter = view.mesh_candidates.begin(); iter != view.mesh_candidates.end();) {
    if (*iter == coord) {
      iter = view.mesh_candidates.erase(iter);
      set_mesh_candidates_sort_dirty(view);
    } else {
      ++iter;
    }
  }
  set_draw_items_dirty(view);
}

bool update_chunk_view(
    world::WorldRuntime& world,
    render::RenderCommandQueue& renderer,
    sched::JobSystem& job_system,
    ChunkViewState& view,
    const io::ChunkDistanceConfig& distances,
    const io::ChunkPipelineConfig& pipeline,
    ChunkSectionCoord center,
    i32 min_chunk_y,
    i32 max_chunk_y,
    const voxel::BlockRegistry& block_registry,
    const voxel::BlockRenderData& block_render_data,
    const render::FluidRenderData* fluid_render_data) {
  ARV_PROFILE_ZONE("Chunk view update");
  const auto refresh_start = std::chrono::steady_clock::now();
  ++view.frames;
  view.mesh_completed_last_frame = 0;
  view.empty_mesh_skips_last_frame = 0;
  view.mesh_sections_prepared_last_frame = 0;
  view.mesh_ms_last_frame = 0.0;
  const u64 entry_mesh_revision = world.mesh_revision();
  const bool mesh_revision_changed_before_update =
      view.observed_mesh_revision != 0U &&
      view.observed_mesh_revision != entry_mesh_revision;

  drain_mesh_upload_receipts(world, renderer, view);
  drain_mesh_jobs(world, renderer, view);

  if (!target_matches(view, distances, center, min_chunk_y, max_chunk_y)) {
    retarget_chunk_view(world, view, distances, center, min_chunk_y, max_chunk_y);
  } else if (view.mesh_rescan_requested || mesh_revision_changed_before_update) {
    enqueue_dirty_mesh_chunks_in_view(world, view);
  }

  schedule_mesh_jobs(
      world,
      renderer,
      job_system,
      view,
      pipeline,
      block_registry,
      block_render_data,
      fluid_render_data);

  log_chunk_view_profile(view, renderer, pipeline, refresh_start);
  view.observed_mesh_revision = world.mesh_revision();
  return true;
}

const std::vector<render::ChunkDrawItem>& chunk_draw_items(ChunkViewState& view) {
  if (!view.draw_items_dirty) {
    return view.cached_draw_items;
  }

  view.cached_draw_items.clear();
  view.cached_draw_items.reserve(view.draw_items.size());
  for (const auto& [coord, item] : view.draw_items) {
    (void)coord;
    view.cached_draw_items.emplace_back(item);
  }
  std::sort(
      view.cached_draw_items.begin(),
      view.cached_draw_items.end(),
      [](const auto& lhs, const auto& rhs) {
        if (lhs.key.coord.value.z != rhs.key.coord.value.z) {
          return lhs.key.coord.value.z < rhs.key.coord.value.z;
        }
        if (lhs.key.coord.value.x != rhs.key.coord.value.x) {
          return lhs.key.coord.value.x < rhs.key.coord.value.x;
        }
        return lhs.key.coord.value.y < rhs.key.coord.value.y;
      });
  view.draw_items_dirty = false;
  return view.cached_draw_items;
}

} // namespace arv::desktop
