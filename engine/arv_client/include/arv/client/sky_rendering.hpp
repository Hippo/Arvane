#pragma once

#include "arv/io/config.hpp"
#include "arv/render/render_types.hpp"
#include "arv/world/world_time.hpp"

namespace arv::client {

[[nodiscard]] render::SkyUniforms sky_uniforms_from_clock(
    const world::WorldClock& clock,
    const io::RenderConfig::SkyConfig& sky_config) noexcept;

void configure_frame_sky(
    render::FramePacket& frame,
    const world::WorldClock& clock,
    const io::RenderConfig::SkyConfig& sky_config) noexcept;

void configure_frame_space_sky(render::FramePacket& frame) noexcept;

} // namespace arv::client
