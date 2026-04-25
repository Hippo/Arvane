#pragma once

#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "arv/render/render_types.hpp"

#include <algorithm>
#include <cmath>

namespace arv::client {

inline constexpr f32 DefaultCameraFovYDegrees = 70.0F;
inline constexpr f32 DefaultCameraNearPlane = 0.05F;
inline constexpr f32 DefaultCameraFarPlane = 1000.0F;
inline constexpr f32 MaxCameraPitchRadians = 1.55334306F;
inline constexpr f32 FreecamMouseSensitivityRadians = 0.0025F;
inline constexpr f32 FreecamMoveSpeedBlocksPerSecond = 8.0F;
inline constexpr f32 FreecamFastMoveSpeedBlocksPerSecond = 24.0F;

struct FreecamInput {
  bool move_forward{};
  bool move_backward{};
  bool move_left{};
  bool move_right{};
  bool move_up{};
  bool move_down{};
  bool fast{};
  f32 mouse_delta_x{};
  f32 mouse_delta_y{};
  Seconds delta_seconds{};
};

class CameraShell {
public:
  [[nodiscard]] WorldID world_id() const noexcept {
    return world_id_;
  }

  void set_world_id(WorldID world_id) noexcept {
    world_id_ = world_id;
  }

  [[nodiscard]] const Vec3f& position() const noexcept {
    return position_;
  }

  void set_position(Vec3f position) noexcept {
    position_ = position;
  }

  [[nodiscard]] f32 yaw_radians() const noexcept {
    return yaw_radians_;
  }

  [[nodiscard]] f32 pitch_radians() const noexcept {
    return pitch_radians_;
  }

  void set_rotation(f32 yaw_radians, f32 pitch_radians) noexcept {
    yaw_radians_ = yaw_radians;
    pitch_radians_ = std::clamp(pitch_radians, -MaxCameraPitchRadians, MaxCameraPitchRadians);
  }

  void apply_mouse_delta(f32 yaw_delta_radians, f32 pitch_delta_radians) noexcept {
    set_rotation(yaw_radians_ + yaw_delta_radians, pitch_radians_ + pitch_delta_radians);
  }

  void apply_freecam_input(FreecamInput input) noexcept {
    apply_mouse_delta(
        input.mouse_delta_x * FreecamMouseSensitivityRadians,
        -input.mouse_delta_y * FreecamMouseSensitivityRadians);

    Vec3f movement{};
    const Vec3f forward = forward_vector();
    const Vec3f right = right_vector();

    if (input.move_forward) {
      movement = movement + forward;
    }
    if (input.move_backward) {
      movement = movement - forward;
    }
    if (input.move_right) {
      movement = movement + right;
    }
    if (input.move_left) {
      movement = movement - right;
    }
    if (input.move_up) {
      movement.y += 1.0F;
    }
    if (input.move_down) {
      movement.y -= 1.0F;
    }

    movement = normalize_or_zero(movement);
    const f32 speed = input.fast ? FreecamFastMoveSpeedBlocksPerSecond : FreecamMoveSpeedBlocksPerSecond;
    position_ = position_ + movement * (speed * input.delta_seconds);
  }

  [[nodiscard]] Vec3f forward_vector() const noexcept {
    const f32 cos_pitch = std::cos(pitch_radians_);
    return normalize_or_zero({
        std::sin(yaw_radians_) * cos_pitch,
        std::sin(pitch_radians_),
        std::cos(yaw_radians_) * cos_pitch});
  }

  [[nodiscard]] Vec3f right_vector() const noexcept {
    return normalize_or_zero({std::cos(yaw_radians_), 0.0F, -std::sin(yaw_radians_)});
  }

  [[nodiscard]] Vec3f up_vector() const noexcept {
    return normalize_or_zero(cross(forward_vector(), right_vector()));
  }

  [[nodiscard]] render::CameraSnapshot snapshot(
      u32 viewport_width,
      u32 viewport_height,
      f32 partial_tick_alpha) const noexcept {
    render::CameraSnapshot camera;
    camera.world_id = world_id_;
    camera.position = position_;
    camera.rotation = orientation();
    camera.forward = forward_vector();
    camera.right = right_vector();
    camera.up = up_vector();
    camera.view = identity_mat4f();
    camera.projection = identity_mat4f();
    camera.view_projection = identity_mat4f();
    camera.near_plane = DefaultCameraNearPlane;
    camera.far_plane = DefaultCameraFarPlane;
    camera.fov_y_degrees = DefaultCameraFovYDegrees;
    camera.viewport_width = viewport_width;
    camera.viewport_height = viewport_height;
    camera.partial_tick_alpha = partial_tick_alpha;
    return camera;
  }

  [[nodiscard]] render::RenderView render_view() const noexcept {
    return render::render_view_from_camera(snapshot(0, 0, 0.0F));
  }

private:
  [[nodiscard]] Quatf orientation() const noexcept {
    const f32 half_yaw = yaw_radians_ * 0.5F;
    const f32 half_pitch = pitch_radians_ * 0.5F;
    const f32 cy = std::cos(half_yaw);
    const f32 sy = std::sin(half_yaw);
    const f32 cp = std::cos(half_pitch);
    const f32 sp = std::sin(half_pitch);

    Quatf rotation;
    rotation.x = sp * cy;
    rotation.y = cp * sy;
    rotation.z = -sp * sy;
    rotation.w = cp * cy;
    return rotation;
  }

  WorldID world_id_{};
  Vec3f position_{8.0F, 14.0F, -20.0F};
  f32 yaw_radians_{};
  f32 pitch_radians_{-0.28F};
};

} // namespace arv::client
