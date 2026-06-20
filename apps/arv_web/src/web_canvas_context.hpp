#pragma once

namespace arv::web {

[[nodiscard]] bool initialize_webgl();
[[nodiscard]] bool web_canvas_context_ready() noexcept;

} // namespace arv::web
