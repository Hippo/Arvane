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
  [[nodiscard]] WorldID world_id() const noexcept;
  void set_world_id(WorldID world_id) noexcept;
  [[nodiscard]] const Vec3f& position() const noexcept;
  void set_position(Vec3f position) noexcept;
  [[nodiscard]] f32 yaw_radians() const noexcept;
  [[nodiscard]] f32 pitch_radians() const noexcept;
  void set_rotation(f32 yaw_radians, f32 pitch_radians) noexcept;
  void apply_mouse_delta(f32 yaw_delta_radians, f32 pitch_delta_radians) noexcept;
  void apply_freecam_input(FreecamInput input) noexcept;
  [[nodiscard]] Vec3f forward_vector() const noexcept;
  [[nodiscard]] Vec3f right_vector() const noexcept;
  [[nodiscard]] Vec3f up_vector() const noexcept;
  [[nodiscard]] render::CameraSnapshot snapshot(
      u32 viewport_width,
      u32 viewport_height,
      f32 partial_tick_alpha) const noexcept;
  [[nodiscard]] render::RenderView render_view() const noexcept;

private:
  [[nodiscard]] Quatf orientation() const noexcept;

  WorldID world_id_{};
  Vec3f position_{8.0F, 14.0F, -20.0F};
  f32 yaw_radians_{};
  f32 pitch_radians_{-0.28F};
};

} // namespace arv::client
