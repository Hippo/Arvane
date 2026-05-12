#include "world_runtime_internal.hpp"

#include "arv/core/profiling.hpp"

#include <cassert>
#include <memory>
#include <thread>
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

void WorldRuntime::bind_owner_thread() noexcept {
  impl_->owner_thread_id = std::this_thread::get_id();
}

bool WorldRuntime::is_owner_thread() const noexcept {
  return impl_->owner_thread_id == std::this_thread::get_id();
}

void WorldRuntime::assert_owner_thread() const noexcept {
  assert(is_owner_thread() && "WorldRuntime mutation must run on the world owner thread");
}

TickIndex WorldRuntime::tick_index() const noexcept {
  return impl_->tick_index;
}

void WorldRuntime::set_tick_index(TickIndex tick_index) noexcept {
  assert_owner_thread();
  impl_->tick_index = tick_index;
  impl_->clock.world_tick = tick_index;
}

const WorldClock& WorldRuntime::clock() const noexcept {
  return impl_->clock;
}

void WorldRuntime::configure_clock(
    Seconds day_length_seconds,
    f32 start_day_fraction,
    TickRate fixed_tick_rate,
    bool daylight_cycle,
    f32 time_scale) noexcept {
  assert_owner_thread();
  const TickIndex world_tick = impl_->tick_index;
  impl_->clock = make_world_clock(
      fixed_tick_rate,
      day_length_seconds,
      start_day_fraction,
      daylight_cycle,
      time_scale);
  impl_->clock.world_tick = world_tick;
}

void WorldRuntime::set_clock(WorldClock clock, TickRate fixed_tick_rate) noexcept {
  assert_owner_thread();
  retime_world_clock(clock, fixed_tick_rate);
  impl_->clock = clock;
  impl_->tick_index = impl_->clock.world_tick;
}

void WorldRuntime::set_day_time_tick(u32 day_time_tick) noexcept {
  assert_owner_thread();
  world::set_day_time_tick(impl_->clock, day_time_tick);
}

void WorldRuntime::set_day_fraction(f32 day_fraction) noexcept {
  assert_owner_thread();
  world::set_day_fraction(impl_->clock, day_fraction);
}

void WorldRuntime::set_daylight_cycle(bool daylight_cycle) noexcept {
  assert_owner_thread();
  impl_->clock.daylight_cycle = daylight_cycle;
}

void WorldRuntime::set_time_scale(f32 time_scale) noexcept {
  assert_owner_thread();
  impl_->clock.time_scale = time_scale;
}

void WorldRuntime::set_day_length_seconds(
    Seconds day_length_seconds,
    TickRate fixed_tick_rate) noexcept {
  assert_owner_thread();
  const f32 fraction = day_fraction(impl_->clock);
  impl_->clock.day_length_seconds = day_length_seconds;
  retime_world_clock(impl_->clock, fixed_tick_rate);
  set_day_fraction(fraction);
}

void WorldRuntime::set_generation_context(u64 seed, WorldGenerationConfig config) {
  assert_owner_thread();
  configure_height(world_height_settings_from_generation_config(config));
  for (auto& [coord, chunk] : impl_->chunks.chunks()) {
    (void)coord;
    chunk.clear_generation_surface_profile();
  }
  impl_->generation_seed = seed;
  impl_->generation_config = std::move(config);
}

void WorldRuntime::clear_generation_context() noexcept {
  assert_owner_thread();
  for (auto& [coord, chunk] : impl_->chunks.chunks()) {
    (void)coord;
    chunk.clear_generation_surface_profile();
  }
  impl_->generation_config.reset();
  impl_->generation_seed = 0U;
}

WorldTickStats WorldRuntime::tick(
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& fluid_definition) {
  assert_owner_thread();
  ARV_PROFILE_ZONE("Simulation tick");
  tick_world_clock(impl_->clock);
  impl_->tick_index = impl_->clock.world_tick;

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
