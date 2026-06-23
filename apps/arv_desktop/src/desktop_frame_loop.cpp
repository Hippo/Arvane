#include "desktop_frame_loop.hpp"

#include "debug_menu.hpp"
#include "desktop_frame_pacing.hpp"
#include "desktop_render_builders.hpp"
#include "desktop_save_helpers.hpp"
#include "ui_model.hpp"

#include "arv/client/client_input_mapping.hpp"
#include "arv/client/client_ui_routing.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/base/base_ids.hpp"
#include "arv/core/log.hpp"
#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"
#include "arv/core/time.hpp"
#include "arv/net/connection_state.hpp"
#include "arv/base/ui/screens/account.hpp"
#include "arv/base/ui/screens/main_menu.hpp"
#include "arv/base/ui/screens/pause_menu.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace arv::desktop {

bool session_world_ready(const DesktopGameSession* session) noexcept {
  return session != nullptr && session->client != nullptr &&
         session->client->connection_state() == net::ConnectionState::InGame;
}

namespace {

constexpr auto SpikeLogCooldown = std::chrono::milliseconds{500};

template <typename Count>
[[nodiscard]] Count monotonic_delta(Count current, Count previous) noexcept {
  return current >= previous ? current - previous : Count{};
}

[[nodiscard]] double per_second(u64 count, double seconds) noexcept {
  return seconds > 0.0 ? static_cast<double>(count) / seconds : 0.0;
}

[[nodiscard]] double percentile(std::vector<double> samples, double quantile) {
  if (samples.empty()) {
    return 0.0;
  }
  std::sort(samples.begin(), samples.end());
  const double clamped = std::clamp(quantile, 0.0, 1.0);
  const usize index = std::min<usize>(
      samples.size() - 1U,
      static_cast<usize>(std::ceil(clamped * static_cast<double>(samples.size()))) - 1U);
  return samples[index];
}

[[nodiscard]] std::string command_result_text(const command::CommandResult& result) {
  std::string text;
  for (const command::CommandMessage& message : result.messages) {
    if (!text.empty()) {
      text.append("; ");
    }
    text.append(message.text);
  }
  return text;
}

template <typename Size>
[[nodiscard]] Vec2f size_vec(Size size) noexcept {
  return {
      static_cast<f32>(size.width),
      static_cast<f32>(size.height)};
}

} // namespace

DesktopFrameLoop::DesktopFrameLoop(
    io::EngineConfig& config,
    asset::ResourceManager& resources,
    platform::OpenGLWindow& window,
    DesktopRenderThread& render_thread,
    render::RenderCommandQueue& renderer,
    platform::InputBindings& input_bindings,
    DebugImGuiLayer& debug_imgui,
    DebugMenuState& debug_menu,
    DesktopLoadedAssets& assets,
    sched::JobSystem& world_jobs,
    sched::ITaskExecutor& client_tasks,
    DesktopSessionManager& sessions,
    ui::UiTree& ui_tree,
    ui::FontUiTextMeasurer& ui_text_measurer,
    const ui::UiTheme& ui_theme,
    const voxel::FluidDefinition& water_fluid,
    player::PlayerTuning& player_tuning,
    DesktopMenuState& menu_state,
    ui::UIScreenManager& screen_manager,
    const voxel::BlockBehaviorRegistry& block_behaviors,
    const player::ItemBehaviorRegistry& item_behaviors,
    const world::EntityGoalRegistry& entity_goals,
    DesktopChunkStreamingValidationOptions validation_options) : config_(config),
                                                     resources_(resources),
                                                     window_(window),
                                                     render_thread_(render_thread),
                                                     renderer_(renderer),
                                                     input_bindings_(input_bindings),
                                                     debug_imgui_(debug_imgui),
                                                     debug_menu_(debug_menu),
                                                     assets_(assets),
                                                     world_jobs_(world_jobs),
                                                     client_tasks_(client_tasks),
                                                     sessions_(sessions),
                                                     ui_tree_(ui_tree),
                                                     ui_text_measurer_(ui_text_measurer),
                                                     ui_theme_(ui_theme),
                                                     water_fluid_(water_fluid),
                                                     player_tuning_(player_tuning),
                                                     menu_state_(menu_state),
                                                     screen_manager_(screen_manager),
                                                     block_behaviors_(block_behaviors),
                                                     item_behaviors_(item_behaviors),
                                                     entity_goals_(entity_goals),
                                                     framebuffer_(window.framebuffer_size()),
                                                     ui_metrics_(
                                                         ui::UiDisplayMetrics::from_window_and_framebuffer(
                                                             size_vec(window.window_size()),
                                                             size_vec(framebuffer_))),
                                                     validation_options_(
                                                         std::move(validation_options)) {
  const auto start = std::chrono::steady_clock::now();
  previous_time_ = start;
  app_start_time_ = start;
  last_fps_display_time_ = start;
  if (validation_options_.enabled) {
    begin_chunk_streaming_validation();
  }
}

