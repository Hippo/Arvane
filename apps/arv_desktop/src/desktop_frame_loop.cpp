#include "desktop_frame_loop.hpp"

#include "debug_menu.hpp"
#include "desktop_frame_pacing.hpp"
#include "desktop_input_routing.hpp"
#include "desktop_render_builders.hpp"
#include "input_adapter.hpp"
#include "ui_model.hpp"

#include "arv/core/log.hpp"
#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"
#include "arv/net/connection_state.hpp"
#include "arv/ui/screens/main_menu.hpp"
#include "arv/ui/screens/pause_menu.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace arv::desktop {

bool session_world_ready(const DesktopGameSession* session) noexcept {
  return session != nullptr && session->client != nullptr &&
         session->client->connection_state() == net::ConnectionState::InGame;
}

namespace {

constexpr auto SpikeLogCooldown = std::chrono::milliseconds{500};

[[nodiscard]] bool spike_log_due(
    std::chrono::steady_clock::time_point last_log,
    std::chrono::steady_clock::time_point now) noexcept {
  return last_log.time_since_epoch().count() == 0 ||
         now - last_log >= SpikeLogCooldown;
}

[[nodiscard]] ui::InputBindingDevice ui_device_from(platform::InputDeviceType type) noexcept {
  switch (type) {
    case platform::InputDeviceType::Keyboard:
      return ui::InputBindingDevice::Keyboard;
    case platform::InputDeviceType::MouseButton:
      return ui::InputBindingDevice::MouseButton;
    case platform::InputDeviceType::MouseWheel:
      return ui::InputBindingDevice::MouseWheel;
  }
  return ui::InputBindingDevice::Keyboard;
}

[[nodiscard]] std::string binding_label(const platform::InputBinding& binding) {
  std::string label;
  if (platform::has_input_modifier(binding.modifiers, platform::InputModifier::Control)) {
    label += "Ctrl + ";
  }
  if (platform::has_input_modifier(binding.modifiers, platform::InputModifier::Shift)) {
    label += "Shift + ";
  }
  if (platform::has_input_modifier(binding.modifiers, platform::InputModifier::Alt)) {
    label += "Alt + ";
  }
  switch (binding.device_type) {
    case platform::InputDeviceType::Keyboard:
      label += binding.input_code;
      break;
    case platform::InputDeviceType::MouseButton:
      label += "Mouse " + binding.input_code;
      break;
    case platform::InputDeviceType::MouseWheel:
      label += "Wheel " + binding.input_code;
      break;
  }
  return label;
}

[[nodiscard]] ui::CapturedInputView captured_input_from(
    const platform::RawInputEvent& event) {
  return {
      ui_device_from(event.binding.device_type),
      event.binding.input_code,
      binding_label(event.binding),
      event.binding.modifiers};
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
    DesktopSessionManager& sessions,
    ui::UiTree& ui_tree,
    ui::FontUiTextMeasurer& ui_text_measurer,
    const ui::UiTheme& ui_theme,
    const voxel::FluidDefinition& water_fluid,
    player::PlayerTuning& player_tuning,
    DesktopMenuState& menu_state,
    ui::UIScreenManager& screen_manager) : config_(config),
                                           resources_(resources),
                                           window_(window),
                                           render_thread_(render_thread),
                                           renderer_(renderer),
                                           input_bindings_(input_bindings),
                                           debug_imgui_(debug_imgui),
                                           debug_menu_(debug_menu),
                                           assets_(assets),
                                           world_jobs_(world_jobs),
                                           sessions_(sessions),
                                           ui_tree_(ui_tree),
                                           ui_text_measurer_(ui_text_measurer),
                                           ui_theme_(ui_theme),
                                           water_fluid_(water_fluid),
                                           player_tuning_(player_tuning),
                                           menu_state_(menu_state),
                                           screen_manager_(screen_manager),
                                           framebuffer_(window.framebuffer_size()) {
  const auto start = std::chrono::steady_clock::now();
  previous_time_ = start;
  app_start_time_ = start;
  last_fps_display_time_ = start;
}

int DesktopFrameLoop::run() {
  while (!window_.should_close()) {
    ARV_PROFILE_ZONE("Frame time");
    FrameTiming timing = begin_frame();
    platform::InputSnapshot platform_input = poll_platform_input();
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
    handle_captured_menu_input(platform_input, menu_context, active_screens);
    handle_pause_menu_input(
        platform_input,
        menu_context,
        active_screens,
        active_session,
        has_world_session);

    client::InputFrame client_input =
        has_world_session ? to_client_input(platform_input) : client::InputFrame{};
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

    update_framebuffer_size();
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
    ARV_PROFILE_FRAME_MARK("Frame");
  }

  return 0;
}

ui::UIScreenManager& DesktopFrameLoop::active_screen_manager() noexcept {
  return gameplay_menu_stack_active_ ? gameplay_screen_manager_ : screen_manager_;
}

const ui::UIScreenManager& DesktopFrameLoop::active_screen_manager() const noexcept {
  return gameplay_menu_stack_active_ ? gameplay_screen_manager_ : screen_manager_;
}

void DesktopFrameLoop::reset_main_menu_stack() {
  screen_manager_.clear();
  screen_manager_.push(ui::make_main_menu_screen());
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
      {
          static_cast<f32>(framebuffer_.width),
          static_cast<f32>(framebuffer_.height),
      },
      &menu_state_,
      &menu_state_,
      &screens,
      &ui_theme_,
      &assets_.language};
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
  (void)has_session;
  if (debug_menu_.open || !platform_input.pause_menu_pressed) {
    return;
  }
  const bool has_live_session =
      active_session != nullptr && active_session->client != nullptr;
  if (gameplay_menu_stack_active_ && screens.empty()) {
    reset_gameplay_menu_stack();
  }
  if (screens.has_active_screen()) {
    (void)screens.handle_cancel(menu_context);
  } else if (gameplay_menu_stack_active_ && has_live_session &&
             !active_session->client->inventory_open() &&
             !active_session->client->chat_open()) {
    screens.push(ui::make_pause_menu_screen());
  } else if (!gameplay_menu_stack_active_ && screens.empty()) {
    screens.push(ui::make_main_menu_screen());
  }
}

