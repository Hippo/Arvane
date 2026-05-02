#pragma once

#include "arv/client/camera_shell.hpp"
#include "arv/render/render_types.hpp"

namespace arv::client {

[[nodiscard]] render::FramePacket build_client_frame_packet(
    const CameraShell& camera,
    f32 interpolation_alpha,
    u32 viewport_width,
    u32 viewport_height);

} // namespace arv::client