int DesktopFrameLoop::run() {
  while (true) {
    ARV_PROFILE_ZONE("Frame time");
    if (window_.should_close() && loading_screen_ != DesktopLoadingScreen::ClosingGame) {
      if (!begin_closing_game()) {
        return 1;
      }
    }
    if (loading_screen_ == DesktopLoadingScreen::ClosingGame) {
      if (!render_loading_screen_frame(DesktopLoadingScreen::ClosingGame)) {
        return 1;
      }
      if (sessions_.start_pending()) {
        const std::optional<bool> start_result = sessions_.poll_start();
        if (!start_result.has_value()) {
          ARV_PROFILE_FRAME_MARK("Frame");
          continue;
        }
        if (start_result.value() &&
            !sessions_.shutdown_pending() &&
            !sessions_.begin_save_and_shutdown()) {
          return 1;
        }
      }
      sessions_.poll_shutdown();
      if (!sessions_.shutdown_pending()) {
        if (validation_state_.phase == ChunkStreamingValidationPhase::ShuttingDown) {
          return complete_chunk_streaming_validation_shutdown();
        }
        return sessions_.shutdown_failed() ? 1 : 0;
      }
      ARV_PROFILE_FRAME_MARK("Frame");
      continue;
    }
    if (loading_screen_ == DesktopLoadingScreen::LoadingWorld) {
      if (!process_loading_world_frame()) {
        return 1;
      }
      ARV_PROFILE_FRAME_MARK("Frame");
      continue;
    }

    sessions_.poll_shutdown();
    if (!sessions_.shutdown_pending() && sessions_.active_session() == nullptr &&
        !ensure_local_assets()) {
      return 1;
    }
    FrameTiming timing = begin_frame();
    platform::InputSnapshot platform_input = poll_platform_input();
    update_framebuffer_size();
    DesktopGameSession* active_session = sessions_.active_session();
    const bool has_live_session =
        active_session != nullptr && active_session->client != nullptr;
    if (gameplay_menu_stack_active_ && !has_live_session) {
      gameplay_menu_stack_active_ = false;
      gameplay_screen_manager_.clear();
      reset_main_menu_stack();
    } else if (gameplay_menu_stack_active_ && gameplay_screen_manager_.empty()) {
      reset_gameplay_menu_stack();
    }
    bool has_world_session = session_world_ready(active_session);
    handle_debug_menu_input(platform_input, active_session, has_world_session);
    begin_debug_frame(platform_input, timing);
    const bool was_inventory_open =
        has_world_session && active_session->client->inventory_open();

    ui::UIScreenManager& active_screens = active_screen_manager();
    ui::UIContext menu_context = make_menu_context(active_screens);
    handle_captured_menu_input(platform_input, menu_context, active_screens, active_session);
    handle_pause_menu_input(
        platform_input,
        menu_context,
        active_screens,
        active_session,
        has_world_session);

    client::InputFrame client_input =
        has_world_session
            ? client::input_frame_from_platform_snapshot(platform_input)
            : client::InputFrame{};
    bool inventory_changed_by_ui = route_input(
        platform_input,
        menu_context,
        active_screens,
        active_session,
        has_world_session,
        client_input);

    if (!process_menu_commands()) {
      return 1;
    }

    active_session = sessions_.active_session();
    bool has_active_session =
        active_session != nullptr && active_session->client != nullptr;
    has_world_session = session_world_ready(active_session);
    bool has_visual_gameplay_session =
        gameplay_menu_stack_active_ && has_active_session;
    bool has_active_integrated_session =
        has_world_session && active_session->integrated_server != nullptr;

    handle_chat_command_submit(active_session, has_world_session, client_input);
    if (!drive_chunk_streaming_validation(
            active_session,
            has_world_session,
            client_input,
            timing)) {
      return 1;
    }

    submit_client_input_and_update_cursor(
        active_session,
        has_world_session,
        was_inventory_open,
        std::move(client_input),
        inventory_changed_by_ui,
        timing.now);

    bool fixed_tick_ran = false;
    if (has_active_session &&
        !update_simulation_and_chunks(*active_session, timing.delta, fixed_tick_ran)) {
      return 1;
    }
    if (has_active_session) {
      handle_remote_connection_ready(active_session);
      has_world_session = session_world_ready(active_session);
      has_active_integrated_session =
          has_world_session && active_session->integrated_server != nullptr;
      has_visual_gameplay_session =
          gameplay_menu_stack_active_ && active_session != nullptr &&
          active_session->client != nullptr;
    }
    if (has_active_session && handle_session_disconnect(active_session)) {
      active_session = sessions_.active_session();
      has_active_session = active_session != nullptr && active_session->client != nullptr;
      has_world_session = session_world_ready(active_session);
      has_active_integrated_session =
          has_world_session && active_session->integrated_server != nullptr;
      has_visual_gameplay_session =
          gameplay_menu_stack_active_ && active_session != nullptr &&
          active_session->client != nullptr;
    }

    if (has_visual_gameplay_session) {
      refresh_player_skin_render_assets(*active_session);
    }
    refresh_menu_profile_render_assets();

    render::FramePacket frame;
    const usize visible_chunk_count = build_world_frame(
        frame,
        active_session,
        has_visual_gameplay_session,
        timing.now);

    render_debug_menu_if_available(
        active_session,
        has_active_integrated_session,
        timing,
        visible_chunk_count);

    build_ui(
        frame,
        platform_input,
        menu_context,
        active_screen_manager(),
        active_session,
        has_visual_gameplay_session,
        has_active_integrated_session,
        fixed_tick_ran,
        timing.now);

    if (!submit_render_and_present(
            std::move(frame),
            timing,
            has_world_session,
            active_session)) {
      return 1;
    }
    if (validation_state_.phase == ChunkStreamingValidationPhase::Flying &&
        active_session != nullptr &&
        has_world_session &&
        timing.now - validation_state_.flight_started_at >=
            std::chrono::duration<Seconds>(validation_options_.duration_seconds)) {
      finish_chunk_streaming_validation_run(*active_session, timing.now);
      if (!begin_closing_game()) {
        return 1;
      }
    }
    ARV_PROFILE_FRAME_MARK("Frame");
  }

  return 0;
}