void DesktopFrameLoop::handle_captured_menu_input(
    platform::InputSnapshot& platform_input,
    ui::UIContext& menu_context,
    ui::UIScreenManager& screens) {
  if (debug_menu_.open || !screens.has_active_screen() ||
      !platform_input.raw_input.has_value()) {
    return;
  }
  if (platform_input.raw_input->binding.device_type == platform::InputDeviceType::MouseButton) {
    const std::optional<ui::UiNodeID> hit =
        ui_tree_.hit_test({platform_input.cursor_x, platform_input.cursor_y});
    if (hit.has_value()) {
      const ui::UiElement* element = ui_tree_.element(hit.value());
      if (element != nullptr && element->action == "key_bind.cancel_capture") {
        return;
      }
    }
  }
  const ui::CapturedInputView captured =
      captured_input_from(platform_input.raw_input.value());
  if (!screens.handle_captured_input(captured, menu_context)) {
    return;
  }
  platform_input.pause_menu_pressed = false;
  platform_input.text_cancel_pressed = false;
  platform_input.text_submit_pressed = false;
  platform_input.text_backspace_pressed = false;
  platform_input.open_inventory_pressed = false;
  platform_input.open_chat_pressed = false;
  platform_input.debug_menu_toggle_pressed = false;
  platform_input.hotbar_select.reset();
  platform_input.hotbar_scroll_steps = 0;
  platform_input.mouse_wheel_y = 0.0F;
  platform_input.raw_input.reset();
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
    (void)route_screen_mouse_input(screens, menu_context, platform_input);
    (void)screens.handle_pointer(
        {platform_input.cursor_x, platform_input.cursor_y},
        {platform_input.mouse_delta_x, platform_input.mouse_delta_y},
        platform_input.mouse_left_pressed,
        platform_input.mouse_left_released,
        platform_input.mouse_left_held,
        menu_context);
    if (platform_input.mouse_wheel_y != 0.0F) {
      (void)screens.handle_scroll(
          {platform_input.cursor_x, platform_input.cursor_y},
          platform_input.mouse_wheel_y,
          menu_context);
    }
    if (!platform_input.text_input.empty()) {
      (void)screens.handle_text_input(platform_input.text_input, menu_context);
    }
    if (platform_input.text_backspace_pressed) {
      (void)screens.handle_backspace(menu_context);
    }
    if (platform_input.text_submit_pressed) {
      (void)screens.handle_submit(menu_context);
    }
  } else if (!debug_menu_.open && has_session && active_session->client->inventory_open()) {
    inventory_changed_by_ui = route_inventory_mouse_input(
        ui_tree_,
        *active_session->client,
        platform_input,
        active_session->client->inventory_cursor_stack().has_value());
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
    update_held_item_animation_from_input(
        *active_session,
        client_input,
        std::chrono::duration<Seconds>(now - app_start_time_).count());
    active_session->client->submit_input(std::move(client_input));
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
      assets_.entity_render_assets,
      player_skin_regions_,
      config_.render,
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
    const auto profile_now = std::chrono::steady_clock::now();
    if (frame_work_ms > static_cast<double>(config_.chunk_pipeline.frame_spike_log_ms) &&
        spike_log_due(last_frame_spike_log_, profile_now)) {
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
  }
}

} // namespace arv::desktop
