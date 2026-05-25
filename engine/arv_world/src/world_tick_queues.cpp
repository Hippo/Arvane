#include "world_runtime_internal.hpp"

namespace arv::world::detail {

void WorldTickQueues::set_policy(WorldTickPolicy policy) noexcept {
  tick_policy_ = policy;
}

WorldTickPolicy WorldTickQueues::policy() const noexcept {
  return tick_policy_;
}

void WorldTickQueues::schedule_block_update(
    BlockPos pos,
    TickIndex current_tick,
    TickIndex delay_ticks,
    BlockUpdateReason reason) {
  scheduled_block_updates_.emplace_back(pos, current_tick + delay_ticks, reason);
}

void WorldTickQueues::enqueue_block_update(BlockPos pos, BlockUpdateReason reason) {
  block_updates_.emplace_back(pos, reason);
}

void WorldTickQueues::enqueue_neighbor_block_updates(BlockPos pos) {
  for (const Vec3i offset : NeighborOffsets) {
    enqueue_block_update(arv::offset_block(pos, offset), BlockUpdateReason::NeighborChanged);
  }
}

u32 WorldTickQueues::process_due_scheduled_ticks(TickIndex current_tick) {
  std::vector<ScheduledBlockUpdate> remaining;
  remaining.reserve(scheduled_block_updates_.size());

  u32 processed = 0;
  for (const ScheduledBlockUpdate& update : scheduled_block_updates_) {
    if (update.execute_at <= current_tick) {
      block_updates_.emplace_back(update.pos, update.reason);
      ++processed;
    } else {
      remaining.emplace_back(update);
    }
  }

  scheduled_block_updates_ = std::move(remaining);
  return processed;
}

u32 WorldTickQueues::drain_block_updates(u32 budget) {
  u32 processed = 0;
  while (processed < budget && !block_updates_.empty()) {
    block_updates_.pop_front();
    ++processed;
  }
  return processed;
}

usize WorldTickQueues::pending_block_update_count() const noexcept {
  return block_updates_.size() + scheduled_block_updates_.size();
}

} // namespace arv::world::detail

namespace arv::world {

void WorldRuntime::set_tick_policy(WorldTickPolicy policy) noexcept {
  impl_->tick_queues.set_policy(policy);
}

WorldTickPolicy WorldRuntime::tick_policy() const noexcept {
  return impl_->tick_queues.policy();
}

void WorldRuntime::schedule_block_update(
    BlockPos pos,
    TickIndex delay_ticks,
    BlockUpdateReason reason) {
  impl_->tick_queues.schedule_block_update(pos, impl_->tick_index, delay_ticks, reason);
}

usize WorldRuntime::pending_block_update_count() const noexcept {
  return impl_->tick_queues.pending_block_update_count();
}

} // namespace arv::world