bool DesktopFrameLoop::render_loading_screen_frame(DesktopLoadingScreen screen) {
  FrameTiming timing = begin_frame();
  platform::InputSnapshot platform_input = poll_platform_input();
  update_framebuffer_size();
  begin_debug_frame(platform_input, timing);

  render::FramePacket frame;
  append_loading_screen_ui(frame, screen, timing.now);
  return submit_render_and_present(
      std::move(frame),
      timing,
      false,
      nullptr);
}

ui::UIScreenManager& DesktopFrameLoop::active_screen_manager() noexcept {
  return gameplay_menu_stack_active_ ? gameplay_screen_manager_ : screen_manager_;
}

const ui::UIScreenManager& DesktopFrameLoop::active_screen_manager() const noexcept {
  return gameplay_menu_stack_active_ ? gameplay_screen_manager_ : screen_manager_;
}

void DesktopFrameLoop::push_main_menu_root() {
  screen_manager_.push(ui::make_main_menu_screen());
  if (menu_state_.local_profile_store().profiles.empty()) {
    screen_manager_.push(ui::make_account_create_screen());
  }
}

void DesktopFrameLoop::reset_main_menu_stack() {
  screen_manager_.clear();
  push_main_menu_root();
  invalidate_ui_tree_cache();
}

void DesktopFrameLoop::reset_gameplay_menu_stack() {
  gameplay_screen_manager_.clear();
  gameplay_screen_manager_.push(ui::make_gameplay_screen());
  invalidate_ui_tree_cache();
}

ui::UIContext DesktopFrameLoop::make_menu_context(ui::UIScreenManager& screens) {
  return {
      ui_tree_,
      ui_metrics_.logical_size,
      &menu_state_,
      &menu_state_,
      &screens,
      &ui_theme_,
      &assets_.language,
      this};
}

std::string DesktopFrameLoop::text() const {
  return window_.clipboard_text();
}

void DesktopFrameLoop::set_text(std::string text) {
  window_.set_clipboard_text(text);
}

FrameTiming DesktopFrameLoop::begin_frame() {
  frame_arena_.begin_frame();
  const auto now = std::chrono::steady_clock::now();
  const std::chrono::duration<Seconds> delta = now - previous_time_;
  previous_time_ = now;
  const double frame_delta_ms =
      std::chrono::duration<double, std::milli>(delta).count();
  ++profile_frame_index_;
  core::set_current_profile_frame_index(profile_frame_index_);
  core::record_thread_timing(
      core::PerfMetric::FrameDelta,
      frame_delta_ms,
      "main/render");

  update_fps_sample(now, delta.count());

  std::chrono::steady_clock::time_point frame_work_start{};
  if constexpr (core::profiling_enabled()) {
    frame_work_start = std::chrono::steady_clock::now();
  }
  return {now, delta, frame_delta_ms, frame_work_start};
}

void DesktopFrameLoop::update_fps_sample(
    std::chrono::steady_clock::time_point now,
    Seconds delta_seconds) {
  ++fps_sample_frames_;
  fps_sample_seconds_ += delta_seconds;
  if (displayed_fps_ == 0U ||
      now - last_fps_display_time_ >= FpsHudRefreshInterval) {
    displayed_fps_ = fps_sample_seconds_ > 0.0F
                         ? static_cast<u32>(
                               std::round(static_cast<double>(fps_sample_frames_) /
                                          static_cast<double>(fps_sample_seconds_)))
                         : 0U;
    last_fps_display_time_ = now;
    fps_sample_frames_ = 0;
    fps_sample_seconds_ = 0.0F;
  }
}

platform::InputSnapshot DesktopFrameLoop::poll_platform_input() {
  platform::OpenGLWindow::poll_events();
  return window_.poll_input();
}

void DesktopFrameLoop::handle_debug_menu_input(
    platform::InputSnapshot& platform_input,
    DesktopGameSession* active_session,
    bool has_session) {
  const bool chat_has_focus =
      has_session && active_session != nullptr && active_session->client->chat_open();
  if (debug_menu_.open && platform_input.text_cancel_pressed) {
    debug_menu_.open = false;
    platform_input.pause_menu_pressed = false;
    platform_input.text_cancel_pressed = false;
    platform_input.debug_menu_toggle_pressed = false;
    return;
  }

  if (platform_input.debug_menu_toggle_pressed && !chat_has_focus) {
    debug_menu_.open = !debug_menu_.open;
  }
  if (chat_has_focus) {
    platform_input.debug_menu_toggle_pressed = false;
  }
}

void DesktopFrameLoop::begin_debug_frame(
    const platform::InputSnapshot& platform_input,
    const FrameTiming& timing) {
  debug_imgui_.begin_frame(
      window_.native_handle(),
      platform_input,
      framebuffer_,
      timing.delta.count());
}

void DesktopFrameLoop::handle_pause_menu_input(
    const platform::InputSnapshot& platform_input,
    ui::UIContext& menu_context,
    ui::UIScreenManager& screens,
    DesktopGameSession* active_session,
    bool has_session) {
  if (debug_menu_.open || !platform_input.pause_menu_pressed) {
    return;
  }
  const bool has_live_session =
      active_session != nullptr && active_session->client != nullptr;
  const bool has_client_overlay =
      has_live_session &&
      (active_session->client->inventory_open() || active_session->client->chat_open());
  if (gameplay_menu_stack_active_) {
    const auto reset_gameplay_stack = [&]() {
      reset_gameplay_menu_stack();
    };
    const client::GameplayPauseMenuRoutingCallbacks callbacks{
        .reset_gameplay_screen_stack = reset_gameplay_stack,
    };
    (void)client::route_gameplay_pause_menu_input(
        screens,
        menu_context,
        platform_input.pause_menu_pressed,
        gameplay_menu_stack_active_,
        has_session,
        has_client_overlay,
        callbacks);
    return;
  }

  const ui::UIScreen* current_screen = screens.current();
  if (current_screen != nullptr) {
    (void)screens.handle_cancel(menu_context);
  } else if (screens.empty()) {
    screens.push(ui::make_main_menu_screen());
  }
}

