#include "desktop_application.hpp"

#include "app_config.hpp"
#include "desktop_assets.hpp"
#include "desktop_frame_loop.hpp"
#include "desktop_menu_state.hpp"
#include "desktop_runtime_config.hpp"
#include "renderer_bootstrap.hpp"

#include "arv/asset/resource.hpp"
#include "arv/core/log.hpp"
#include "arv/core/profiling.hpp"
#include "arv/io/config.hpp"
#include "arv/io/input_bindings.hpp"
#include "arv/platform/glfw_window.hpp"
#include "arv/render/opengl_renderer.hpp"
#include "arv/sched/scheduler.hpp"
#include "arv/ui/ui.hpp"

#include <utility>

namespace arv::desktop {

int DesktopApplication::run(int argc, char** argv) {
  core::info("desktop", "starting Arvane desktop client");

  auto config_result = io::load_config(parse_config_options(argc, argv));
  if (!config_result) {
    core::error("config", config_result.error().message);
    return 1;
  }
  const io::EngineConfig config = config_result.value();
  const ui::UiThemeLoadResult theme_result =
      ui::load_ui_theme(config.paths.arvane_home_path());
  const ui::UiTheme ui_theme = theme_result.theme;
  core::info(
      "ui/theme",
      "loaded theme '{}' ({})",
      ui_theme.name,
      theme_result.loaded_theme);
  asset::ResourceManager resources(config.paths.assets_path());

  platform::GlfwLibrary glfw;
  if (!glfw.initialized()) {
    core::error("platform", "failed to initialize GLFW");
    return 1;
  }

  const bool swap_interval_vsync =
      config.window.vsync && !config.render.frame_pacing_enabled;
  auto window_result = platform::OpenGLWindow::create({config.window.width,
                                                       config.window.height,
                                                       config.window.title,
                                                       swap_interval_vsync});
  if (!window_result) {
    core::error("platform", window_result.error().message);
    return 1;
  }
  core::info(
      "render",
      "vsync={} frame_pacing={} target_fps={}",
      swap_interval_vsync ? "enabled" : "disabled",
      config.render.frame_pacing_enabled ? "enabled" : "disabled",
      config.render.frame_pacing_target_fps);
  platform::OpenGLWindow window = std::move(window_result.value());
  core::set_profile_thread_name("main/render");

  DebugImGuiLayer debug_imgui;
  if (!debug_imgui.initialize(window.native_handle())) {
    core::error("debug_ui", "failed to initialize ImGui debug UI");
    return 1;
  }
  DebugMenuState debug_menu;

  const auto input_bindings_result =
      io::load_input_bindings_file_or_defaults(config.paths.input_bindings_file);
  if (!input_bindings_result) {
    core::warn("input", input_bindings_result.error().message);
  } else {
    window.set_input_bindings(input_bindings_result.value());
  }

  render::OpenGLRenderer renderer;
  if (!configure_renderer(renderer, resources, config)) {
    debug_imgui.shutdown();
    return 1;
  }

  auto framebuffer = window.framebuffer_size();
  renderer.resize(framebuffer.width, framebuffer.height);

  auto loaded_assets_result = load_desktop_assets(resources, renderer, config, ui_theme);
  if (!loaded_assets_result) {
    core::error(
        loaded_assets_result.error().category,
        loaded_assets_result.error().message);
    debug_imgui.shutdown();
    renderer.begin_shutdown();
    return 1;
  }
  DesktopLoadedAssets assets = std::move(loaded_assets_result.value());
  ui::FontUiTextMeasurer ui_text_measurer(assets.font_system, assets.ui_font_stacks);
  ui::UiTree ui_tree;
  ui_tree.set_theme(&ui_theme);

  const server::ServerChunkStreamingConfig chunk_streaming =
      make_chunk_streaming_config(config);
  const world::WorldTickPolicy world_tick_policy =
      make_world_tick_policy(config.simulation);
  const voxel::FluidDefinition water_fluid =
      make_water_fluid_definition(config.simulation);
  const player::PlayerTuning player_tuning =
      make_player_tuning(config.physics);
  const sched::RuntimeThreadBudgetPlan thread_budget =
      make_runtime_thread_budget_plan(config.jobs);

  sched::Scheduler world_jobs{thread_budget.secondary_workers};
  core::info(
      "jobs",
      "hardware_threads={} reserved_threads={} server_workers={} client_workers={}",
      thread_budget.hardware_thread_count,
      thread_budget.reserved_thread_count,
      thread_budget.primary_worker_thread_count,
      world_jobs.worker_count());

  DesktopSessionManager sessions{
      {
          config,
          renderer,
          world_jobs,
          assets.block_registry,
          assets.block_registry_hash,
          assets.item_registry,
          assets.item_registry_hash,
          assets.entity_registry,
          assets.block_atlas,
          assets.item_atlas,
          player_tuning,
          water_fluid,
          chunk_streaming,
          world_tick_policy,
          assets.default_generation_config,
          thread_budget,
          assets.language,
      }};

  DesktopMenuState menu_state{
      config.paths.saves_path(),
      config.paths.server_list_file,
      config.paths.arvane_home_path(),
      &assets.language};
  ui::UIScreenManager screen_manager;
  screen_manager.push(ui::make_main_menu_screen());

  DesktopFrameLoop frame_loop{
      config,
      resources,
      window,
      renderer,
      debug_imgui,
      debug_menu,
      assets,
      world_jobs,
      sessions,
      ui_tree,
      ui_text_measurer,
      ui_theme,
      water_fluid,
      menu_state,
      screen_manager};
  const int loop_result = frame_loop.run();

  const bool save_result = sessions.save_and_shutdown();
  debug_imgui.shutdown();
  renderer.begin_shutdown();
  if (loop_result != 0) {
    return loop_result;
  }
  if (!save_result) {
    return 1;
  }
  core::info("desktop", "shutdown complete");
  return 0;
}

int run_desktop_client(int argc, char** argv) {
  DesktopApplication app;
  return app.run(argc, argv);
}

} // namespace arv::desktop
