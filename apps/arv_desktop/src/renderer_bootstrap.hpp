#pragma once

#include "arv/asset/resource.hpp"
#include "arv/core/log.hpp"
#include "arv/io/config.hpp"
#include "arv/render/opengl_renderer.hpp"

namespace arv::desktop {

[[nodiscard]] bool configure_renderer(
    render::OpenGLRenderer& renderer,
    asset::ResourceManager& resources,
    const io::EngineConfig& config,
    core::Logger& logger);

} // namespace arv::desktop
