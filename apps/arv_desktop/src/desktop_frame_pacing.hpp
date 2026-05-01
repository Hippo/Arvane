#pragma once

#include "arv/io/config.hpp"

#include <chrono>

namespace arv::desktop {

void pace_frame(
    const io::RenderConfig& render,
    std::chrono::steady_clock::time_point frame_start);

} // namespace arv::desktop
