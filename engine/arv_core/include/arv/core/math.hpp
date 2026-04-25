#pragma once

#include "arv/core/types.hpp"

#include <array>
#include <cmath>

namespace arv {

template<typename T>
struct Vec2 {
  T x{};
  T y{};

  constexpr bool operator==(const Vec2&) const = default;
};

template<typename T>
struct Vec3 {
  T x{};
  T y{};
  T z{};

  constexpr bool operator==(const Vec3&) const = default;
};

template<typename T>
struct Vec4 {
  T x{};
  T y{};
  T z{};
  T w{};

  constexpr bool operator==(const Vec4&) const = default;
};

using Vec2i = Vec2<i32>;
using Vec3i = Vec3<i32>;
using Vec4i = Vec4<i32>;

using Vec2f = Vec2<f32>;
using Vec3f = Vec3<f32>;
using Vec4f = Vec4<f32>;
using Vec3d = Vec3<f64>;

struct Mat4f {
  std::array<f32, 16> values{
      1.0F, 0.0F, 0.0F, 0.0F,
      0.0F, 1.0F, 0.0F, 0.0F,
      0.0F, 0.0F, 1.0F, 0.0F,
      0.0F, 0.0F, 0.0F, 1.0F};

  constexpr bool operator==(const Mat4f&) const = default;
};

struct Quatf {
  f32 x{};
  f32 y{};
  f32 z{};
  f32 w{1.0F};

  constexpr bool operator==(const Quatf&) const = default;
};

struct Aabb3f {
  Vec3f min{};
  Vec3f max{};

  [[nodiscard]] constexpr bool overlaps(const Aabb3f& other) const noexcept {
    return min.x < other.max.x && max.x > other.min.x &&
           min.y < other.max.y && max.y > other.min.y &&
           min.z < other.max.z && max.z > other.min.z;
  }
};

template<typename T>
[[nodiscard]] constexpr Vec3<T> operator+(Vec3<T> lhs, Vec3<T> rhs) noexcept {
  return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

template<typename T>
[[nodiscard]] constexpr Vec3<T> operator-(Vec3<T> lhs, Vec3<T> rhs) noexcept {
  return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

template<typename T>
[[nodiscard]] constexpr Vec3<T> operator*(Vec3<T> value, T scalar) noexcept {
  return {value.x * scalar, value.y * scalar, value.z * scalar};
}

template<typename T>
[[nodiscard]] constexpr T dot(Vec3<T> lhs, Vec3<T> rhs) noexcept {
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

template<typename T>
[[nodiscard]] constexpr Vec3<T> cross(Vec3<T> lhs, Vec3<T> rhs) noexcept {
  return {
      lhs.y * rhs.z - lhs.z * rhs.y,
      lhs.z * rhs.x - lhs.x * rhs.z,
      lhs.x * rhs.y - lhs.y * rhs.x};
}

[[nodiscard]] inline f32 length(Vec3f value) noexcept {
  return std::sqrt(dot(value, value));
}

[[nodiscard]] inline Vec3f normalize_or_zero(Vec3f value) noexcept {
  const f32 len = length(value);
  if (len <= 0.0F) {
    return {};
  }
  return {value.x / len, value.y / len, value.z / len};
}

[[nodiscard]] constexpr Mat4f identity_mat4f() noexcept {
  return {};
}

} // namespace arv
