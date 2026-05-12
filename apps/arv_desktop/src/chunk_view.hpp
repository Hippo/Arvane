#pragma once

#include "arv/client/camera_shell.hpp"
#include "arv/core/log.hpp"
#include "arv/core/types.hpp"
#include "arv/io/config.hpp"
#include "arv/render/render_commands.hpp"
#include "arv/render/render_types.hpp"
#include "arv/sched/job_system.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/world_runtime.hpp"

#include <chrono>
#include <deque>
#include <optional>
#include <vector>

namespace arv::desktop {

struct ChunkViewState {
  struct TimedChunkMeshResult {
    render::ChunkMeshData mesh;
    double duration_ms{};
  };

  struct PendingMesh {
    ChunkSectionCoord coord{};
    u64 source_version{};
    u64 source_content_version{};
    u64 source_light_version{};
    sched::JobFuture<TimedChunkMeshResult> job;
  };

  HashMap<ChunkSectionCoord, render::ChunkDrawItem> draw_items;
  std::vector<PendingMesh> pending_meshes;
  HashSet<ChunkSectionCoord> mesh_in_flight;

  std::optional<ChunkSectionCoord> target_center;
  i32 target_min_chunk_y{};
  i32 target_max_chunk_y{};
  i32 target_render_distance{};
  std::deque<ChunkSectionCoord> mesh_candidates;
  HashSet<ChunkSectionCoord> mesh_candidate_set;
  u64 observed_mesh_revision{};
  bool mesh_rescan_requested{true};
  bool mesh_candidates_sort_dirty{true};
  bool draw_items_dirty{true};
  std::vector<render::ChunkDrawItem> cached_draw_items;

  std::chrono::steady_clock::time_point last_profile_log{};
  u64 frames{};
  u32 mesh_completed_last_frame{};
  double mesh_ms_last_frame{};
  usize visible_chunks_last_frame{};
  usize opaque_chunks_last_frame{};
  usize transparent_chunks_last_frame{};
  usize distance_culled_chunks_last_frame{};
  usize frustum_culled_chunks_last_frame{};
};

[[nodiscard]] ChunkSectionCoord world_position_chunk_section_coord(Vec3f position);

[[nodiscard]] ChunkSectionCoord camera_chunk_section_coord(const client::CameraShell& camera);

void request_chunk_view_mesh_rescan(ChunkViewState& view) noexcept;
void remove_chunk_view_chunk(ChunkViewState& view, ChunkSectionCoord coord);

[[nodiscard]] bool update_chunk_view(
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
    const voxel::BlockRenderData& block_render_data);

[[nodiscard]] const std::vector<render::ChunkDrawItem>& chunk_draw_items(ChunkViewState& view);

} // namespace arv::desktop
