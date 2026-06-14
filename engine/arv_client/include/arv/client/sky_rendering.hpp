#pragma once

#include "arv/io/config.hpp"
#include "arv/render/render_types.hpp"
#include "arv/world/world_time.hpp"

namespace arv::client {

[[nodiscard]] render::SkyUniforms sky_uniforms_from_clock(
    const world::WorldClock& clock,
    const io::RenderConfig::SkyConfig& sky_config) noexcept;

} // namespace arv::client
