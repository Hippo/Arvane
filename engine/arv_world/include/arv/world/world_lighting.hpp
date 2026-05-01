#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/storage.hpp"

#include <array>
#include <vector>

namespace arv::voxel {
class BlockRegistry;
} // namespace arv::voxel

namespace arv::world {

class WorldRuntime;

enum class LightingJobType : u8 {
  SectionRelight,
  BoundaryContinuation,
};

inline constexpr u32 DefaultLightingWorkBudgetNodes =
    static_cast<u32>(voxel::ChunkVolume * 8U);

struct LightingJobStats {
  ChunkCoord section_coord{};
  LightingJobType job_type{LightingJobType::SectionRelight};
  u32 nodes_processed{};
  u32 add_nodes_processed{};
  u32 remove_nodes_processed{};
  u32 boundary_deferrals{};
  u32 sections_touched{};
  u32 sections_published{};
  u32 max_queue_depth{};
  u32 continuation_count{};
  bool touched_neighbor_sections{};
  bool exceeded_soft_budget{};
};

struct ChunkLightingSnapshot {
  ChunkCoord coord{};
  u64 source_version{};
  voxel::ChunkStorageView storage{};
  u64 source_content_version{};
  std::array<u8, voxel::ChunkArea> missing_above_sky{};
  std::array<i32, voxel::ChunkArea> generation_surface_y{};
  bool needs_relight{};
  bool is_target{};
  bool has_generation_surface{};

  [[nodiscard]] const std::array<voxel::BlockVariantID, voxel::ChunkVolume>& blocks() const noexcept {
    return storage->blocks;
  }
};

struct LightingSnapshot {
  WorldID world_id{};
  u64 source_revision{};
  ChunkCoord target_coord{};
  u32 pending_dirty_sections{};
  u32 work_budget_nodes{DefaultLightingWorkBudgetNodes};
  LightingJobType job_type{LightingJobType::SectionRelight};
  std::vector<ChunkLightingSnapshot> chunks{};
};

struct ChunkLightingResult {
  ChunkCoord coord{};
  u64 source_version{};
  std::array<voxel::LightWord, voxel::ChunkVolume> lights{};
  u64 source_content_version{};
  u8 changed_boundary_mask{};
};

struct LightingResult {
  WorldID world_id{};
  u64 source_revision{};
  LightingJobStats stats{};
  bool continuation_requested{};
  std::vector<ChunkLightingResult> chunks{};
};

struct LightingPublishStats {
  u32 accepted_chunks{};
  u32 stale_chunks{};
  u32 changed_chunks{};
  u32 boundary_continuations{};
  u32 boundary_deferrals{};
  u32 remeshes_triggered{};
};

class WorldLighting {
public:
  [[nodiscard]] static bool has_work(const WorldRuntime& world) noexcept;
  [[nodiscard]] static LightingSnapshot make_snapshot(const WorldRuntime& world);
  [[nodiscard]] static LightingResult build_result(
      const LightingSnapshot& snapshot,
      const voxel::BlockRegistry& registry);
  static LightingPublishStats publish_result(WorldRuntime& world, LightingResult result) noexcept;
};

} // namespace arv::world
