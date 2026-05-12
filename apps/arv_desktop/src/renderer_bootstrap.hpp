#pragma once

#include "arv/asset/resource.hpp"
#include "arv/core/log.hpp"
#include "arv/io/config.hpp"
#include "arv/render/render_commands.hpp"

namespace arv::desktop {

[[nodiscard]] bool configure_renderer(
    render::RenderCommandQueue& renderer,
    asset::ResourceManager& resources,
    const io::EngineConfig& config);

} // namespace arv::desktop