void DesktopFrameLoop::handle_captured_menu_input(
    platform::InputSnapshot& platform_input,
    ui::UIContext& menu_context,
    ui::UIScreenManager& screens,
    DesktopGameSession* active_session) {
  if (!debug_menu_.open &&
      active_session != nullptr &&
      active_session->client != nullptr &&
      client::route_player_menu_captured_input(
          ui_tree_,
          *active_session->client,
          menu_context.command_sink,
          platform_input)) {
    client::clear_platform_input_captured_binding_edges(platform_input);
    return;
  }
  if (debug_menu_.open ||
      !client::route_captured_screen_input(screens, menu_context, platform_input)) {
    return;
  }
  client::clear_platform_input_captured_binding_edges(platform_input);
}

bool DesktopFrameLoop::route_input(
    const platform::InputSnapshot& platform_input,
    ui::UIContext& menu_context,
    ui::UIScreenManager& screens,
    DesktopGameSession* active_session,
    bool has_session,
    client::InputFrame& client_input) {
  const bool menu_blocks_gameplay = screens.blocks_gameplay_input();
  if (debug_menu_.open || menu_blocks_gameplay) {
    client_input = {};
  }

  bool inventory_changed_by_ui = false;
  if (!debug_menu_.open && menu_blocks_gameplay) {
    (void)client::route_blocking_screen_input(screens, menu_context, platform_input);
  } else if (!debug_menu_.open && has_session && active_session->client->inventory_open()) {
    inventory_changed_by_ui = client::route_inventory_mouse_input(
        ui_tree_,
        *active_session->client,
        platform_input,
        active_session->client->inventory_cursor_stack().has_value(),
        menu_context.command_sink,
        menu_context.data_source);
  } else if (!debug_menu_.open && has_session && active_session->client->chat_open()) {
    (void)route_chat_mouse_input(*active_session, platform_input);
  }
  return inventory_changed_by_ui;
}

void DesktopFrameLoop::invalidate_ui_tree_cache() noexcept {
  ui_tree_cache_valid_ = false;
}

void DesktopFrameLoop::submit_client_input_and_update_cursor(
    DesktopGameSession* active_session,
    bool has_active_session,
    bool was_inventory_open,
    client::InputFrame client_input,
    bool inventory_changed_by_ui,
    std::chrono::steady_clock::time_point now) {
  if (has_active_session) {
    const bool gameplay_blocked =
        debug_menu_.open || active_screen_manager().blocks_gameplay_input();
    const client::GameplayInteractionDispatchResult interactions =
        dispatch_gameplay_interactions_from_input(
            *active_session,
            client_input,
            gameplay_blocked);
    if (interactions.consume_secondary_use) {
      client_input.secondary_pressed = false;
      client_input.secondary_held = false;
      client::stop_held_item_animation_playback(
          active_session->held_item_animation,
          active_session->held_item_secondary_use_active,
          0.0);
    }
    if (interactions.requested_ride || interactions.requested_dismount) {
      client_input.mount_pressed = false;
    }
    update_held_item_animation_from_input(
        *active_session,
        client_input,
        std::chrono::duration<Seconds>(now - app_start_time_).count(),
        interactions.requested_ride);
    active_session->last_gameplay_input = client_input;
    active_session->client->submit_input(std::move(client_input));
    inventory_changed_by_ui = inventory_changed_by_ui || interactions.opened_container;
    if (std::optional<std::string> clipboard =
            active_session->client->consume_pending_clipboard_text();
        clipboard.has_value()) {
      window_.set_clipboard_text(clipboard.value());
    }
  }
  if (has_active_session && was_inventory_open && !active_session->client->inventory_open()) {
    (void)active_session->client->request_inventory_close();
    inventory_changed_by_ui = true;
  }
  if (has_active_session && inventory_changed_by_ui) {
    sessions_.refresh_inventory_slots(*active_session);
  }
  window_.set_cursor_captured(
      has_active_session &&
      !(active_session->client->inventory_open() || active_session->client->chat_open() ||
        debug_menu_.open || active_screen_manager().blocks_gameplay_input()));
}

void DesktopFrameLoop::update_framebuffer_size() {
  const auto next_framebuffer = window_.framebuffer_size();
  const ui::UiDisplayMetrics next_metrics =
      ui::UiDisplayMetrics::from_window_and_framebuffer(
          size_vec(window_.window_size()),
          size_vec(next_framebuffer));
  if (next_metrics.logical_size != ui_metrics_.logical_size) {
    invalidate_ui_tree_cache();
  }
  ui_text_measurer_.set_display_scale(next_metrics.scalar_to_framebuffer(1.0F));
  ui_metrics_ = next_metrics;
  if (next_framebuffer.width != framebuffer_.width ||
      next_framebuffer.height != framebuffer_.height) {
    framebuffer_ = next_framebuffer;
    renderer_.resize(framebuffer_.width, framebuffer_.height);
  }
}

