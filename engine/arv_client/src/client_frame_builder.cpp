#include "client_frame_builder.hpp"

#include <algorithm>

namespace arv::client {

render::FramePacket build_client_frame_packet(
    const CameraShell& camera,
    f32 interpolation_alpha,
    u32 viewport_width,
    u32 viewport_height) {
  render::FramePacket packet;
  packet.camera = camera.snapshot(viewport_width, viewport_height, interpolation_alpha);
  const f32 aspect =
      static_cast<f32>(std::max<u32>(viewport_width, 1U)) /
      static_cast<f32>(std::max<u32>(viewport_height, 1U));
  const Vec3f eye = camera.position();
  packet.camera.view =
      look_at_mat4f(eye, eye + camera.forward_vector(), {0.0F, 1.0F, 0.0F});
  packet.camera.projection = perspective_mat4f(
      degrees_to_radians(camera.fov_y_degrees()),
      aspect,
      DefaultCameraNearPlane,
      DefaultCameraFarPlane);
  packet.camera.view_projection =
      multiply_mat4f(packet.camera.projection, packet.camera.view);
  packet.world_view = render::render_view_from_camera(packet.camera);
  return packet;
}

} // namespace arv::client
