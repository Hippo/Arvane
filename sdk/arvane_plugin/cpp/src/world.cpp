#include "arv/world.hpp"

namespace arv {

World::World(const ArvAPI* api) noexcept : api_(api) {
}

ValueResult<std::uint32_t> World::get_block(
    ArvWorldHandle world,
    ArvBlockPos pos) const {
  std::uint32_t block{};
  if (api_ == nullptr || api_->worlds == nullptr || api_->worlds->get_block == nullptr) {
    return {Result::unsupported("world API is unavailable"), block};
  }
  return {
      Result{api_->worlds->get_block(api_->worlds_ctx, world, pos, &block)},
      block,
  };
}

Result World::set_block(
    ArvWorldHandle world,
    ArvBlockPos pos,
    std::uint32_t block_variant) const {
  if (api_ == nullptr || api_->worlds == nullptr || api_->worlds->set_block == nullptr) {
    return Result::unsupported("world API is unavailable");
  }
  return Result{api_->worlds->set_block(
      api_->worlds_ctx,
      world,
      pos,
      block_variant)};
}

ValueResult<ArvTickIndex> World::current_tick(ArvWorldHandle world) const {
  ArvTickIndex tick{};
  if (api_ == nullptr || api_->worlds == nullptr ||
      api_->worlds->current_tick == nullptr) {
    return {Result::unsupported("world API is unavailable"), tick};
  }
  return {
      Result{api_->worlds->current_tick(api_->worlds_ctx, world, &tick)},
      tick,
  };
}

ValueResult<std::uint64_t> World::seed(ArvWorldHandle world) const {
  std::uint64_t seed_value{};
  if (api_ == nullptr || api_->worlds == nullptr || api_->worlds->world_seed == nullptr) {
    return {Result::unsupported("world API is unavailable"), seed_value};
  }
  return {
      Result{api_->worlds->world_seed(api_->worlds_ctx, world, &seed_value)},
      seed_value,
  };
}

} // namespace arv
