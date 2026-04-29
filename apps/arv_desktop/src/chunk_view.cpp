#include "chunk_view.hpp"

#include "arv/render/chunk_mesher.hpp"
#include "arv/render/opengl_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <utility>

namespace arv::desktop {
namespace {

[[nodiscard]] i32 abs_i32(i32 value) noexcept {
  return value < 0 ? -value : value;
}

[[nodiscard]] bool is_inside_horizontal_distance(
    ChunkCoord coord,
    ChunkCoord center,
    i32 radius) noexcept {
  return abs_i32(coord.value.x - center.value.x) <= radius &&
         abs_i32(coord.value.z - center.value.z) <= radius;
}

[[nodiscard]] bool target_matches(
    const ChunkViewState& view,
    const io::ChunkDistanceConfig& distances,
    ChunkCoord center,
    i32 min_chunk_y,
    i32 max_chunk_y) noexcept {
  return view.target_center.has_value() &&
         view.target_center.value() == center &&
         view.target_min_chunk_y == min_chunk_y &&
         view.target_max_chunk_y == max_chunk_y &&
         view.target_render_distance == distances.render_distance;
}

[[nodiscard]] bool is_inside_target_y(const ChunkViewState& view, ChunkCoord coord) noexcept {
  return coord.value.y >= view.target_min_chunk_y &&
         coord.value.y <= view.target_max_chunk_y;
}

[[nodiscard]] bool is_inside_render_view(const ChunkViewState& view, ChunkCoord coord) noexcept {
  return view.target_center.has_value() &&
         is_inside_target_y(view, coord) &&
         is_inside_horizontal_distance(
             coord,
             view.target_center.value(),
             view.target_render_distance);
}

[[nodiscard]] i32 chunk_priority_distance(ChunkCoord coord, ChunkCoord center) noexcept {
  const i32 horizontal = std::max(
      abs_i32(coord.value.x - center.value.x),
      abs_i32(coord.value.z - center.value.z));
  const i32 vertical = abs_i32(coord.value.y - center.value.y);
  return horizontal * 4 + vertical;
}

[[nodiscard]] u32 mesh_candidate_rank(
    const ChunkViewState& view,
    const voxel::ChunkSection* chunk,
    ChunkCoord coord) noexcept {
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
  if (!view.target_center.has_value() || view.mesh_candidates.size() < 2U) {
    return;
  }

  const ChunkCoord center = view.target_center.value();
  std::stable_sort(
      view.mesh_candidates.begin(),
      view.mesh_candidates.end(),
      [&world, &view, center](ChunkCoord lhs, ChunkCoord rhs) {
        const voxel::ChunkSection* lhs_chunk = world.find_chunk(lhs);
        const voxel::ChunkSection* rhs_chunk = world.find_chunk(rhs);
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
}

void set_draw_items_dirty(ChunkViewState& view) noexcept {
  view.draw_items_dirty = true;
}

void enqueue_mesh_candidate(ChunkViewState& view, ChunkCoord coord) {
  if (!is_inside_render_view(view, coord)) {
    return;
  }
  if (view.mesh_candidate_set.insert(coord).second) {
    view.mesh_candidates.push_back(coord);
  }
}

void remove_draw_items_outside_view(ChunkViewState& view) {
  for (auto iter = view.draw_items.begin(); iter != view.draw_items.end();) {
    if (!is_inside_render_view(view, iter->first)) {
      iter = view.draw_items.erase(iter);
      set_draw_items_dirty(view);
    } else {
      ++iter;
    }
  }
}

void clear_mesh_candidates(ChunkViewState& view) {
  view.mesh_candidates.clear();
  view.mesh_candidate_set.clear();
}

void rebuild_mesh_candidates(world::WorldRuntime& world, ChunkViewState& view) {
  clear_mesh_candidates(view);

  const ChunkCoord center = view.target_center.value();
  const i32 render_distance = view.target_render_distance;
  for (i32 z = center.value.z - render_distance; z <= center.value.z + render_distance; ++z) {
    for (i32 x = center.value.x - render_distance; x <= center.value.x + render_distance; ++x) {
      for (i32 y = view.target_min_chunk_y; y <= view.target_max_chunk_y; ++y) {
        const ChunkCoord coord{{x, y, z}};
        if (world.find_chunk(coord) != nullptr) {
          enqueue_mesh_candidate(view, coord);
        }
      }
    }
  }
}

void enqueue_dirty_mesh_chunks_in_view(world::WorldRuntime& world, ChunkViewState& view) {
  for (const ChunkCoord coord : world.dirty_chunks(
           voxel::ChunkDirtyFlags::Geometry | voxel::ChunkDirtyFlags::LightMesh)) {
    enqueue_mesh_candidate(view, coord);
  }
  view.mesh_rescan_requested = false;
}

void retarget_chunk_view(
    world::WorldRuntime& world,
    ChunkViewState& view,
    const io::ChunkDistanceConfig& distances,
    ChunkCoord center,
    i32 min_chunk_y,
    i32 max_chunk_y) {
  view.target_center = center;
  view.target_min_chunk_y = min_chunk_y;
  view.target_max_chunk_y = max_chunk_y;
  view.target_render_distance = distances.render_distance;

  remove_draw_items_outside_view(view);
  rebuild_mesh_candidates(world, view);
  view.mesh_rescan_requested = false;
}

void drain_mesh_jobs(
    world::WorldRuntime& world,
    render::OpenGLRenderer& renderer,
    ChunkViewState& view) {
  for (usize index = 0; index < view.pending_meshes.size();) {
    auto& pending = view.pending_meshes[index];
    if (!pending.job.is_complete()) {
      ++index;
      continue;
    }

    auto timed = pending.job.get();
    view.mesh_in_flight.erase(pending.coord);
    view.mesh_completed_last_frame++;
    view.mesh_ms_last_frame += timed.duration_ms;
    const bool mesh_matches_current_version =
        world.validate_mesh_result(timed.mesh.coord, timed.mesh.source_version);
    if (mesh_matches_current_version) {
      render::ChunkDrawItem draw_item;
      draw_item.key = {timed.mesh.world_id, timed.mesh.coord};
      draw_item.expected_version = timed.mesh.source_version;
      draw_item.expected_light_version = timed.mesh.source_light_version;
      render::ChunkMeshUpload upload;
      upload.mesh = std::move(timed.mesh);
      renderer.enqueue_chunk_mesh_upload(std::move(upload));
      (void)world.publish_mesh_result(
          draw_item.key.coord,
          draw_item.expected_version,
          draw_item.expected_light_version);
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
      if (const voxel::ChunkSection* chunk = world.find_chunk(draw_item.key.coord);
          chunk != nullptr && chunk->is_dirty(voxel::ChunkDirtyFlags::LightMesh)) {
        enqueue_mesh_candidate(view, draw_item.key.coord);
      }
    } else if (is_inside_render_view(view, pending.coord)) {
      enqueue_mesh_candidate(view, pending.coord);
    }

    view.pending_meshes.erase(
        view.pending_meshes.begin() + static_cast<isize>(index));
  }
}

void schedule_mesh_jobs(
    world::WorldRuntime& world,
    render::OpenGLRenderer& renderer,
    sched::Scheduler& scheduler,
    ChunkViewState& view,
    const io::ChunkPipelineConfig& pipeline,
    const voxel::BlockRegistry& block_registry,
    const voxel::BlockRenderData& block_render_data) {
  sort_mesh_candidates(world, view);

  u32 mesh_jobs_scheduled = 0;
  const usize soft_upload_backlog =
      std::max<usize>(static_cast<usize>(pipeline.max_mesh_jobs_per_frame) * 4U, 16U);
  const bool upload_backlog_high = renderer.pending_mesh_upload_count() >= soft_upload_backlog;
  while (!view.mesh_candidates.empty() &&
         mesh_jobs_scheduled < pipeline.max_mesh_jobs_per_frame &&
         view.pending_meshes.size() < pipeline.max_mesh_jobs_per_frame) {
    const ChunkCoord coord = view.mesh_candidates.front();
    view.mesh_candidates.pop_front();
    view.mesh_candidate_set.erase(coord);

    if (!is_inside_render_view(view, coord) ||
        view.mesh_in_flight.find(coord) != view.mesh_in_flight.end()) {
      continue;
    }

    voxel::ChunkSection* chunk = world.find_chunk(coord);
    if (chunk == nullptr) {
      continue;
    }
    if (!chunk->is_dirty(voxel::ChunkDirtyFlags::Geometry) &&
        !chunk->is_dirty(voxel::ChunkDirtyFlags::LightMesh) &&
        view.draw_items.find(coord) != view.draw_items.end()) {
      continue;
    }
    if (upload_backlog_high &&
        !chunk->is_dirty(voxel::ChunkDirtyFlags::Geometry) &&
        view.draw_items.find(coord) != view.draw_items.end()) {
      enqueue_mesh_candidate(view, coord);
      break;
    }

    const std::optional<voxel::ChunkSnapshot> snapshot =
        world.make_meshing_snapshot(
            coord,
            world::MissingNeighborPolicy::TreatMissingAsAir);
    if (!snapshot) {
      continue;
    }

    const WorldID world_id = world.id();
    const u64 source_version = snapshot->source_version;
    const u64 source_light_version = snapshot->source_light_version;
    auto job = scheduler.submit(
        "view_chunk_meshing",
        [world_id, snapshot = snapshot.value(), &block_registry, &block_render_data] {
      const auto start = std::chrono::steady_clock::now();
      auto mesh = render::build_chunk_mesh(
          world_id,
          snapshot,
          block_registry,
          &block_render_data);
      const auto end = std::chrono::steady_clock::now();
      return ChunkViewState::TimedChunkMeshResult{
          std::move(mesh),
          std::chrono::duration<double, std::milli>(end - start).count()};
    });
    chunk->mark_mesh_queued();
    view.mesh_in_flight.insert(coord);
    view.pending_meshes.push_back({coord, source_version, source_light_version, std::move(job)});
    ++mesh_jobs_scheduled;
  }
}

void log_chunk_view_profile(
    ChunkViewState& view,
    render::OpenGLRenderer& renderer,
    const io::ChunkPipelineConfig& pipeline,
    core::Logger& logger,
    std::chrono::steady_clock::time_point refresh_start) {
  const auto refresh_end = std::chrono::steady_clock::now();
  const double refresh_ms =
      std::chrono::duration<double, std::milli>(refresh_end - refresh_start).count();
  if (refresh_ms > static_cast<double>(pipeline.frame_spike_log_ms)) {
    std::ostringstream message;
    message << "chunk view update spike " << refresh_ms << "ms"
            << " queued_mesh=" << view.pending_meshes.size()
            << " mesh_candidates=" << view.mesh_candidates.size();
    logger.warn("chunk_profile", message.str());
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
        renderer.pending_mesh_upload_count() > 0U ||
        renderer.mesh_uploads_last_frame() > 0U;
    if (!has_worker_activity) {
      view.last_profile_log = refresh_end;
      return;
    }

    std::ostringstream message;
    message << "workers queued_mesh=" << view.pending_meshes.size()
            << " completed_mesh=" << view.mesh_completed_last_frame
            << " mesh_ms=" << view.mesh_ms_last_frame
            << " pending_uploads=" << renderer.pending_mesh_upload_count()
            << " uploads_last_frame=" << renderer.mesh_uploads_last_frame()
            << " upload_ms=" << renderer.mesh_upload_time_ms_last_frame();
    logger.info("chunk_profile", message.str());
    (void)renderer;
    view.last_profile_log = refresh_end;
  }
}

} // namespace

ChunkCoord world_position_chunk_coord(Vec3f position) {
  return block_to_chunk({{
      static_cast<i32>(std::floor(position.x)),
      static_cast<i32>(std::floor(position.y)),
      static_cast<i32>(std::floor(position.z))}});
}

ChunkCoord camera_chunk_coord(const client::CameraShell& camera) {
  return world_position_chunk_coord(camera.position());
}

std::pair<i32, i32> visible_worldgen_chunk_y_range(
    const world::WorldGenerationConfig& config) {
  const f32 height_extent =
      std::abs(static_cast<f32>(config.height_scale)) * config.terrain_amplitude + 4.0F;
  const i32 surface_min =
      static_cast<i32>(std::floor(static_cast<f32>(config.base_height) - height_extent));
  const i32 surface_max =
      static_cast<i32>(std::ceil(static_cast<f32>(config.base_height) + height_extent));
  const i32 min_y = std::max(config.min_y, surface_min - ChunkSize);
  const i32 max_y = std::min(config.max_y, surface_max);
  return {floor_div(min_y, ChunkSize), floor_div(max_y, ChunkSize)};
}

void request_chunk_view_mesh_rescan(ChunkViewState& view) noexcept {
  view.mesh_rescan_requested = true;
}

void remove_chunk_view_chunk(ChunkViewState& view, ChunkCoord coord) {
  view.draw_items.erase(coord);
  view.mesh_candidate_set.erase(coord);
  for (auto iter = view.mesh_candidates.begin(); iter != view.mesh_candidates.end();) {
    if (*iter == coord) {
      iter = view.mesh_candidates.erase(iter);
    } else {
      ++iter;
    }
  }
  set_draw_items_dirty(view);
}

bool update_chunk_view(
    world::WorldRuntime& world,
    render::OpenGLRenderer& renderer,
    sched::Scheduler& scheduler,
    ChunkViewState& view,
    const io::ChunkDistanceConfig& distances,
    const io::ChunkPipelineConfig& pipeline,
    ChunkCoord center,
    i32 min_chunk_y,
    i32 max_chunk_y,
    const voxel::BlockRegistry& block_registry,
    const voxel::BlockRenderData& block_render_data,
    core::Logger& logger) {
  const auto refresh_start = std::chrono::steady_clock::now();
  ++view.frames;
  view.mesh_completed_last_frame = 0;
  view.mesh_ms_last_frame = 0.0;
  const u64 entry_mesh_revision = world.mesh_revision();
  const bool mesh_revision_changed_before_update =
      view.observed_mesh_revision != 0U &&
      view.observed_mesh_revision != entry_mesh_revision;

  drain_mesh_jobs(world, renderer, view);

  if (!target_matches(view, distances, center, min_chunk_y, max_chunk_y)) {
    retarget_chunk_view(world, view, distances, center, min_chunk_y, max_chunk_y);
  } else if (view.mesh_rescan_requested || mesh_revision_changed_before_update) {
    enqueue_dirty_mesh_chunks_in_view(world, view);
  }

  schedule_mesh_jobs(
      world,
      renderer,
      scheduler,
      view,
      pipeline,
      block_registry,
      block_render_data);

  log_chunk_view_profile(view, renderer, pipeline, logger, refresh_start);
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
    view.cached_draw_items.push_back(item);
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
