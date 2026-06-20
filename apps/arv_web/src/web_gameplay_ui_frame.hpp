#pragma once

#include "arv/core/time.hpp"
#include "arv/render/render_types.hpp"
#include "arv/base/ui/widgets/gameplay.hpp"

namespace arv::web {

void append_web_gameplay_ui_to_frame(
    arv::render::FramePacket& frame,
    arv::Seconds time_seconds,
    arv::ui::RidePromptModel ride_prompt);

} // namespace arv::web
