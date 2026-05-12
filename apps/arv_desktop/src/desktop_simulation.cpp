#include "desktop_frame_loop.hpp"

#include "chunk_view.hpp"

#include "arv/core/log.hpp"
#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"
#include "arv/net/connection_state.hpp"
#include "arv/world/world_queries.hpp"

#include <chrono>

namespace arv::desktop {
namespace {

inline constexpr u32 MaxSimulationTicksPerFrame = 5;

} // namespace

bool DesktopFrameLoop::update_simulation_and_chunks(
    DesktopGameSession& active_session,
    std::chrono::duration<Seconds> delta,
    bool& fixed_tick_ran) {
  active_session.ticks.add_time(delta.count());
  const bool has_active_integrated_session = active_session.integrated_server != nullptr;
  const bool singleplayer_paused =
      has_active_integrated_session && gameplay_screen_manager_.pauses_singleplayer();
  u32 simulation_ticks_this_frame = 0;
  while (active_session.ticks.consume_tick()) {
    ARV_PROFILE_ZONE("Simulation tick");
    if (!singleplayer_paused) {
      std::chrono::steady_clock::time_point simulation_start{};
      if constexpr (core::profiling_enabled()) {
        simulation_start = std::chrono::steady_clock::now();
      }
      if (has_active_integrated_session) {
        active_session.integrated_server->tick();
        world::WorldCollisionQuery local_collision_query(
            active_session.client_world,
            assets_.block_registry,
            true);
        active_session.client->fixed_tick(
            active_session.integrated_server->tick_index(),
            &local_collision_query);
      } else {
        const bool was_in_game =
            active_session.client->connection_state() == net::ConnectionState::InGame;
        if (was_in_game) {
          if (active_session.client_tick_index == 0U) {
            active_session.client_tick_index = active_session.client->world_initial_tick();
          }
          ++active_session.client_tick_index;
        }
        world::WorldCollisionQuery local_collision_query(
            active_session.client_world,
            assets_.block_registry,
            true);
        active_session.client->fixed_tick(
            active_session.client_tick_index,
            &local_collision_query);
        if (!was_in_game &&
            active_session.client->connection_state() == net::ConnectionState::InGame) {
          active_session.client_tick_index = active_session.client->world_initial_tick();
        }
      }
      if (active_session.client->connection_state() == net::ConnectionState::InGame) {
        update_block_outline_target(active_session);
        sessions_.collect_client_chunk_stream(active_session);
      }
      if constexpr (core::profiling_enabled()) {
        const auto simulation_end = std::chrono::steady_clock::now();
        core::record_thread_timing(
            core::PerfMetric::SimulationTick,
            std::chrono::duration<double, std::milli>(
                simulation_end - simulation_start)
                .count(),
            "main/world");
      }
      fixed_tick_ran = true;
    }
    ++simulation_ticks_this_frame;
    if (simulation_ticks_this_frame >= MaxSimulationTicksPerFrame &&
        active_session.ticks.accumulated_seconds() >=
            active_session.ticks.tick_duration()) {
      core::warn(
          "desktop",
          "dropped accumulated simulation time after {} catch-up ticks",
          MaxSimulationTicksPerFrame);
      active_session.ticks.clear();
      break;
    }
  }

  if (active_session.client->connection_state() != net::ConnectionState::InGame) {
    return true;
  }

  active_session.client->advance_entity_interpolation(delta.count());

  sessions_.apply_pending_client_chunk_stream(
      active_session,
      config_.chunk_pipeline.max_client_chunk_applies_per_frame,
      config_.chunk_pipeline.max_client_chunk_unloads_per_frame);

  return update_chunk_view(
      active_session.client_world,
      renderer_,
      world_jobs_,
      active_session.chunk_view,
      config_.chunks,
      config_.chunk_pipeline,
      camera_chunk_section_coord(active_session.client->camera()),
      active_session.min_chunk_y,
      active_session.max_chunk_y,
      assets_.block_registry,
      assets_.block_atlas.render_data,
      &assets_.fluid_render_data);
}

} // namespace arv::desktop
