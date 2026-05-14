#pragma once

#include "arv/core/types.hpp"

#include <string_view>

namespace arv::world::worldgen {

[[nodiscard]] constexpr u64 fnv1a(std::string_view text) noexcept {
  u64 value = 14695981039346656037ULL;
  for (const char ch : text) {
    value ^= static_cast<u8>(ch);
    value *= 1099511628211ULL;
  }
  return value;
}

[[nodiscard]] constexpr u64 mix64(u64 value) noexcept {
  value += 0x9E3779B97F4A7C15ULL;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

[[nodiscard]] constexpr u64 salted_seed(u64 seed, std::string_view salt, u32 version = 1U) noexcept {
  return mix64(seed ^ mix64(fnv1a(salt)) ^ (static_cast<u64>(version) << 32U));
}

[[nodiscard]] constexpr u64 coordinate_seed(
    u64 seed,
    i32 x,
    i32 y,
    i32 z,
    u64 salt) noexcept {
  u64 value = seed ^ mix64(salt);
  value ^= mix64(static_cast<u64>(static_cast<i64>(x)) + 0x632BE59BD9B4E019ULL);
  value ^= mix64(static_cast<u64>(static_cast<i64>(y)) + 0x9E3779B97F4A7C15ULL);
  value ^= mix64(static_cast<u64>(static_cast<i64>(z)) + 0x85157AF5D8E536A5ULL);
  return mix64(value);
}

class DeterministicRng {
public:
  explicit constexpr DeterministicRng(u64 seed) noexcept : state_(seed) {}

  [[nodiscard]] u64 next_u64() noexcept;
  [[nodiscard]] u32 next_u32() noexcept;
  [[nodiscard]] f32 next_f32() noexcept;
  [[nodiscard]] bool chance(f32 probability) noexcept;
  [[nodiscard]] i32 range_i32(i32 min, i32 max) noexcept;

private:
  u64 state_{};
};

} // namespace arv::world::worldgen
