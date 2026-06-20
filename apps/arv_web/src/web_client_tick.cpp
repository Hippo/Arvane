#include "web_client_tick.hpp"

#include "arv/client/chunk_collision_query.hpp"
#include "arv/client/client_input_mapping.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/world/world_time.hpp"
#include "web_application.hpp"
#include "web_client_host.hpp"
#include "web_gameplay_interactions.hpp"
#include "web_platform.hpp"
#include "web_ui_host.hpp"

#include <algorithm>
#include <optional>
#include <utility>

namespace arv::web {
namespace {

[[nodiscard]] arv::client::InputFrame build_browser_input_frame() {
  return arv::client::input_frame_from_platform_snapshot(g_app.input.snapshot);
}

void clear_per_frame_input_edges() {
  arv::platform::InputSnapshot& input = g_app.input.snapshot;
  arv::client::clear_platform_input_frame_edges(input);
  g_app.input.primary_animation_pressed = false;
  g_app.input.secondary_animation_pressed = false;
  input.drop_item_held = false;
}

} // namespace

void submit_web_input_and_tick_runtime(double now) {
  if (g_app.session == nullptr) {
    return;
  }
  const bool wanted_cursor_before = ui_wants_cursor();
  const double dt_seconds = std::clamp(
      now - g_app.input.last_frame_time,
      0.0,
      0.1);
  g_app.input.last_frame_time = now;

  route_web_ui_input();
  if (g_app.session == nullptr) {
    clear_per_frame_input_edges();
    return;
  }
  arv::client::InputFrame input = build_browser_input_frame();
  if (g_app.ui.screen_manager.blocks_gameplay_input()) {
    input = {};
  }
  const arv::Seconds gameplay_time_seconds =
      static_cast<arv::Seconds>(now - g_app.start_time);
  send_web_entity_interactions_from_input(input, gameplay_time_seconds);
  arv::client::InputFrame animation_input = input;
  animation_input.primary_pressed = g_app.input.primary_animation_pressed;
  animation_input.secondary_pressed = g_app.input.secondary_animation_pressed;
  g_app.session->submit_input(std::move(input));
  const bool wants_cursor_after = ui_wants_cursor();
  if (wants_cursor_after) {
    release_pointer_lock();
  } else if (wanted_cursor_before && !g_app.pointer_locked) {
    request_pointer_lock();
  }
  update_web_held_item_animation_from_input(
      animation_input,
      gameplay_time_seconds);
  update_web_local_particles_from_input(
      animation_input,
      gameplay_time_seconds,
      static_cast<arv::Seconds>(dt_seconds));
  clear_per_frame_input_edges();

  g_app.session->advance_entity_interpolation(static_cast<arv::Seconds>(dt_seconds));
  g_app.session->advance_particles(static_cast<arv::Seconds>(dt_seconds));

  const arv::TickRate tick_rate =
      g_app.session->server_tick_rate() == 0U
          ? arv::DefaultBaseTps
          : g_app.session->server_tick_rate();
  const arv::Seconds tick_seconds = arv::tick_duration_seconds(tick_rate);
  g_app.input.movement_accumulator_seconds = std::min(
      g_app.input.movement_accumulator_seconds + dt_seconds,
      tick_seconds * 5.0);

  int fixed_steps = 0;
  while (g_app.input.movement_accumulator_seconds + 1.0e-9 >= tick_seconds &&
         fixed_steps < 5) {
    ++fixed_steps;
    g_app.input.movement_accumulator_seconds -= tick_seconds;

    std::optional<arv::client::ChunkSectionCollisionQuery> collision_query;
    if (g_app.block_assets.ready) {
      collision_query.emplace(
          g_app.chunk_stream.sections(),
          g_app.block_assets.loaded.block_registry);
    }
    const arv::client::RemoteClientFixedTickResult tick_result =
        arv::client::advance_remote_client_fixed_tick(
            *g_app.session,
            g_app.input.client_tick,
            collision_query.has_value() ? &collision_query.value() : nullptr);
    if (tick_result.was_in_game) {
      arv::world::tick_world_clock(g_app.world_clock);
    }
    collect_web_runtime_chunk_stream();
  }
}

} // namespace arv::web
