#pragma once

#include "arv/core/time.hpp"
#include "arv/render/render_types.hpp"

namespace arv::web {

void append_web_entity_draw_items(
    render::FramePacket& frame,
    Seconds animation_time_seconds);
void sync_web_entity_fallback_mesh();

} // namespace arv::web
