#include "web_application.hpp"

#include "web_render_host.hpp"

namespace arv::web {

WebApplication g_app;

void reset_input(bool keep_view) {
  const bool has_camera = g_app.session != nullptr;
  const float yaw = has_camera ? g_app.session->camera().yaw_radians() : 0.0F;
  const float pitch = has_camera ? g_app.session->camera().pitch_radians() : 0.0F;
  const f32 cursor_x = g_app.input.snapshot.cursor_x;
  const f32 cursor_y = g_app.input.snapshot.cursor_y;
  const bool cursor_initialized = g_app.input.cursor_initialized;
  g_app.input = {};
  g_app.input.snapshot.cursor_x = cursor_x;
  g_app.input.snapshot.cursor_y = cursor_y;
  g_app.input.cursor_initialized = cursor_initialized;
  if (keep_view && g_app.session != nullptr) {
    g_app.session->camera().set_rotation(yaw, pitch);
  }
}

bool ui_wants_cursor() {
  return (g_app.session != nullptr &&
          (g_app.session->inventory_open() || g_app.session->chat_open())) ||
         g_app.ui.screen_manager.blocks_gameplay_input();
}

} // namespace arv::web