usize DesktopFrameLoop::build_world_frame(
    render::FramePacket& frame,
    DesktopGameSession* active_session,
    bool has_active_session,
    std::chrono::steady_clock::time_point now) {
  if (!has_active_session) {
    return 0;
  }

  const f32 interpolation_alpha = active_session->ticks.interpolation_alpha();
  frame = active_session->client->build_frame_packet(
      interpolation_alpha,
      framebuffer_.width,
      framebuffer_.height);
  const usize visible_chunk_count =
      populate_chunk_draw_lists(frame, active_session->chunk_view, config_.chunks);
  populate_world_frame_draw_items(
      frame,
      *active_session,
      active_session->chunk_view,
      assets_.entity_render_assets,
      assets_.block_registry,
      assets_.entity_registry,
      player_skin_regions_,
      config_.chunks,
      config_.render,
      debug_menu_.show_chunk_boundaries,
      now,
      app_start_time_,
      interpolation_alpha);
  return visible_chunk_count;
}

void DesktopFrameLoop::render_debug_menu_if_available(
    DesktopGameSession* active_session,
    bool has_active_integrated_session,
    const FrameTiming& timing,
    usize visible_chunk_count) {
  if (!has_active_integrated_session) {
    return;
  }

  const DebugMenuResult debug_menu_result = render_debug_menu(
      debug_menu_,
      {
          profile_frame_index_,
          timing.frame_delta_ms,
          last_frame_work_ms_,
          displayed_fps_,
          visible_chunk_count,
          active_session->pending_client_chunk_data.size(),
          active_session->pending_client_chunk_unloads.size(),
      },
      *active_session->integrated_server,
      *active_session->client,
      assets_.item_registry,
      assets_.item_animation_library,
      active_session->held_item_animation,
      std::chrono::duration<Seconds>(timing.now - app_start_time_).count(),
      config_.paths.packs_path() / "arvane_base/data/arvane/item_animations.json",
      assets_.entity_registry,
      world_jobs_,
      renderer_,
      active_session->chunk_view);
  if (debug_menu_result.inventory_changed) {
    sessions_.refresh_inventory_slots(*active_session);
  }
}

bool DesktopFrameLoop::submit_render_and_present(
    render::FramePacket frame,
    const FrameTiming& timing,
    bool has_active_session,
    DesktopGameSession* active_session) {
  DebugImGuiDrawData debug_draw_data = debug_imgui_.capture_draw_data();
  record_frame_work_profile(timing, has_active_session, active_session);
  if (!render_thread_.present_frame(std::move(frame), std::move(debug_draw_data))) {
    return false;
  }
  pace_frame(config_.render, timing.now);
  return true;
}

void DesktopFrameLoop::begin_chunk_streaming_validation() {
  validation_state_ = {};
  validation_state_.phase = ChunkStreamingValidationPhase::Loading;
  validation_state_.save_name =
      unique_server_save_name(config_, "chunk-streaming-validation");
  validation_state_.frame_work_samples_ms.reserve(1024);

  core::info(
      "chunk_validation",
      "starting desktop chunk streaming validation save='{}' seconds={} flyspeed={} tracy={}",
      validation_state_.save_name,
      validation_options_.duration_seconds,
      validation_options_.fly_speed_multiplier,
      core::tracy_profiling_enabled() ? "enabled" : "disabled");

  queue_singleplayer_start({
      validation_state_.save_name,
      "default",
      0xA271C0DEULL,
      true,
      false,
      "Chunk Streaming Validation",
      client::make_ephemeral_local_identity("chunk_validator")});
}

bool DesktopFrameLoop::drive_chunk_streaming_validation(
    DesktopGameSession* active_session,
    bool has_active_session,
    client::InputFrame& client_input,
    const FrameTiming& timing) {
  if (validation_state_.phase == ChunkStreamingValidationPhase::Disabled ||
      validation_state_.phase == ChunkStreamingValidationPhase::ShuttingDown ||
      validation_state_.phase == ChunkStreamingValidationPhase::Failed) {
    return true;
  }
  if (!has_active_session ||
      active_session == nullptr ||
      active_session->client == nullptr ||
      active_session->integrated_server == nullptr) {
    return true;
  }

  if (!validation_state_.movement_commands_sent) {
    const server::ServerSession* server_session =
        active_session->integrated_server->sessions().first();
    if (server_session == nullptr) {
      core::error("chunk_validation", "cannot validate chunk streaming without a local server session");
      validation_state_.phase = ChunkStreamingValidationPhase::Failed;
      return false;
    }
    std::optional<command::CommandSender> sender =
        active_session->integrated_server->command_sender_for_session(
            server_session->session_id);
    if (!sender.has_value()) {
      core::error("chunk_validation", "cannot build command sender for validation session");
      validation_state_.phase = ChunkStreamingValidationPhase::Failed;
      return false;
    }
    sender->kind = command::CommandSourceKind::ServerConsole;
    sender->display_name = "chunk_streaming_validation";

    std::string gamemode_command{"/gamemode "};
    gamemode_command.append(base::ids::NoClipGameModeID);
    command::CommandResult mode_result =
        active_session->integrated_server->execute_command(
            sender.value(),
            gamemode_command);
    if (!mode_result) {
      core::error(
          "chunk_validation",
          "failed to enter noclip mode: {}",
          command_result_text(mode_result));
      validation_state_.phase = ChunkStreamingValidationPhase::Failed;
      return false;
    }

    const std::string flyspeed_command =
        "/flyspeed " + std::to_string(validation_options_.fly_speed_multiplier);
    command::CommandResult flyspeed_result =
        active_session->integrated_server->execute_command(
            sender.value(),
            flyspeed_command);
    if (!flyspeed_result) {
      core::error(
          "chunk_validation",
          "failed to set validation fly speed: {}",
          command_result_text(flyspeed_result));
      validation_state_.phase = ChunkStreamingValidationPhase::Failed;
      return false;
    }

    validation_state_.movement_commands_sent = true;
    validation_state_.phase = ChunkStreamingValidationPhase::Flying;
    validation_state_.flight_started_at = timing.now;
    validation_state_.start_generation_jobs =
        active_session->integrated_server->completed_generation_job_count();
    validation_state_.start_generation_sections =
        active_session->integrated_server->published_generation_section_count();
    validation_state_.start_visible_sections =
        active_session->chunk_view.visible_chunks_last_frame;
    validation_state_.last_visible_sections =
        validation_state_.start_visible_sections;
    validation_state_.max_visible_sections =
        validation_state_.start_visible_sections;
    core::info(
        "chunk_validation",
        "validation flight started start_visible_sections={} start_generated_columns={} start_generated_sections={}",
        validation_state_.start_visible_sections,
        validation_state_.start_generation_jobs,
        validation_state_.start_generation_sections);
  }

  if (validation_state_.phase != ChunkStreamingValidationPhase::Flying) {
    return true;
  }

  const double elapsed_seconds =
      std::chrono::duration<double>(timing.now - validation_state_.flight_started_at)
          .count();
  client_input = {};
  client_input.move_forward = true;
  client_input.sprint_held = true;
  client_input.jump_held = elapsed_seconds < 1.25;
  client_input.mouse_delta_x =
      static_cast<f32>(2.0 * std::sin(elapsed_seconds * 1.3));
  if (!validation_state_.fly_toggle_submitted && elapsed_seconds >= 0.25) {
    client_input.toggle_fly_pressed = true;
    validation_state_.fly_toggle_submitted = true;
  }
  return true;
}

