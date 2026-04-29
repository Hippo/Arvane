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

struct ChunkLightingSnapshot {
  ChunkCoord coord{};
  u64 source_version{};
  voxel::ChunkStorageView storage{};
  u64 source_content_version{};
  bool needs_relight{};

  [[nodiscard]] const std::array<voxel::BlockVariantID, voxel::ChunkVolume>& blocks() const noexcept {
    return storage->blocks;
  }
};

struct LightingSnapshot {
  WorldID world_id{};
  u64 source_revision{};
  std::vector<ChunkLightingSnapshot> chunks{};
};

struct ChunkLightingResult {
  ChunkCoord coord{};
  u64 source_version{};
  std::array<voxel::LightWord, voxel::ChunkVolume> lights{};
  u64 source_content_version{};
};

struct LightingResult {
  WorldID world_id{};
  u64 source_revision{};
  std::vector<ChunkLightingResult> chunks{};
};

struct LightingPublishStats {
  u32 accepted_chunks{};
  u32 stale_chunks{};
  u32 changed_chunks{};
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
