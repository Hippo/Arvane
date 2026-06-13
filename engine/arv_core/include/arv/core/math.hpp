#pragma once

#include "arv/core/types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

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

inline constexpr f32 PiRadians = std::numbers::pi_v<f32>;
inline constexpr f32 TauRadians = PiRadians * 2.0F;
inline constexpr f32 ModelForwardYawOffsetRadians = PiRadians;

struct MovementBasis {
  Vec3f forward{};
  Vec3f right{};
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

[[nodiscard]] inline i32 floor_to_i32(f32 value) noexcept {
  return static_cast<i32>(std::floor(value));
}

[[nodiscard]] constexpr bool intervals_overlap_open(
    f32 min_a,
    f32 max_a,
    f32 min_b,
    f32 max_b) noexcept {
  return min_a < max_b && max_a > min_b;
}

[[nodiscard]] constexpr f32 distance_squared_point_to_aabb(
    Vec3f point,
    Aabb3f bounds) noexcept {
  const auto axis_distance = [](f32 value, f32 min, f32 max) constexpr noexcept {
    if (value < min) {
      return min - value;
    }
    if (value > max) {
      return value - max;
    }
    return 0.0F;
  };
  const f32 dx = axis_distance(point.x, bounds.min.x, bounds.max.x);
  const f32 dy = axis_distance(point.y, bounds.min.y, bounds.max.y);
  const f32 dz = axis_distance(point.z, bounds.min.z, bounds.max.z);
  return dx * dx + dy * dy + dz * dz;
}

[[nodiscard]] constexpr f32 horizontal_length_squared(Vec3f value) noexcept {
  return value.x * value.x + value.z * value.z;
}

[[nodiscard]] inline f32 horizontal_length(Vec3f value) noexcept {
  return std::sqrt(horizontal_length_squared(value));
}

[[nodiscard]] constexpr f32 horizontal_distance_squared(Vec3f from, Vec3f to) noexcept {
  const f32 dx = from.x - to.x;
  const f32 dz = from.z - to.z;
  return dx * dx + dz * dz;
}

[[nodiscard]] inline f32 horizontal_distance(Vec3f from, Vec3f to) noexcept {
  return std::sqrt(horizontal_distance_squared(from, to));
}

[[nodiscard]] constexpr i32 abs_i32(i32 value) noexcept {
  return value < 0
             ? (value == std::numeric_limits<i32>::min()
                    ? std::numeric_limits<i32>::max()
                    : -value)
             : value;
}

[[nodiscard]] constexpr i32 max_abs3(i32 x, i32 y, i32 z) noexcept {
  const i32 xy = std::max(abs_i32(x), abs_i32(y));
  return std::max(xy, abs_i32(z));
}

[[nodiscard]] constexpr int nonzero_axis_count(Vec3i value) noexcept {
  return static_cast<int>(value.x != 0) +
         static_cast<int>(value.y != 0) +
         static_cast<int>(value.z != 0);
}

[[nodiscard]] inline bool finite(Vec3f value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] constexpr Aabb3f translated(Aabb3f bounds, Vec3f delta) noexcept {
  return {
      {bounds.min.x + delta.x, bounds.min.y + delta.y, bounds.min.z + delta.z},
      {bounds.max.x + delta.x, bounds.max.y + delta.y, bounds.max.z + delta.z}};
}

[[nodiscard]] constexpr Aabb3f swept(Aabb3f bounds, Vec3f delta) noexcept {
  return {
      {std::min(bounds.min.x, bounds.min.x + delta.x),
       std::min(bounds.min.y, bounds.min.y + delta.y),
       std::min(bounds.min.z, bounds.min.z + delta.z)},
      {std::max(bounds.max.x, bounds.max.x + delta.x),
       std::max(bounds.max.y, bounds.max.y + delta.y),
       std::max(bounds.max.z, bounds.max.z + delta.z)}};
}

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

[[nodiscard]] constexpr f32 distance_squared(Vec3f from, Vec3f to) noexcept {
  const Vec3f delta = from - to;
  return dot(delta, delta);
}

[[nodiscard]] inline f32 distance(Vec3f from, Vec3f to) noexcept {
  return std::sqrt(distance_squared(from, to));
}

[[nodiscard]] constexpr Vec3f lerp(Vec3f from, Vec3f to, f32 alpha) noexcept {
  return {
      from.x + (to.x - from.x) * alpha,
      from.y + (to.y - from.y) * alpha,
      from.z + (to.z - from.z) * alpha};
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

[[nodiscard]] inline f32 degrees_to_radians(f32 degrees) noexcept {
  return degrees * PiRadians / 180.0F;
}

[[nodiscard]] inline f32 radians_to_degrees(f32 radians) noexcept {
  return radians * 180.0F / PiRadians;
}

[[nodiscard]] inline f32 wrap_radians(f32 radians) noexcept {
  f32 wrapped = std::fmod(radians + PiRadians, TauRadians);
  if (wrapped < 0.0F) {
    wrapped += TauRadians;
  }
  return wrapped - PiRadians;
}

[[nodiscard]] inline Vec3f horizontal_forward(f32 yaw_radians) noexcept {
  return {std::sin(yaw_radians), 0.0F, std::cos(yaw_radians)};
}

[[nodiscard]] inline Vec3f horizontal_right(f32 yaw_radians) noexcept {
  return {std::cos(yaw_radians), 0.0F, -std::sin(yaw_radians)};
}

[[nodiscard]] inline MovementBasis horizontal_movement_basis(f32 yaw_radians) noexcept {
  const f32 sin_yaw = std::sin(yaw_radians);
  const f32 cos_yaw = std::cos(yaw_radians);
  return {
      {sin_yaw, 0.0F, cos_yaw},
      {cos_yaw, 0.0F, -sin_yaw}};
}

[[nodiscard]] inline Vec3f view_forward(f32 yaw_radians, f32 pitch_radians) noexcept {
  const f32 sin_yaw = std::sin(yaw_radians);
  const f32 cos_yaw = std::cos(yaw_radians);
  const f32 sin_pitch = std::sin(pitch_radians);
  const f32 cos_pitch = std::cos(pitch_radians);
  return {sin_yaw * cos_pitch, sin_pitch, cos_yaw * cos_pitch};
}

[[nodiscard]] inline Vec3f look_direction(f32 yaw_radians, f32 pitch_radians) noexcept {
  return view_forward(yaw_radians, pitch_radians);
}

[[nodiscard]] inline Quatf multiply_quat(Quatf lhs, Quatf rhs) noexcept {
  return {
      lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
      lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
      lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
      lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z};
}

[[nodiscard]] inline Quatf normalize_quat(Quatf value) noexcept {
  const f32 len =
      std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
  if (len <= 0.0F) {
    return {};
  }
  return {value.x / len, value.y / len, value.z / len, value.w / len};
}

[[nodiscard]] inline Quatf conjugate_quat(Quatf value) noexcept {
  return {-value.x, -value.y, -value.z, value.w};
}

[[nodiscard]] inline Vec3f rotate_vector(Quatf rotation, Vec3f value) noexcept {
  const Quatf vector_quat{value.x, value.y, value.z, 0.0F};
  const Quatf rotated =
      multiply_quat(multiply_quat(rotation, vector_quat), conjugate_quat(rotation));
  return {rotated.x, rotated.y, rotated.z};
}

[[nodiscard]] inline Quatf yaw_rotation(f32 radians) noexcept {
  const f32 half = radians * 0.5F;
  return {0.0F, std::sin(half), 0.0F, std::cos(half)};
}

[[nodiscard]] inline Quatf pitch_rotation(f32 radians) noexcept {
  const f32 half = radians * 0.5F;
  return {std::sin(half), 0.0F, 0.0F, std::cos(half)};
}

[[nodiscard]] inline Quatf roll_rotation(f32 radians) noexcept {
  const f32 half = radians * 0.5F;
  return {0.0F, 0.0F, std::sin(half), std::cos(half)};
}

[[nodiscard]] inline Quatf euler_degrees_rotation(Vec3f degrees) noexcept {
  return multiply_quat(
      pitch_rotation(degrees_to_radians(degrees.x)),
      multiply_quat(
          yaw_rotation(degrees_to_radians(degrees.y)),
          roll_rotation(degrees_to_radians(degrees.z))));
}

[[nodiscard]] inline Quatf yaw_pitch_rotation(
    f32 yaw_radians,
    f32 pitch_radians) noexcept {
  const f32 half_yaw = yaw_radians * 0.5F;
  const f32 half_pitch = pitch_radians * 0.5F;
  const f32 cy = std::cos(half_yaw);
  const f32 sy = std::sin(half_yaw);
  const f32 cp = std::cos(half_pitch);
  const f32 sp = std::sin(half_pitch);
  return {sp * cy, cp * sy, -sp * sy, cp * cy};
}

[[nodiscard]] inline f32 model_yaw_from_horizontal_direction(Vec3f direction) noexcept {
  return std::atan2(direction.x, direction.z) + ModelForwardYawOffsetRadians;
}

[[nodiscard]] inline Quatf model_forward_yaw_rotation(f32 body_yaw_radians) noexcept {
  return yaw_rotation(body_yaw_radians + ModelForwardYawOffsetRadians);
}

[[nodiscard]] constexpr Mat4f identity_mat4f() noexcept {
  return {};
}

[[nodiscard]] constexpr Mat4f multiply_mat4f(Mat4f lhs, Mat4f rhs) noexcept {
  Mat4f result{{}};
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      for (int k = 0; k < 4; ++k) {
        result.values[static_cast<usize>(column * 4 + row)] +=
            lhs.values[static_cast<usize>(k * 4 + row)] *
            rhs.values[static_cast<usize>(column * 4 + k)];
      }
    }
  }
  return result;
}

