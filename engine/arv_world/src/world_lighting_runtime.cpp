#include "world_runtime_internal.hpp"

#include "arv/core/profiling.hpp"
#include "arv/world/world_lighting.hpp"

#include <utility>

namespace arv::world::detail {

bool WorldLightingRuntime::has_work(const WorldRuntime& world) const noexcept {
  return WorldLighting::has_work(world);
}

LightingSnapshot WorldLightingRuntime::make_snapshot(const WorldRuntime& world) const {
  return WorldLighting::make_snapshot(world);
}

LightingResult WorldLightingRuntime::build_result(
    const LightingSnapshot& snapshot,
    const voxel::BlockRegistry& registry) {
  return WorldLighting::build_result(snapshot, registry);
}

LightingPublishStats WorldLightingRuntime::publish_result(
    WorldRuntime& world,
    LightingResult result) noexcept {
  return WorldLighting::publish_result(world, std::move(result));
}

} // namespace arv::world::detail

namespace arv::world {

bool WorldRuntime::has_lighting_work() const noexcept {
  return impl_->dirty.has_lighting_work();
}

LightingSnapshot WorldRuntime::make_lighting_snapshot() const {
  ARV_PROFILE_ZONE("Snapshot creation");
  LightingSnapshot snapshot = impl_->lighting.make_snapshot(*this);
  impl_->dirty.set_last_lighting_snapshot_chunk_count(snapshot.pending_dirty_sections);
  return snapshot;
}

LightingResult WorldRuntime::build_lighting_result(
    const LightingSnapshot& snapshot,
    const voxel::BlockRegistry& registry) {
  return detail::WorldLightingRuntime::build_result(snapshot, registry);
}

LightingPublishStats WorldRuntime::publish_lighting_result(LightingResult result) noexcept {
  const LightingPublishStats stats = impl_->lighting.publish_result(*this, std::move(result));
  if (stats.changed_chunks > 0U || stats.remeshes_triggered > 0U) {
    impl_->dirty.mark_mesh_content_dirty();
  }
  impl_->dirty.recompute_lighting_work_pending(*this);
  return stats;
}

} // namespace arv::world