void DesktopFrameLoop::record_chunk_streaming_validation_sample(
    bool has_active_session,
    DesktopGameSession* active_session,
    std::optional<double> frame_work_ms) {
  if (validation_state_.phase != ChunkStreamingValidationPhase::Flying ||
      !has_active_session ||
      active_session == nullptr ||
      active_session->integrated_server == nullptr) {
    return;
  }

  ++validation_state_.frames;
  if (frame_work_ms.has_value()) {
    validation_state_.frame_work_samples_ms.push_back(frame_work_ms.value());
    validation_state_.max_frame_work_ms =
        std::max(validation_state_.max_frame_work_ms, frame_work_ms.value());
  }

  const server::ServerRuntime& server = *active_session->integrated_server;
  const world::ChunkWorkloadSnapshot workload =
      server.voxel_world().chunk_workload_snapshot();
  const ChunkViewState& chunk_view = active_session->chunk_view;
  const usize visible_sections = chunk_view.visible_chunks_last_frame;
  if (visible_sections > validation_state_.last_visible_sections) {
    validation_state_.visible_section_growth +=
        static_cast<u64>(visible_sections - validation_state_.last_visible_sections);
  }
  validation_state_.last_visible_sections = visible_sections;
  validation_state_.max_visible_sections =
      std::max(validation_state_.max_visible_sections, visible_sections);
  validation_state_.meshed_sections += chunk_view.mesh_completed_last_frame;
  validation_state_.uploaded_meshes += renderer_.mesh_uploads_last_frame();
  validation_state_.max_pending_generation_jobs = std::max<u64>(
      validation_state_.max_pending_generation_jobs,
      server.pending_generation_job_count());
  validation_state_.max_awaiting_generation_sections = std::max<u64>(
      validation_state_.max_awaiting_generation_sections,
      workload.chunks_awaiting_generation);
  validation_state_.max_pending_client_sections = std::max(
      validation_state_.max_pending_client_sections,
      active_session->pending_client_chunk_data.size());
  validation_state_.max_mesh_candidates = std::max(
      validation_state_.max_mesh_candidates,
      chunk_view.mesh_candidates.size());
  validation_state_.max_mesh_batches_in_flight = std::max(
      validation_state_.max_mesh_batches_in_flight,
      chunk_view.pending_meshes.size());
  validation_state_.max_mesh_sections_in_flight = std::max(
      validation_state_.max_mesh_sections_in_flight,
      chunk_view.mesh_in_flight.size());
  validation_state_.max_pending_mesh_uploads = std::max(
      validation_state_.max_pending_mesh_uploads,
      renderer_.pending_mesh_upload_count());
}

