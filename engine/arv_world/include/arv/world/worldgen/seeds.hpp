#pragma once

#include "arv/core/coordinates.hpp"
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

[[nodiscard]] constexpr u64 structure_block_seed(
    u64 seed,
    u64 salt,
    BlockPos origin,
    BlockPos local,
    u64 resource_hash = 0U) noexcept {
  u64 value = seed ^ mix64(salt) ^ mix64(resource_hash);
  value ^= mix64(static_cast<u64>(static_cast<i64>(origin.value.x)) + 0x632BE59BD9B4E019ULL);
  value ^= mix64(static_cast<u64>(static_cast<i64>(origin.value.y)) + 0x9E3779B97F4A7C15ULL);
  value ^= mix64(static_cast<u64>(static_cast<i64>(origin.value.z)) + 0x85157AF5D8E536A5ULL);
  value ^= mix64(static_cast<u64>(static_cast<i64>(local.value.x)) + 0xDB4F0B9175AE2165ULL);
  value ^= mix64(static_cast<u64>(static_cast<i64>(local.value.y)) + 0xBBE0563303A4615FULL);
  value ^= mix64(static_cast<u64>(static_cast<i64>(local.value.z)) + 0xA0F2EC75A1FE1575ULL);
  return mix64(value);
}

[[nodiscard]] constexpr f32 hash_to_unit_float(u64 hash) noexcept {
  return static_cast<f32>((hash >> 40U) & 0xFFFFFFU) / 16777215.0F;
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
