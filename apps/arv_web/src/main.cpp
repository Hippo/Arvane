#include <emscripten.h>

#include "arv/core/math.hpp"
#include "arv/core/time.hpp"
#include "web_application.hpp"
#include "web_canvas_context.hpp"
#include "web_client_host.hpp"
#include "web_client_tick.hpp"
#include "web_input.hpp"
#include "web_platform.hpp"
#include "web_render_host.hpp"
#include "web_world_frame.hpp"

using namespace arv::web;

extern "C" {

EMSCRIPTEN_KEEPALIVE void arv_web_frame() {
  if (!web_canvas_context_ready() || !g_app.visible) {
    return;
  }

  const double now = emscripten_get_now() * 0.001;
  const arv::Seconds elapsed_seconds =
      static_cast<arv::Seconds>(now - g_app.start_time);
  poll_web_session();
  resize_canvas();
  submit_web_input_and_tick_runtime(now);
  const arv::Vec4f clear_color = web_clear_color(elapsed_seconds);
  if (!web_world_frame_will_clear()) {
    clear_web_render_frame(
        clear_color.x,
        clear_color.y,
        clear_color.z,
        clear_color.w);
  }
  render_remote_world(clear_color);
}

EMSCRIPTEN_KEEPALIVE void arv_web_connect(const char* url) {
  connect_web_session(url);
}

EMSCRIPTEN_KEEPALIVE void arv_web_disconnect() {
  disconnect_web_session();
}

} // extern "C"

int main() {
  g_app.start_time = emscripten_get_now() * 0.001;
  initialize_web_client_host();
  set_shell_state(WebShellState::Boot);
  set_status("starting Arvane web client");
  if (!initialize_webgl()) {
    return 1;
  }
  install_input_callbacks();
  note_runtime_ready();
  emscripten_set_main_loop(arv_web_frame, 0, true);
  return 0;
}