void DesktopFrameLoop::finish_chunk_streaming_validation_run(
    DesktopGameSession& active_session,
    std::chrono::steady_clock::time_point now) {
  if (validation_state_.phase != ChunkStreamingValidationPhase::Flying ||
      active_session.integrated_server == nullptr) {
    return;
  }

  const double duration_seconds =
      std::chrono::duration<double>(now - validation_state_.flight_started_at)
          .count();
  const server::ServerRuntime& server = *active_session.integrated_server;
  const u64 generated_columns = monotonic_delta(
      server.completed_generation_job_count(),
      validation_state_.start_generation_jobs);
  const u64 generated_sections = monotonic_delta(
      server.published_generation_section_count(),
      validation_state_.start_generation_sections);
  const double frame_p95_ms =
      percentile(validation_state_.frame_work_samples_ms, 0.95);
  const double frame_p99_ms =
      percentile(validation_state_.frame_work_samples_ms, 0.99);

  validation_state_.summary_succeeded =
      generated_columns > 0U &&
      generated_sections > 0U &&
      validation_state_.meshed_sections > 0U &&
      validation_state_.uploaded_meshes > 0U;

  core::info(
      "chunk_validation",
      "summary status={} duration={}s frames={} generated_columns={} generated_sections={} meshed_sections={} uploaded_meshes={} visible_growth={} max_visible={} frame_p95_ms={} frame_p99_ms={} frame_max_ms={} max_pending_gen_jobs={} max_awaiting_gen_sections={} max_pending_client_sections={} max_mesh_candidates={} max_mesh_batches_in_flight={} max_mesh_sections_in_flight={} max_pending_mesh_uploads={}",
      validation_state_.summary_succeeded ? "pass" : "fail",
      duration_seconds,
      validation_state_.frames,
      generated_columns,
      generated_sections,
      validation_state_.meshed_sections,
      validation_state_.uploaded_meshes,
      validation_state_.visible_section_growth,
      validation_state_.max_visible_sections,
      frame_p95_ms,
      frame_p99_ms,
      validation_state_.max_frame_work_ms,
      validation_state_.max_pending_generation_jobs,
      validation_state_.max_awaiting_generation_sections,
      validation_state_.max_pending_client_sections,
      validation_state_.max_mesh_candidates,
      validation_state_.max_mesh_batches_in_flight,
      validation_state_.max_mesh_sections_in_flight,
      validation_state_.max_pending_mesh_uploads);

  if (!validation_options_.report_path.empty()) {
    const std::filesystem::path parent = validation_options_.report_path.parent_path();
    if (!parent.empty()) {
      std::error_code directory_error;
      std::filesystem::create_directories(parent, directory_error);
      if (directory_error) {
        core::warn(
            "chunk_validation",
            "failed to create validation report directory '{}': {}",
            parent.string(),
            directory_error.message());
      }
    }
    std::ofstream report{validation_options_.report_path};
    if (!report) {
      core::warn(
          "chunk_validation",
          "failed to open validation report '{}'",
          validation_options_.report_path.string());
    } else {
      report << "status="
             << (validation_state_.summary_succeeded ? "pass" : "fail") << '\n';
      report << "tracy_enabled="
             << (core::tracy_profiling_enabled() ? "true" : "false") << '\n';
      report << "duration_seconds=" << duration_seconds << '\n';
      report << "frames=" << validation_state_.frames << '\n';
      report << "generated_columns=" << generated_columns << '\n';
      report << "generated_sections=" << generated_sections << '\n';
      report << "meshed_sections=" << validation_state_.meshed_sections << '\n';
      report << "uploaded_meshes=" << validation_state_.uploaded_meshes << '\n';
      report << "visible_section_growth="
             << validation_state_.visible_section_growth << '\n';
      report << "max_visible_sections="
             << validation_state_.max_visible_sections << '\n';
      report << "frame_p95_ms=" << frame_p95_ms << '\n';
      report << "frame_p99_ms=" << frame_p99_ms << '\n';
      report << "frame_max_ms=" << validation_state_.max_frame_work_ms << '\n';
      report << "max_pending_generation_jobs="
             << validation_state_.max_pending_generation_jobs << '\n';
      report << "max_awaiting_generation_sections="
             << validation_state_.max_awaiting_generation_sections << '\n';
      report << "max_pending_client_sections="
             << validation_state_.max_pending_client_sections << '\n';
      report << "max_mesh_candidates="
             << validation_state_.max_mesh_candidates << '\n';
      report << "max_mesh_batches_in_flight="
             << validation_state_.max_mesh_batches_in_flight << '\n';
      report << "max_mesh_sections_in_flight="
             << validation_state_.max_mesh_sections_in_flight << '\n';
      report << "max_pending_mesh_uploads="
             << validation_state_.max_pending_mesh_uploads << '\n';
    }
  }

  validation_state_.phase = ChunkStreamingValidationPhase::ShuttingDown;
}

int DesktopFrameLoop::complete_chunk_streaming_validation_shutdown() {
  if (sessions_.shutdown_failed()) {
    return 1;
  }

  if (validation_options_.cleanup_save && !validation_state_.save_name.empty()) {
    const std::filesystem::path save_path =
        config_.paths.server_save_path(validation_state_.save_name);
    std::error_code remove_error;
    const std::uintmax_t removed_count =
        std::filesystem::remove_all(save_path, remove_error);
    if (remove_error) {
      core::warn(
          "chunk_validation",
          "failed to remove validation save '{}': {}",
          save_path.string(),
          remove_error.message());
    } else {
      core::info(
          "chunk_validation",
          "removed validation save '{}' entries={}",
          save_path.string(),
          removed_count);
    }
  }

  return validation_state_.summary_succeeded ? 0 : 1;
}