[[nodiscard]] inline Mat4f perspective_mat4f(
    f32 fov_y_radians,
    f32 aspect,
    f32 near_z,
    f32 far_z) noexcept {
  const f32 f = 1.0F / std::tan(fov_y_radians * 0.5F);
  Mat4f result{{}};
  result.values[0] = f / std::max(aspect, 0.001F);
  result.values[5] = f;
  result.values[10] = (far_z + near_z) / (near_z - far_z);
  result.values[11] = -1.0F;
  result.values[14] = (2.0F * far_z * near_z) / (near_z - far_z);
  return result;
}

[[nodiscard]] inline Mat4f look_at_mat4f(Vec3f eye, Vec3f center, Vec3f up) noexcept {
  const Vec3f forward = normalize_or_zero(center - eye);
  const Vec3f side = normalize_or_zero(cross(up, forward));
  const Vec3f camera_up = cross(forward, side);
  return {{
      side.x, camera_up.x, -forward.x, 0.0F,
      side.y, camera_up.y, -forward.y, 0.0F,
      side.z, camera_up.z, -forward.z, 0.0F,
      -dot(side, eye), -dot(camera_up, eye), dot(forward, eye), 1.0F}};
}

[[nodiscard]] constexpr Vec3f transform_point(Mat4f matrix, Vec3f point) noexcept {
  return {
      matrix.values[0] * point.x + matrix.values[4] * point.y +
          matrix.values[8] * point.z + matrix.values[12],
      matrix.values[1] * point.x + matrix.values[5] * point.y +
          matrix.values[9] * point.z + matrix.values[13],
      matrix.values[2] * point.x + matrix.values[6] * point.y +
          matrix.values[10] * point.z + matrix.values[14]};
}

} // namespace arv
