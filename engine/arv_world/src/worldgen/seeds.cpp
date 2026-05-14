#include "arv/world/worldgen/seeds.hpp"

#include <algorithm>

namespace arv::world::worldgen {

u64 DeterministicRng::next_u64() noexcept {
  state_ = mix64(state_ + 0x9E3779B97F4A7C15ULL);
  return state_;
}

u32 DeterministicRng::next_u32() noexcept {
  return static_cast<u32>(next_u64() >> 32U);
}

f32 DeterministicRng::next_f32() noexcept {
  return static_cast<f32>(next_u32()) / static_cast<f32>(0xFFFFFFFFU);
}

bool DeterministicRng::chance(f32 probability) noexcept {
  return next_f32() < std::clamp(probability, 0.0F, 1.0F);
}

i32 DeterministicRng::range_i32(i32 min, i32 max) noexcept {
  if (max <= min) {
    return min;
  }
  const u32 span = static_cast<u32>(max - min + 1);
  return min + static_cast<i32>(next_u32() % span);
}

} // namespace arv::world::worldgen
