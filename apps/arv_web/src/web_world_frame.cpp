#include "web_world_frame.hpp"

#include <emscripten.h>

#include "arv/client/client_runtime.hpp"
#include "arv/client/sky_rendering.hpp"
#include "arv/net/transport.hpp"
#include "web_application.hpp"
#include "web_chunk_renderer.hpp"
#include "web_entity_frame.hpp"
#include "web_gameplay_interactions.hpp"
#include "web_gameplay_ui_frame.hpp"
#include "web_player_skins.hpp"
#include "web_render_host.hpp"
#include "web_ui_host.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace arv::web {

[[nodiscard]] Vec4f web_clear_color(Seconds elapsed_seconds) noexcept {
  const float pulse =
      static_cast<float>(0.5 + 0.5 * std::sin(elapsed_seconds * 0.85));
  const float base = g_app.connected ? 0.16F : (g_app.connecting ? 0.12F : 0.09F);
  return {
      base + pulse * 0.04F,
      0.12F + pulse * 0.03F,
      0.14F + pulse * 0.02F,
      1.0F};
}

[[nodiscard]] bool web_world_frame_will_clear() noexcept {
  return web_render_host_ready() &&
         g_app.session != nullptr &&
         g_app.session->connection_state() == net::ConnectionState::InGame;
}

void render_remote_world(Vec4f clear_color) {
  if (!web_render_host_ready() ||
      g_app.session == nullptr ||
      g_app.session->connection_state() != net::ConnectionState::InGame) {
    return;
  }
  sync_web_chunk_meshes();
  sync_web_entity_fallback_mesh();

  const double now = emscripten_get_now();
  const Seconds animation_time_seconds =
      static_cast<Seconds>(now * 0.001 - g_app.start_time);

  const u32 viewport_width = static_cast<u32>(std::max(g_app.width, 1));
  const u32 viewport_height = static_cast<u32>(std::max(g_app.height, 1));
  render::FramePacket frame =
      g_app.session->build_frame_packet(0.0F, viewport_width, viewport_height);
  client::configure_frame_sky(frame, g_app.world_clock, web_sky_config());

  append_web_chunk_draws_to_frame(frame);
  if (g_app.entity_assets.ready) {
    refresh_web_player_skin_render_assets();
    append_web_entity_draw_items(frame, animation_time_seconds);
  }

  frame.clear_color = clear_color;
  append_web_gameplay_ui_to_frame(
      frame,
      animation_time_seconds,
      web_ride_prompt_model());

  if (frame.opaque_chunks.empty() &&
      frame.transparent_chunks.empty() &&
      frame.entities.empty() &&
      frame.ui_rects.empty() &&
      frame.ui_textured_quads.empty() &&
      frame.ui_glyphs.empty()) {
    submit_and_render_web_frame(std::move(frame));
    return;
  }

  submit_and_render_web_frame(std::move(frame));
}

} // namespace arv::web
