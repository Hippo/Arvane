#pragma once

#include "arv/core/math.hpp"
#include "arv/core/time.hpp"

namespace arv::web {

[[nodiscard]] Vec4f web_clear_color(Seconds elapsed_seconds) noexcept;
[[nodiscard]] bool web_world_frame_will_clear() noexcept;
void render_remote_world(Vec4f clear_color);

} // namespace arv::web
