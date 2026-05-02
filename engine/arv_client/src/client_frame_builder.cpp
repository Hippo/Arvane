#include "client_frame_builder.hpp"

namespace arv::client {

render::FramePacket build_client_frame_packet(
    const CameraShell& camera,
    f32 interpolation_alpha,
    u32 viewport_width,
    u32 viewport_height) {
  render::FramePacket packet;
  packet.camera = camera.snapshot(viewport_width, viewport_height, interpolation_alpha);
  packet.world_view = render::render_view_from_camera(packet.camera);
  return packet;
}

} // namespace arv::client
