#include "world_runtime_internal.hpp"

#include "arv/core/profiling.hpp"

#include <memory>
#include <utility>

namespace arv::world {

WorldRuntime::WorldRuntime(WorldID id) : impl_(std::make_unique<Impl>(id)) {
}

WorldRuntime::~WorldRuntime() = default;

WorldRuntime::WorldRuntime(WorldRuntime&&) noexcept = default;

WorldRuntime& WorldRuntime::operator=(WorldRuntime&&) noexcept = default;

WorldID WorldRuntime::id() const noexcept {
  return impl_->id;
}

TickIndex WorldRuntime::tick_index() const noexcept {
  return impl_->tick_index;
}

void WorldRuntime::set_tick_index(TickIndex tick_index) noexcept {
  impl_->tick_index = tick_index;
}

void WorldRuntime::set_generation_context(u64 seed, WorldGenerationConfig config) {
  impl_->generation_seed = seed;
  impl_->generation_config = std::move(config);
}

void WorldRuntime::clear_generation_context() noexcept {
  impl_->generation_config.reset();
  impl_->generation_seed = 0U;
}

WorldTickStats WorldRuntime::tick(
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& fluid_definition) {
  ARV_PROFILE_ZONE("Simulation tick");
  ++impl_->tick_index;

  WorldTickStats stats;
  stats.entity_updates_processed =
      impl_->entity_simulation.tick_entities(*this, impl_->entities, impl_->regions, &registry);
  stats.scheduled_ticks_processed =
      impl_->tick_queues.process_due_scheduled_ticks(impl_->tick_index);

  const WorldTickPolicy policy = impl_->tick_queues.policy();
  if (policy.block_update_interval == 0U ||
      impl_->tick_index % policy.block_update_interval == 0U) {
    stats.block_updates_processed =
        impl_->tick_queues.drain_block_updates(policy.block_update_budget);
  }

  if (policy.fluid_interval == 0U ||
      impl_->tick_index % policy.fluid_interval == 0U) {
    stats.fluid_updates_processed =
        impl_->fluid.process_updates(*this, registry, fluid_definition, policy.fluid_budget);
  }

  return stats;
}

} // namespace arv::world