void DesktopFrameLoop::record_frame_work_profile(
    const FrameTiming& timing,
    bool has_active_session,
    DesktopGameSession* active_session) {
  if constexpr (core::profiling_enabled()) {
    const auto frame_work_end = std::chrono::steady_clock::now();
    const double frame_work_ms =
        std::chrono::duration<double, std::milli>(
            frame_work_end - timing.frame_work_start)
            .count();
    core::record_thread_timing(
        core::PerfMetric::CpuFrameWork,
        frame_work_ms,
        "main/render");
    last_frame_work_ms_ = frame_work_ms;
    record_chunk_streaming_tracy_plots(timing, has_active_session, active_session);
    record_chunk_streaming_validation_sample(
        has_active_session,
        active_session,
        frame_work_ms);
    const auto profile_now = std::chrono::steady_clock::now();
    if (frame_work_ms > static_cast<double>(config_.chunk_pipeline.frame_spike_log_ms) &&
        cooldown_due(last_frame_spike_log_, profile_now, SpikeLogCooldown)) {
      core::warn(
          "frame_profile",
          "cpu frame work spike {}ms frame_delta={}ms queued_mesh_batches={} mesh_sections_in_flight={} mesh_sections_prepared={} empty_mesh_skips={} pending_uploads={} uploads_last_frame={}",
          frame_work_ms,
          timing.frame_delta_ms,
          has_active_session ? active_session->chunk_view.pending_meshes.size() : 0U,
          has_active_session ? active_session->chunk_view.mesh_in_flight.size() : 0U,
          has_active_session ? active_session->chunk_view.mesh_sections_prepared_last_frame : 0U,
          has_active_session ? active_session->chunk_view.empty_mesh_skips_last_frame : 0U,
          renderer_.pending_mesh_upload_count(),
          renderer_.mesh_uploads_last_frame());
      last_frame_spike_log_ = profile_now;
    }
  } else {
    (void)timing;
    (void)has_active_session;
    (void)active_session;
    last_frame_work_ms_ = 0.0;
    record_chunk_streaming_validation_sample(
        has_active_session,
        active_session,
        std::nullopt);
  }
}

void DesktopFrameLoop::record_chunk_streaming_tracy_plots(
    const FrameTiming& timing,
    bool has_active_session,
    DesktopGameSession* active_session) {
  if constexpr (core::tracy_profiling_enabled()) {
    if (!has_active_session || active_session == nullptr ||
        active_session->integrated_server == nullptr) {
      chunk_streaming_plot_state_ = {};
      return;
    }

    const server::ServerRuntime& server = *active_session->integrated_server;
    const u64 completed_generation_jobs = server.completed_generation_job_count();
    const u64 published_generation_sections =
        server.published_generation_section_count();
    const usize visible_chunks = active_session->chunk_view.visible_chunks_last_frame;
    const double frame_seconds = std::max(timing.frame_delta_ms / 1000.0, 1.0 / 240.0);

    u64 generated_columns_delta = 0;
    u64 generated_sections_delta = 0;
    u64 visible_chunks_delta = 0;
    if (chunk_streaming_plot_state_.initialized) {
      generated_columns_delta = monotonic_delta(
          completed_generation_jobs,
          chunk_streaming_plot_state_.completed_generation_jobs);
      generated_sections_delta = monotonic_delta(
          published_generation_sections,
          chunk_streaming_plot_state_.published_generation_sections);
      visible_chunks_delta = static_cast<u64>(
          monotonic_delta(visible_chunks, chunk_streaming_plot_state_.visible_chunks));
    }

    chunk_streaming_plot_state_.completed_generation_jobs = completed_generation_jobs;
    chunk_streaming_plot_state_.published_generation_sections =
        published_generation_sections;
    chunk_streaming_plot_state_.visible_chunks = visible_chunks;
    chunk_streaming_plot_state_.initialized = true;

    const world::ChunkWorkloadSnapshot workload =
        server.voxel_world().chunk_workload_snapshot();
    const ChunkViewState& chunk_view = active_session->chunk_view;
    const u32 uploaded_meshes = renderer_.mesh_uploads_last_frame();

    ARV_PROFILE_PLOT(
        "arv.chunk.generated_columns_per_sec",
        per_second(generated_columns_delta, frame_seconds));
    ARV_PROFILE_PLOT(
        "arv.chunk.generated_sections_per_sec",
        per_second(generated_sections_delta, frame_seconds));
    ARV_PROFILE_PLOT(
        "arv.chunk.meshed_sections_per_sec",
        per_second(chunk_view.mesh_completed_last_frame, frame_seconds));
    ARV_PROFILE_PLOT(
        "arv.chunk.uploaded_meshes_per_sec",
        per_second(uploaded_meshes, frame_seconds));
    ARV_PROFILE_PLOT(
        "arv.chunk.visible_renderable_growth_per_sec",
        per_second(visible_chunks_delta, frame_seconds));

    ARV_PROFILE_PLOT(
        "arv.chunk.pending_generation_jobs",
        static_cast<double>(server.pending_generation_job_count()));
    ARV_PROFILE_PLOT(
        "arv.chunk.awaiting_generation_sections",
        static_cast<double>(workload.chunks_awaiting_generation));
    ARV_PROFILE_PLOT(
        "arv.chunk.pending_client_sections",
        static_cast<double>(active_session->pending_client_chunk_data.size()));
    ARV_PROFILE_PLOT(
        "arv.chunk.mesh_candidates",
        static_cast<double>(chunk_view.mesh_candidates.size()));
    ARV_PROFILE_PLOT(
        "arv.chunk.mesh_batches_in_flight",
        static_cast<double>(chunk_view.pending_meshes.size()));
    ARV_PROFILE_PLOT(
        "arv.chunk.mesh_sections_in_flight",
        static_cast<double>(chunk_view.mesh_in_flight.size()));
    ARV_PROFILE_PLOT(
        "arv.chunk.pending_mesh_uploads",
        static_cast<double>(renderer_.pending_mesh_upload_count()));
    ARV_PROFILE_PLOT(
        "arv.chunk.visible_renderable_sections",
        static_cast<double>(visible_chunks));
  } else {
    (void)timing;
    (void)has_active_session;
    (void)active_session;
    chunk_streaming_plot_state_ = {};
  }
}

} // namespace arv::desktop
