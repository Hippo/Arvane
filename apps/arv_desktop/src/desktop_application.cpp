#include "desktop_application.hpp"

#include "app_config.hpp"
#include "desktop_assets.hpp"
#include "desktop_frame_loop.hpp"
#include "desktop_menu_state.hpp"
#include "desktop_render_thread.hpp"
#include "desktop_runtime_config.hpp"
#include "renderer_bootstrap.hpp"

#include "arv/asset/resource.hpp"
#include "arv/base/base_commands.hpp"
#include "arv/base/base_ids.hpp"
#include "arv/base/base_module.hpp"
#include "arv/base/ui.hpp"
#include "arv/base/ui/screens/account.hpp"
#include "arv/core/log.hpp"
#include "arv/core/profiling.hpp"
#include "arv/io/config.hpp"
#include "arv/io/input_bindings.hpp"
#include "arv/platform/glfw_window.hpp"
#include "arv/plugin/plugin_manager.hpp"
#include "arv/sched/job_system.hpp"
#include "arv/sched/task_executor.hpp"
#include "arv/world/entity_goal_registry.hpp"

#include <utility>

namespace arv::desktop {

int DesktopApplication::run(int argc, char** argv) {
  core::info("desktop", "starting Arvane desktop client");

  const io::ConfigLoadOptions config_options = parse_config_options(argc, argv);
  auto config_result = io::load_config(config_options);
  if (!config_result) {
    core::error("config", config_result.error().message);
    return 1;
  }
  io::EngineConfig config = config_result.value();
  auto pack_manager_result = asset::PackManager::load(config.paths.arvane_home_path());
  if (!pack_manager_result) {
    core::error("packs", pack_manager_result.error().describe());
    return 1;
  }
  asset::PackManager pack_manager = std::move(pack_manager_result.value());
  core::info("packs", "loaded {} enabled packs", pack_manager.stack().size());
  const ui::UiThemeLoadResult theme_result =
      ui::load_ui_theme(pack_manager, config.paths.config_path() / "style.yaml");
  const ui::UiTheme ui_theme = theme_result.theme;
  core::info(
      "ui/theme",
      "loaded theme '{}' ({})",
      ui_theme.name,
      theme_result.loaded_theme);
  asset::ResourceManager resources(pack_manager);

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
  core::set_profile_thread_name("main/platform");

  DebugImGuiLayer debug_imgui;
  if (!debug_imgui.initialize(window.native_handle())) {
    core::error("debug_ui", "failed to initialize ImGui debug UI");
    return 1;
  }
  DebugMenuState debug_menu;

  platform::InputBindings input_bindings = platform::default_input_bindings();
  const auto input_bindings_result =
      io::load_input_bindings_file_or_defaults(config.paths.input_bindings_file);
  if (!input_bindings_result) {
    core::warn("input", input_bindings_result.error().message);
  } else {
    input_bindings = input_bindings_result.value();
    window.set_input_bindings(input_bindings);
  }

  DesktopRenderThread desktop_renderer{window};
  if (!desktop_renderer.start(swap_interval_vsync, debug_imgui.font_atlas())) {
    debug_imgui.shutdown();
    return 1;
  }
  render::RenderCommandQueue& render_commands = desktop_renderer.commands();
  if (!configure_renderer(render_commands, resources, config)) {
    desktop_renderer.shutdown();
    debug_imgui.shutdown();
    return 1;
  }

  world::EntityGoalRegistry entity_goals;
  voxel::BlockBehaviorRegistry block_behaviors;
  player::ItemBehaviorRegistry item_behaviors;
  player::GameModeRegistry game_modes;
  base::register_base_game_modes(game_modes);
  base::register_base_block_behaviors(block_behaviors);
  base::register_base_item_behaviors(item_behaviors);
  base::register_base_entity_goals(entity_goals);

  plugin::PluginManager plugin_manager;
  auto plugin_result = plugin_manager.load_plugins({
      &pack_manager,
      plugin::make_native_plugin_policy(
          config.native_plugins.enabled,
          config.native_plugins.allowed_packs),
      config.paths.config_path(),
      &game_modes,
      &block_behaviors,
      &item_behaviors,
      &entity_goals,
  });
  if (!plugin_result) {
    core::error("plugins", plugin_result.error().describe());
    desktop_renderer.shutdown();
    debug_imgui.shutdown();
    return 1;
  }

  auto framebuffer = window.framebuffer_size();
  auto window_size = window.window_size();
  const ui::UiDisplayMetrics initial_ui_metrics =
      ui::UiDisplayMetrics::from_window_and_framebuffer(
          {
              static_cast<f32>(window_size.width),
              static_cast<f32>(window_size.height),
          },
          {
              static_cast<f32>(framebuffer.width),
              static_cast<f32>(framebuffer.height),
          });
  render_commands.resize(framebuffer.width, framebuffer.height);

  auto loaded_assets_result =
      load_desktop_assets(
          resources,
          render_commands,
          config,
          pack_manager,
          ui_theme,
          initial_ui_metrics,
          block_behaviors,
          item_behaviors,
          entity_goals);
  if (!loaded_assets_result) {
    core::error(
        loaded_assets_result.error().category,
        loaded_assets_result.error().message);
    desktop_renderer.shutdown();
    debug_imgui.shutdown();
    return 1;
  }
  render_commands.flush();
  DesktopLoadedAssets assets = std::move(loaded_assets_result.value());
  ui::FontUiTextMeasurer ui_text_measurer(
      assets.font_system,
      assets.ui_font_stacks,
      initial_ui_metrics.scalar_to_framebuffer(1.0F));
  ui::UiTree ui_tree;
  ui_tree.set_theme(&ui_theme);

  const world::WorldTickPolicy world_tick_policy =
      make_world_tick_policy(config.simulation);
  const voxel::FluidDefinition water_fluid =
      make_water_fluid_definition(config.simulation);
  server::BlockBreakBehaviorRegistry block_break_behaviors;
  base::register_base_block_break_behaviors(block_break_behaviors);
  std::vector<server::StarterInventoryItem> starter_inventory;
  for (const base::StarterInventoryItem item : base::starter_inventory_items()) {
    starter_inventory.push_back({std::string{item.item_name}, item.count});
  }
  server::FluidContainerInteractionConfig fluid_containers;
  fluid_containers.empty_container_item_name =
      std::string{base::ids::BucketItemID};
  fluid_containers.filled_source_item_name =
      std::string{base::ids::WaterBucketItemID};
  player::PlayerTuning player_tuning =
      make_player_tuning(config.physics);
  const sched::RuntimeThreadBudgetPlan thread_budget =
      make_runtime_thread_budget_plan(config.jobs);
  const sched::JobSystemConfig client_job_config =
      make_client_job_system_config(config.jobs, thread_budget);
  sched::JobSystem world_jobs{client_job_config};
  world_jobs.start();
  sched::JobSystemTaskExecutor client_tasks{world_jobs};
  core::info(
      "jobs",
      "hardware_threads={} reserved_threads={} server_workers={} client_workers={} client_critical={} client_io={}",
      thread_budget.hardware_thread_count,
      thread_budget.reserved_thread_count,
      thread_budget.primary_worker_thread_count,
      world_jobs.worker_count(sched::JobLane::BackgroundCpu),
      world_jobs.worker_count(sched::JobLane::CriticalCpu),
      world_jobs.worker_count(sched::JobLane::BlockingIo));
  core::info(
      "threading",
      "render_owner=render thread network_owner=transport pump owner world_owner=session tick owner");

  DesktopSessionManager sessions{
      {
          config,
          render_commands,
          world_jobs,
          client_tasks,
          assets.block_registry,
          assets.block_registry_hash,
          assets.item_registry,
          assets.item_registry_hash,
          game_modes,
          block_break_behaviors,
          std::move(starter_inventory),
          std::move(fluid_containers),
          [&plugin_manager](command::CommandRegistry& registry) {
            base::register_server_commands(registry);
            plugin_manager.register_commands(registry);
          },
          assets.entity_registry,
          assets.vehicle_registry,
          entity_goals,
          assets.block_atlas,
          assets.fluid_render_data,
          assets.item_atlas,
          assets.particle_assets.registry,
          player_tuning,
          water_fluid,
          world_tick_policy,
          assets.default_generation_config,
          thread_budget,
          assets.language,
          pack_manager.required_packs(),
          &plugin_manager,
      }};

  DesktopMenuState menu_state{
      config.paths.saves_path(),
      config.paths.server_list_file,
      config.paths.arvane_home_path(),
      &config,
      &input_bindings,
      &assets.language};
  ui::UIScreenManager screen_manager;
  screen_manager.push(ui::make_main_menu_screen());
  if (menu_state.local_profile_store().profiles.empty()) {
    screen_manager.push(ui::make_account_create_screen());
  }

  DesktopFrameLoop frame_loop{
      config,
      resources,
      window,
      desktop_renderer,
      render_commands,
      input_bindings,
      debug_imgui,
      debug_menu,
      assets,
      world_jobs,
      client_tasks,
      sessions,
      ui_tree,
      ui_text_measurer,
      ui_theme,
      water_fluid,
      player_tuning,
      menu_state,
      screen_manager,
      block_behaviors,
      item_behaviors,
      entity_goals};
  const int loop_result = frame_loop.run();

  const auto config_save_result =
      config_options.explicit_config_path.empty()
          ? io::save_config_files(config, io::ConfigProfile::Client)
          : io::save_config_file(config.paths.config_file, config);
  if (!config_save_result) {
    core::warn("config", config_save_result.error().message);
  }
  const auto input_bindings_save_result =
      io::save_input_bindings_file(config.paths.input_bindings_file, input_bindings);
  if (!input_bindings_save_result) {
    core::warn("input", input_bindings_save_result.error().message);
  }

  const bool save_result = sessions.save_and_shutdown();
  plugin_manager.shutdown();
  desktop_renderer.shutdown();
  debug_imgui.shutdown();
  if (loop_result != 0) {
    return loop_result;
  }
  if (!config_save_result || !input_bindings_save_result || !save_result) {
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
