#include "desktop_application.hpp"

#include "app_config.hpp"
#include "asset_helpers.hpp"
#include "debug_imgui.hpp"
#include "debug_menu.hpp"
#include "desktop_assets.hpp"
#include "desktop_frame_pacing.hpp"
#include "desktop_input_routing.hpp"
#include "desktop_menu_state.hpp"
#include "desktop_render_builders.hpp"
#include "desktop_runtime_config.hpp"
#include "desktop_save_helpers.hpp"
#include "desktop_session.hpp"
#include "input_adapter.hpp"
#include "renderer_bootstrap.hpp"
#include "ui_model.hpp"

#include "arv/asset/font.hpp"
#include "arv/asset/resource.hpp"
#include "arv/command/command.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/core/arena.hpp"
#include "arv/core/log.hpp"
#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"
#include "arv/io/config.hpp"
#include "arv/io/input_bindings.hpp"
#include "arv/platform/glfw_window.hpp"
#include "arv/physics/collision.hpp"
#include "arv/protocol/skins.hpp"
#include "arv/render/opengl_renderer.hpp"
#include "arv/sched/scheduler.hpp"
#include "arv/ui/ui.hpp"
#include "arv/world/world_queries.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace arv::desktop {
namespace {

struct FrameTiming {
  std::chrono::steady_clock::time_point now{};
  std::chrono::duration<Seconds> delta{};
  double frame_delta_ms{};
  std::chrono::steady_clock::time_point frame_work_start{};
};

enum class HeldItemActionPlayResult : u8 {
  NoAnimation,
  Suppressed,
  Played,
};

struct UiBuildCacheKey {
  u32 width{};
  u32 height{};
  bool has_active_session{};
  bool has_active_screen{};
  u64 screen_revision{};
  u64 gameplay_signature{};

  [[nodiscard]] bool operator==(const UiBuildCacheKey& other) const noexcept {
    return width == other.width && height == other.height &&
           has_active_session == other.has_active_session &&
           has_active_screen == other.has_active_screen &&
           screen_revision == other.screen_revision &&
           gameplay_signature == other.gameplay_signature;
  }
};

[[nodiscard]] u64 ui_cache_hash_append_byte(u64 hash, u8 value) noexcept {
  constexpr u64 Prime = 1099511628211ULL;
  hash ^= value;
  hash *= Prime;
  return hash;
}

[[nodiscard]] u64 ui_cache_hash_append_u64(u64 hash, u64 value) noexcept {
  for (usize index = 0; index < sizeof(value); ++index) {
    hash = ui_cache_hash_append_byte(
        hash,
        static_cast<u8>((value >> (index * 8U)) & 0xFFU));
  }
  return hash;
}

[[nodiscard]] u64 ui_cache_hash_append_bool(u64 hash, bool value) noexcept {
  return ui_cache_hash_append_byte(hash, value ? 1U : 0U);
}

[[nodiscard]] u64 ui_cache_hash_append_string(u64 hash, std::string_view value) noexcept {
  hash = ui_cache_hash_append_u64(hash, value.size());
  for (char ch : value) {
    hash = ui_cache_hash_append_byte(hash, static_cast<u8>(ch));
  }
  return hash;
}

[[nodiscard]] u64 ui_cache_hash_append_f32(u64 hash, f32 value) noexcept {
  const auto quantized =
      static_cast<std::int64_t>(std::llround(static_cast<double>(value) * 100.0));
  return ui_cache_hash_append_u64(hash, static_cast<u64>(quantized));
}

[[nodiscard]] u64 ui_cache_hash_append_item_stack(
    u64 hash,
    const ui::UiItemStackView& stack) noexcept {
  hash = ui_cache_hash_append_string(hash, stack.item_name);
  hash = ui_cache_hash_append_string(hash, stack.display_name);
  hash = ui_cache_hash_append_u64(hash, stack.count);
  hash = ui_cache_hash_append_u64(hash, stack.durability);
  hash = ui_cache_hash_append_u64(hash, stack.max_durability);
  hash = ui_cache_hash_append_u64(hash, static_cast<u64>(stack.render_type));
  hash = ui_cache_hash_append_bool(hash, stack.texture_icon.valid);
  hash = ui_cache_hash_append_u64(hash, stack.texture_icon.page);
  hash = ui_cache_hash_append_u64(hash, stack.block_model_quads.size());
  return hash;
}

[[nodiscard]] u64 ui_cache_hash_append_optional_item_stack(
    u64 hash,
    const std::optional<ui::UiItemStackView>& stack) noexcept {
  hash = ui_cache_hash_append_bool(hash, stack.has_value());
  return stack.has_value() ? ui_cache_hash_append_item_stack(hash, stack.value()) : hash;
}

[[nodiscard]] u64 gameplay_ui_signature(
    const DesktopGameSession& session,
    const platform::InputSnapshot& platform_input,
    u32 displayed_fps) noexcept {
  constexpr u64 OffsetBasis = 1469598103934665603ULL;
  u64 hash = OffsetBasis;
  hash = ui_cache_hash_append_u64(
      hash,
      static_cast<u64>(reinterpret_cast<std::uintptr_t>(session.client.get())));
  if (session.client == nullptr) {
    return hash;
  }

  hash = ui_cache_hash_append_u64(hash, session.client->selected_hotbar_slot());
  hash = ui_cache_hash_append_bool(hash, session.client->inventory_open());
  hash = ui_cache_hash_append_bool(hash, session.client->chat_open());
  hash = ui_cache_hash_append_bool(hash, session.client->tab_menu_visible());
  hash = ui_cache_hash_append_string(hash, session.client->chat_draft());
  hash = ui_cache_hash_append_u64(hash, displayed_fps);

  hash = ui_cache_hash_append_u64(hash, session.inventory_slots.size());
  for (const ui::UiItemStackView& slot : session.inventory_slots) {
    hash = ui_cache_hash_append_item_stack(hash, slot);
  }
  hash = ui_cache_hash_append_optional_item_stack(hash, session.carried_inventory_stack);
  if (session.client->inventory_open()) {
    hash = ui_cache_hash_append_f32(hash, platform_input.cursor_x);
    hash = ui_cache_hash_append_f32(hash, platform_input.cursor_y);
  }

  hash = ui_cache_hash_append_u64(hash, session.chat_lines.size());
  for (const ui::ChatLineView& line : session.chat_lines) {
    hash = ui_cache_hash_append_string(hash, line.text);
    hash = ui_cache_hash_append_f32(hash, line.opacity);
  }

  hash = ui_cache_hash_append_u64(hash, session.tab_players.size());
  for (const ui::PlayerListEntryView& player : session.tab_players) {
    hash = ui_cache_hash_append_string(hash, player.name);
    hash = ui_cache_hash_append_bool(hash, player.latency_ms.has_value());
    if (player.latency_ms.has_value()) {
      hash = ui_cache_hash_append_u64(hash, player.latency_ms.value());
    }
  }
  return hash;
}

[[nodiscard]] bool is_command_chat_draft(std::string_view draft) noexcept {
  return !draft.empty() && draft.front() == '/';
}

[[nodiscard]] std::string fallback_command_result_message(command::CommandStatus status) {
  switch (status) {
    case command::CommandStatus::Success:
      return "Command succeeded.";
    case command::CommandStatus::Failure:
      return "Command failed.";
    case command::CommandStatus::UsageError:
      return "Invalid command usage.";
    case command::CommandStatus::PermissionDenied:
      return "Permission denied.";
    case command::CommandStatus::UnknownCommand:
      return "Unknown command.";
    case command::CommandStatus::InvalidSource:
      return "Command cannot be used from here.";
    case command::CommandStatus::InternalError:
      return "Command failed internally.";
  }
  return "Command failed.";
}

void append_command_result_to_chat(
    client::ClientRuntime& client,
    const command::CommandResult& result) {
  bool wrote_message = false;
  for (const command::CommandMessage& message : result.messages) {
    if (message.text.empty()) {
      continue;
    }
    client.add_chat_message(message.text);
    wrote_message = true;
  }
  if (!wrote_message) {
    client.add_chat_message(fallback_command_result_message(result.status));
  }
}

[[nodiscard]] BlockPos offset_block(BlockPos pos, voxel::BlockFace face) noexcept {
  switch (face) {
    case voxel::BlockFace::PositiveX:
      return {{pos.value.x + 1, pos.value.y, pos.value.z}};
    case voxel::BlockFace::NegativeX:
      return {{pos.value.x - 1, pos.value.y, pos.value.z}};
    case voxel::BlockFace::PositiveY:
      return {{pos.value.x, pos.value.y + 1, pos.value.z}};
    case voxel::BlockFace::NegativeY:
      return {{pos.value.x, pos.value.y - 1, pos.value.z}};
    case voxel::BlockFace::PositiveZ:
      return {{pos.value.x, pos.value.y, pos.value.z + 1}};
    case voxel::BlockFace::NegativeZ:
      return {{pos.value.x, pos.value.y, pos.value.z - 1}};
  }
  return pos;
}

[[nodiscard]] physics::KinematicConfig make_prediction_kinematic_config(
    const io::PhysicsConfig& physics) noexcept {
  physics::KinematicConfig config;
  config.body_width = physics.body_width;
  config.body_height = physics.body_height;
  config.sneaking_body_height = physics.sneaking_body_height;
  config.swimming_body_width = physics.swimming_body_width;
  config.swimming_body_height = physics.swimming_body_height;
  return config;
}

[[nodiscard]] std::optional<std::vector<u8>> read_binary_file(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  return std::vector<u8>{
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{}};
}

[[nodiscard]] std::filesystem::path player_skin_render_path(
    const client::LocalPlayerIdentity& identity,
    std::string_view skin_hash) {
  for (const client::LocalProfileSkin& local_skin : identity.skins) {
    if (local_skin.skin_hash == skin_hash) {
      return client::local_skin_path(identity.arvane_home, local_skin);
    }
  }
  return client::cached_skin_path(identity.arvane_home, skin_hash);
}

[[nodiscard]] std::optional<asset::Image> load_valid_player_skin_render_image(
    const client::LocalPlayerIdentity& identity,
    const protocol::SkinDescriptor& skin) {
  if (identity.arvane_home.empty() || skin.skin_hash.empty() ||
      !protocol::is_valid_skin_hash(skin.skin_hash)) {
    return std::nullopt;
  }
  std::optional<std::vector<u8>> bytes =
      read_binary_file(player_skin_render_path(identity, skin.skin_hash));
  if (!bytes.has_value() ||
      protocol::skin_hash_for_bytes(*bytes) != skin.skin_hash) {
    return std::nullopt;
  }

  auto image = asset::decode_png_image_bytes(
      *bytes,
      {"arvane:texture/player_skin/" + skin.skin_hash});
  if (!image || image.value().width != skin.width ||
      image.value().height != skin.height ||
      image.value().pixels.size() !=
          static_cast<usize>(skin.width) * static_cast<usize>(skin.height) * 4U) {
    return std::nullopt;
  }
  return std::move(image.value());
}

[[nodiscard]] std::optional<asset::Image> load_valid_local_profile_skin_render_image(
    const std::filesystem::path& arvane_home,
    const client::LocalProfileSkin& skin) {
  if (skin.skin_hash.empty() || !protocol::is_valid_skin_hash(skin.skin_hash)) {
    return std::nullopt;
  }
  std::optional<std::vector<u8>> bytes =
      read_binary_file(client::local_skin_path(arvane_home, skin));
  if (!bytes.has_value() ||
      protocol::skin_hash_for_bytes(*bytes) != skin.skin_hash) {
    return std::nullopt;
  }

  auto image = asset::decode_png_image_bytes(
      *bytes,
      {"arvane:texture/player_skin/" + skin.skin_hash});
  if (!image || image.value().width != skin.width ||
      image.value().height != skin.height ||
      image.value().pixels.size() !=
          static_cast<usize>(skin.width) * static_cast<usize>(skin.height) * 4U) {
    return std::nullopt;
  }
  return std::move(image.value());
}

inline constexpr std::string_view DefaultMenuPlayerSkinKey = "arvane:default_skin/default";

[[nodiscard]] ui::PlayerSkinTextureView player_skin_texture_view(
    const render::OpenGLRenderer& renderer,
    const render::ResidentSkinTexture& resident,
    const render::AtlasRegion& region) noexcept {
  ui::PlayerSkinTextureView view;
  view.valid = true;
  view.page = region.page_index;
  view.atlas_width = renderer.player_skin_atlas_page_width();
  view.atlas_height = renderer.player_skin_atlas_page_height();
  view.x = region.x;
  view.y = region.y;
  view.width = region.width;
  view.height = region.height;
  view.image_width = resident.width;
  view.image_height = resident.height;
  return view;
}

[[nodiscard]] render::RenderView menu_player_preview_view() noexcept {
  render::RenderView view;
  view.position = {0.0F, 1.04F, -4.15F};
  view.forward = normalize_or_zero(Vec3f{0.0F, -0.015F, 1.0F});
  view.right = normalize_or_zero(cross(Vec3f{0.0F, 1.0F, 0.0F}, view.forward));
  view.up = normalize_or_zero(cross(view.forward, view.right));
  view.near_plane = 0.05F;
  view.far_plane = 16.0F;
  view.fov_y_degrees = 30.0F;
  return view;
}

[[nodiscard]] render::SkyUniforms menu_player_preview_sky() noexcept {
  render::SkyUniforms sky;
  sky.daylight_color = {1.0F, 1.0F, 1.0F};
  sky.daylight_strength = 1.0F;
  sky.ambient_color = {1.0F, 1.0F, 1.0F};
  return sky;
}

class DesktopFrameLoop {
public:
  DesktopFrameLoop(
      const io::EngineConfig& config,
      asset::ResourceManager& resources,
      platform::OpenGLWindow& window,
      render::OpenGLRenderer& renderer,
      DebugImGuiLayer& debug_imgui,
      DebugMenuState& debug_menu,
      DesktopLoadedAssets& assets,
      sched::Scheduler& world_jobs,
      DesktopSessionManager& sessions,
      ui::UiTree& ui_tree,
      ui::FontUiTextMeasurer& ui_text_measurer,
      const ui::UiTheme& ui_theme,
      const voxel::FluidDefinition& water_fluid,
      DesktopMenuState& menu_state,
      ui::UIScreenManager& screen_manager)
      : config_(config),
        resources_(resources),
        window_(window),
        renderer_(renderer),
        debug_imgui_(debug_imgui),
        debug_menu_(debug_menu),
        assets_(assets),
        world_jobs_(world_jobs),
        sessions_(sessions),
        ui_tree_(ui_tree),
        ui_text_measurer_(ui_text_measurer),
        ui_theme_(ui_theme),
        water_fluid_(water_fluid),
        menu_state_(menu_state),
        screen_manager_(screen_manager),
        framebuffer_(window.framebuffer_size()) {
    const auto start = std::chrono::steady_clock::now();
    previous_time_ = start;
    app_start_time_ = start;
    last_fps_display_time_ = start;
  }

  [[nodiscard]] int run() {
    while (!window_.should_close()) {
      ARV_PROFILE_ZONE("Frame time");
      FrameTiming timing = begin_frame();
      platform::InputSnapshot platform_input = poll_platform_input();
      DesktopGameSession* active_session = sessions_.active_session();
      const bool has_session = active_session != nullptr && active_session->client != nullptr;
      handle_debug_menu_input(platform_input, active_session, has_session);
      begin_debug_frame(platform_input, timing);
      const bool was_inventory_open = has_session && active_session->client->inventory_open();

      ui::UIContext menu_context = make_menu_context();
      handle_pause_menu_input(platform_input, menu_context, active_session, has_session);

      client::InputFrame client_input =
          has_session ? to_client_input(platform_input) : client::InputFrame{};
      bool inventory_changed_by_ui = route_input(
          platform_input,
          menu_context,
          active_session,
          has_session,
          client_input);

      if (!process_menu_commands()) {
        return 1;
      }

      active_session = sessions_.active_session();
      const bool has_active_session =
          active_session != nullptr && active_session->client != nullptr;
      const bool has_active_integrated_session =
          has_active_session && active_session->integrated_server != nullptr;

      handle_chat_command_submit(active_session, has_active_session, client_input);

      submit_client_input_and_update_cursor(
          active_session,
          has_active_session,
          was_inventory_open,
          std::move(client_input),
          inventory_changed_by_ui,
          timing.now);

      bool fixed_tick_ran = false;
      if (has_active_session &&
          !update_simulation_and_chunks(*active_session, timing.delta, fixed_tick_ran)) {
        return 1;
      }

      update_framebuffer_size();
      if (has_active_session) {
        refresh_player_skin_render_assets(*active_session);
      }
      refresh_menu_profile_render_assets();

      render::FramePacket frame;
      usize visible_chunk_count = build_world_frame(
          frame,
          active_session,
          has_active_session,
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
          active_session,
          has_active_session,
          has_active_integrated_session,
          fixed_tick_ran);

      submit_render_and_present(
          std::move(frame),
          timing,
          has_active_session,
          active_session);
      ARV_PROFILE_FRAME_MARK("Frame");
    }

    return 0;
  }

private:
  [[nodiscard]] ui::UIContext make_menu_context() {
    return {
        ui_tree_,
        {
            static_cast<f32>(framebuffer_.width),
            static_cast<f32>(framebuffer_.height),
        },
        &menu_state_,
        &menu_state_,
        &screen_manager_,
        &ui_theme_};
  }

  [[nodiscard]] FrameTiming begin_frame() {
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

  void update_fps_sample(
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

  [[nodiscard]] platform::InputSnapshot poll_platform_input() {
    platform::OpenGLWindow::poll_events();
    return window_.poll_input();
  }

  void handle_debug_menu_input(
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

  void begin_debug_frame(
      const platform::InputSnapshot& platform_input,
      const FrameTiming& timing) {
    debug_imgui_.begin_frame(
        window_.native_handle(),
        platform_input,
        framebuffer_,
        timing.delta.count());
  }

  void handle_pause_menu_input(
      const platform::InputSnapshot& platform_input,
      ui::UIContext& menu_context,
      DesktopGameSession* active_session,
      bool has_session) {
    if (debug_menu_.open || !platform_input.pause_menu_pressed) {
      return;
    }
    if (screen_manager_.has_active_screen()) {
      (void)screen_manager_.handle_cancel(menu_context);
    } else if (has_session && !active_session->client->inventory_open() &&
               !active_session->client->chat_open()) {
      screen_manager_.push(ui::make_pause_menu_screen());
    } else if (!has_session) {
      screen_manager_.push(ui::make_main_menu_screen());
    }
  }

  [[nodiscard]] bool route_input(
      const platform::InputSnapshot& platform_input,
      ui::UIContext& menu_context,
      DesktopGameSession* active_session,
      bool has_session,
      client::InputFrame& client_input) {
    const bool menu_screen_open = screen_manager_.has_active_screen();
    if (debug_menu_.open || menu_screen_open) {
      client_input = {};
    }

    bool inventory_changed_by_ui = false;
    if (!debug_menu_.open && menu_screen_open) {
      (void)route_screen_mouse_input(screen_manager_, menu_context, platform_input);
      (void)screen_manager_.handle_pointer(
          {platform_input.cursor_x, platform_input.cursor_y},
          {platform_input.mouse_delta_x, platform_input.mouse_delta_y},
          platform_input.mouse_left_pressed,
          platform_input.mouse_left_released,
          platform_input.mouse_left_held,
          menu_context);
      if (platform_input.mouse_wheel_y != 0.0F) {
        (void)screen_manager_.handle_scroll(
            {platform_input.cursor_x, platform_input.cursor_y},
            platform_input.mouse_wheel_y,
            menu_context);
      }
      if (!platform_input.text_input.empty()) {
        (void)screen_manager_.handle_text_input(platform_input.text_input, menu_context);
      }
      if (platform_input.text_backspace_pressed) {
        (void)screen_manager_.handle_backspace(menu_context);
      }
      if (platform_input.text_submit_pressed) {
        (void)screen_manager_.handle_submit(menu_context);
      }
    } else if (!debug_menu_.open && has_session && active_session->client->inventory_open()) {
      inventory_changed_by_ui = route_inventory_mouse_input(
          ui_tree_,
          *active_session->client,
          platform_input,
          active_session->client->inventory_cursor_stack().has_value());
    } else if (!debug_menu_.open && has_session && active_session->client->chat_open()) {
      route_ui_mouse_focus(ui_tree_, platform_input);
    }
    return inventory_changed_by_ui;
  }

  [[nodiscard]] bool process_menu_commands() {
    if (menu_state_.take_back_to_main_menu_request()) {
      if (!sessions_.save_and_shutdown()) {
        return false;
      }
      menu_state_.refresh_worlds();
      invalidate_ui_tree_cache();
    }
    if (const std::optional<ui::DeleteSinglePlayerSaveCommand> command =
            menu_state_.take_delete_save_request();
        command.has_value()) {
      sessions_.delete_singleplayer_save(command.value());
      menu_state_.refresh_worlds();
      invalidate_ui_tree_cache();
    }
    if (const std::optional<ui::JoinServerCommand> command =
            menu_state_.take_join_server_request();
        command.has_value()) {
      if (sessions_.start_remote(command.value())) {
        screen_manager_.clear();
      } else if (sessions_.active_session() == nullptr && screen_manager_.empty()) {
        screen_manager_.push(ui::make_multi_player_menu_screen());
      }
      invalidate_ui_tree_cache();
    }
    if (const std::optional<ui::CreateWorldCommand> command =
            menu_state_.take_create_world_request();
        command.has_value()) {
      const std::string server_save_name =
          unique_server_save_name(config_, command->name);
      const u64 seed = parse_or_generate_seed(command->seed);
      if (sessions_.start_singleplayer(
              server_save_name,
              "default",
              seed,
              true,
              command->name)) {
        menu_state_.refresh_worlds();
        screen_manager_.clear();
      } else if (sessions_.active_session() == nullptr && screen_manager_.empty()) {
        screen_manager_.push(ui::make_single_player_menu_screen());
      }
      invalidate_ui_tree_cache();
    }
    if (const std::optional<ui::StartSinglePlayerCommand> command =
            menu_state_.take_start_singleplayer_request();
        command.has_value()) {
      if (sessions_.start_singleplayer(
              command->server_save_name,
              command->world_name,
              std::nullopt,
              false,
              {})) {
        screen_manager_.clear();
      } else if (sessions_.active_session() == nullptr && screen_manager_.empty()) {
        screen_manager_.push(ui::make_single_player_menu_screen());
      }
      invalidate_ui_tree_cache();
    }
    if (menu_state_.quit_requested) {
      window_.request_close();
    }
    return true;
  }

  void invalidate_ui_tree_cache() noexcept {
    ui_tree_cache_valid_ = false;
  }

  [[nodiscard]] const player::ItemRecord* selected_held_item(
      const DesktopGameSession& session) const noexcept {
    if (session.client == nullptr || !session.client->has_authoritative_inventory()) {
      return nullptr;
    }
    const usize selected_slot = session.client->selected_hotbar_slot();
    const player::Inventory& inventory = session.client->inventory();
    if (selected_slot >= inventory.slot_count()) {
      return nullptr;
    }
    const std::optional<player::ItemStack>& stack = inventory.slot(selected_slot);
    if (!stack.has_value() || stack->count == 0U) {
      return nullptr;
    }
    return assets_.item_registry.item(stack->item);
  }

  [[nodiscard]] bool selected_water_bucket_can_leave_empty_bucket(
      const DesktopGameSession& session) const noexcept {
    if (session.client == nullptr || !session.client->has_authoritative_inventory()) {
      return false;
    }
    const std::optional<player::ItemID> bucket_id =
        assets_.item_registry.find_item("arvane:bucket");
    if (!bucket_id.has_value()) {
      return false;
    }
    const player::ItemRecord* bucket = assets_.item_registry.item(bucket_id.value());
    if (bucket == nullptr) {
      return false;
    }

    const usize selected_slot = session.client->selected_hotbar_slot();
    const player::Inventory& inventory = session.client->inventory();
    if (selected_slot >= inventory.slot_count()) {
      return false;
    }
    const std::optional<player::ItemStack>& held = inventory.slot(selected_slot);
    if (!held.has_value() || held->count == 0U) {
      return false;
    }
    if (held->count == 1U) {
      return true;
    }

    for (usize slot_index = 0; slot_index < inventory.slot_count(); ++slot_index) {
      if (slot_index == selected_slot) {
        continue;
      }
      const std::optional<player::ItemStack>& slot = inventory.slot(slot_index);
      if (!slot.has_value()) {
        return true;
      }
      if (slot->item == bucket_id.value() && slot->count < bucket->max_stack_size) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] std::optional<BlockPos> predicted_secondary_place_pos(
      const DesktopGameSession& session,
      const physics::ICollisionQuery& query) const {
    if (session.client == nullptr) {
      return std::nullopt;
    }
    auto hit = physics::raycast_blocks(
        query,
        session.client->camera().position(),
        session.client->camera().forward_vector(),
        config_.physics.reach_distance);
    if (!hit.has_value()) {
      return std::nullopt;
    }

    BlockPos place_pos = hit->block;
    if (!assets_.block_registry.is_replaceable(hit->variant)) {
      place_pos = offset_block(place_pos, hit->face);
    }
    if (!query.is_block_loaded(place_pos)) {
      return std::nullopt;
    }
    return place_pos;
  }

  [[nodiscard]] bool predict_block_placement_succeeds(
      const DesktopGameSession& session,
      const player::ItemRecord& item,
      const physics::ICollisionQuery& query) const {
    if (session.client == nullptr || !item.placement_variant.has_value()) {
      return false;
    }
    const std::optional<BlockPos> place_pos = predicted_secondary_place_pos(session, query);
    if (!place_pos.has_value()) {
      return false;
    }

    const voxel::BlockVariantID existing = session.client_world.get_block(place_pos.value());
    if (!(assets_.block_registry.is_air(existing) ||
          assets_.block_registry.is_replaceable(existing))) {
      return false;
    }

    const physics::KinematicConfig physics_config =
        make_prediction_kinematic_config(config_.physics);
    const Aabb3f player_bounds = physics::make_body_bounds(
        session.client->player_position(),
        physics_config,
        physics::PlayerCollisionPose::Standing);
    for (const Aabb3f& local_box : assets_.block_registry.collision_boxes(
             item.placement_variant.value())) {
      Aabb3f world_box{
          {local_box.min.x + static_cast<f32>(place_pos->value.x),
           local_box.min.y + static_cast<f32>(place_pos->value.y),
           local_box.min.z + static_cast<f32>(place_pos->value.z)},
          {local_box.max.x + static_cast<f32>(place_pos->value.x),
           local_box.max.y + static_cast<f32>(place_pos->value.y),
           local_box.max.z + static_cast<f32>(place_pos->value.z)}};
      if (player_bounds.overlaps(world_box)) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] bool predict_water_bucket_placement_succeeds(
      const DesktopGameSession& session,
      const physics::ICollisionQuery& query) const {
    if (!selected_water_bucket_can_leave_empty_bucket(session)) {
      return false;
    }
    const std::optional<BlockPos> place_pos = predicted_secondary_place_pos(session, query);
    if (!place_pos.has_value()) {
      return false;
    }

    const voxel::BlockVariantID existing_block =
        session.client_world.get_block(place_pos.value());
    if (!(assets_.block_registry.is_air(existing_block) ||
          assets_.block_registry.is_replaceable(existing_block))) {
      return false;
    }

    const voxel::FluidCell existing_fluid = session.client_world.get_fluid(place_pos.value());
    return existing_fluid.empty() ||
           (existing_fluid.type == water_fluid_.type && !existing_fluid.source());
  }

  [[nodiscard]] bool predict_held_item_placement_succeeds(
      const DesktopGameSession& session,
      const player::ItemRecord* item) const {
    if (session.client == nullptr || item == nullptr ||
        !session.client->resolved_permissions().core.can_modify_world) {
      return false;
    }

    world::WorldCollisionQuery query(session.client_world, assets_.block_registry, false);
    if (item->placement_variant.has_value()) {
      return predict_block_placement_succeeds(session, *item, query);
    }
    if (item->name == "arvane:water_bucket") {
      return predict_water_bucket_placement_succeeds(session, query);
    }
    return false;
  }

  [[nodiscard]] bool held_item_animation_condition_passes(
      const DesktopGameSession& session,
      const player::ItemRecord* item,
      player::HeldItemAnimationPlayCondition condition) const {
    switch (condition) {
      case player::HeldItemAnimationPlayCondition::Always:
        return true;
      case player::HeldItemAnimationPlayCondition::PlacementSucceeds:
        return predict_held_item_placement_succeeds(session, item);
    }
    return false;
  }

  [[nodiscard]] HeldItemActionPlayResult play_held_item_action(
      DesktopGameSession& session,
      const player::ItemRecord* item,
      std::string_view action,
      Seconds now_seconds) const noexcept {
    const client::HeldItemAnimationResolution animation =
        client::resolve_held_item_animation_binding(
            item,
            action,
            assets_.item_animation_library);
    if (animation.clip == nullptr) {
      return animation.disabled
                 ? HeldItemActionPlayResult::Suppressed
                 : HeldItemActionPlayResult::NoAnimation;
    }
    if (!held_item_animation_condition_passes(session, item, animation.play_condition)) {
      return HeldItemActionPlayResult::Suppressed;
    }
    session.held_item_animation.play(*animation.clip, now_seconds);
    return HeldItemActionPlayResult::Played;
  }

  void update_held_item_animation_from_input(
      DesktopGameSession& session,
      const client::InputFrame& client_input,
      Seconds now_seconds) const noexcept {
    if (session.client == nullptr ||
        session.client->inventory_open() ||
        session.client->chat_open()) {
      session.held_item_secondary_use_active = false;
      return;
    }

    const player::ItemRecord* held_item = selected_held_item(session);
    if (held_item == nullptr) {
      session.held_item_secondary_use_active = false;
      session.held_item_animation.stop(now_seconds);
      return;
    }

    if (client_input.primary_pressed) {
      (void)play_held_item_action(
          session,
          held_item,
          client::HeldItemActionPrimaryUse,
          now_seconds);
    }

    if (client_input.primary_held &&
        session.client->block_break_progress().active &&
        session.held_item_animation.finished(now_seconds)) {
      (void)play_held_item_action(
          session,
          held_item,
          client::HeldItemActionPrimaryUse,
          now_seconds);
    }

    if (client_input.secondary_pressed) {
      HeldItemActionPlayResult result = play_held_item_action(
          session,
          held_item,
          client::HeldItemActionSecondaryUseStart,
          now_seconds);
      if (result == HeldItemActionPlayResult::NoAnimation) {
        result = play_held_item_action(
            session,
            held_item,
            client::HeldItemActionSecondaryUseHold,
            now_seconds);
      }
      session.held_item_secondary_use_active =
          result != HeldItemActionPlayResult::Suppressed;
    }

    if (client_input.secondary_held && session.held_item_secondary_use_active &&
        session.held_item_animation.finished(now_seconds)) {
      const client::HeldItemAnimationResolution hold_animation =
          client::resolve_held_item_animation_binding(
              held_item,
              client::HeldItemActionSecondaryUseHold,
              assets_.item_animation_library);
      if (hold_animation.clip != nullptr &&
          held_item_animation_condition_passes(session, held_item, hold_animation.play_condition) &&
          session.held_item_animation.active_clip() != hold_animation.clip) {
        session.held_item_animation.play(*hold_animation.clip, now_seconds);
      }
    }

    if (!client_input.secondary_held && session.held_item_secondary_use_active) {
      session.held_item_secondary_use_active = false;
      if (play_held_item_action(
              session,
              held_item,
              client::HeldItemActionSecondaryUseEnd,
              now_seconds) != HeldItemActionPlayResult::Played) {
        session.held_item_animation.stop(now_seconds);
      }
    }
  }

  [[nodiscard]] command::CommandResult execute_chat_command(
      DesktopGameSession& session,
      std::string_view command_text) {
    if (session.integrated_server == nullptr) {
      return command::CommandResult::invalid_source(
          "Server command networking is not available for remote sessions yet.");
    }

    const server::ServerSession* server_session =
        session.integrated_server->sessions().first();
    if (server_session == nullptr) {
      return command::CommandResult::invalid_source(
          "No player session is available for command execution.");
    }

    std::optional<command::CommandSender> sender =
        session.integrated_server->command_sender_for_session(server_session->session_id);
    if (!sender.has_value()) {
      return command::CommandResult::invalid_source(
          "No player command sender is available.");
    }

    return session.integrated_server->execute_command(sender.value(), command_text);
  }

  void handle_chat_command_submit(
      DesktopGameSession* active_session,
      bool has_active_session,
      client::InputFrame& client_input) {
    if (!has_active_session ||
        active_session == nullptr ||
        active_session->client == nullptr ||
        !active_session->client->chat_open() ||
        !client_input.text_submit_pressed) {
      return;
    }

    const std::string command_text = active_session->client->chat_draft();
    if (!is_command_chat_draft(command_text)) {
      return;
    }

    client_input.text_submit_pressed = false;
    active_session->client->close_chat();
    append_command_result_to_chat(
        *active_session->client,
        execute_chat_command(*active_session, command_text));
  }

  void submit_client_input_and_update_cursor(
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
          debug_menu_.open || screen_manager_.blocks_gameplay_input()));
  }

  [[nodiscard]] bool update_simulation_and_chunks(
      DesktopGameSession& active_session,
      std::chrono::duration<Seconds> delta,
      bool& fixed_tick_ran) {
    active_session.ticks.add_time(delta.count());
    const bool has_active_integrated_session = active_session.integrated_server != nullptr;
    const bool singleplayer_paused =
        has_active_integrated_session && screen_manager_.pauses_singleplayer();
    while (active_session.ticks.consume_tick()) {
      ARV_PROFILE_ZONE("Simulation tick");
      if (!singleplayer_paused) {
        std::chrono::steady_clock::time_point simulation_start{};
        if constexpr (core::profiling_enabled()) {
          simulation_start = std::chrono::steady_clock::now();
        }
        if (has_active_integrated_session) {
          active_session.integrated_server->tick();
          active_session.client->fixed_tick(active_session.integrated_server->tick_index());
        } else {
          ++active_session.client_tick_index;
          active_session.client->fixed_tick(active_session.client_tick_index);
        }
        update_block_outline_target(active_session);
        sessions_.collect_client_chunk_stream(active_session);
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
    }

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
        assets_.block_atlas.render_data);
  }

  void update_framebuffer_size() {
    const auto next_framebuffer = window_.framebuffer_size();
    if (next_framebuffer.width != framebuffer_.width ||
        next_framebuffer.height != framebuffer_.height) {
      framebuffer_ = next_framebuffer;
      renderer_.resize(framebuffer_.width, framebuffer_.height);
    }
  }

  void update_block_outline_target(DesktopGameSession& active_session) {
    active_session.block_outline_target.reset();
    world::WorldCollisionQuery query(
        active_session.client_world,
        assets_.block_registry,
        false);
    const client::CameraShell& camera = active_session.client->camera();
    const std::optional<physics::BlockRaycastHit> hit = physics::raycast_blocks(
        query,
        camera.position(),
        camera.forward_vector(),
        config_.physics.reach_distance);
    if (!hit.has_value() || assets_.block_registry.is_air(hit->variant)) {
      return;
    }
    active_session.block_outline_target = BlockOutlineTarget{hit->block, hit->variant};
  }

  [[nodiscard]] bool install_entity_render_assets(EntityRenderAssets assets) {
    if (!assets.models.empty() && assets.texture_atlas.pages.empty()) {
      core::warn(
          "entities",
          "entity models were rebuilt without an entity texture atlas");
      return false;
    }

    renderer_.clear_entity_models();
    if (!assets.models.empty()) {
      render::TextureArrayUploadData entity_atlas_upload =
          texture_array_upload_from_atlas(assets.texture_atlas);
      renderer_.set_entity_texture_atlas(std::move(entity_atlas_upload));
      for (const render::EntityModelAsset& model : assets.models) {
        renderer_.set_entity_model(model);
      }
    }
    assets_.entity_render_assets = std::move(assets);
    return true;
  }

  void refresh_player_skin_render_assets(DesktopGameSession& active_session) {
    if (active_session.client == nullptr) {
      return;
    }

    HashMap<std::string, asset::Image> new_images;
    const client::LocalPlayerIdentity& identity =
        active_session.client->local_identity();
    for (const protocol::PlayerProfileInfo& profile :
         active_session.client->player_profile_infos()) {
      const std::string& skin_hash = profile.skin.skin_hash;
      if (skin_hash.empty() ||
          player_skin_regions_.find(skin_hash) != player_skin_regions_.end() ||
          new_images.find(skin_hash) != new_images.end()) {
        continue;
      }
      std::optional<asset::Image> image =
          load_valid_player_skin_render_image(identity, profile.skin);
      if (image.has_value()) {
        new_images[skin_hash] = std::move(image.value());
      }
    }
    if (new_images.empty()) {
      return;
    }

    for (const auto& [skin_hash, image] : new_images) {
      std::optional<render::TextureRegionHandle> handle =
          renderer_.upload_player_skin_texture(skin_hash, texture_upload_from_image(image));
      if (handle.has_value()) {
        player_skin_regions_[skin_hash] = handle.value();
      }
    }
  }

  void refresh_menu_profile_render_assets() {
    HashMap<std::string, asset::Image> new_images;

    if (player_skin_regions_.find(std::string{DefaultMenuPlayerSkinKey}) ==
        player_skin_regions_.end()) {
      auto texture = resources_.load_texture({"arvane:texture/default_skin/default"});
      if (texture) {
        const asset::TextureResource* resource = resources_.texture(texture.value());
        if (resource != nullptr && !resource->image.empty()) {
          new_images[std::string{DefaultMenuPlayerSkinKey}] = resource->image;
        }
      }
    }

    const client::LocalProfileStore& store = menu_state_.local_profile_store();
    const auto queue_skin = [&](const client::LocalProfileSkin& skin) {
      if (skin.skin_hash.empty() ||
          player_skin_regions_.find(skin.skin_hash) != player_skin_regions_.end() ||
          new_images.find(skin.skin_hash) != new_images.end()) {
        return;
      }
      std::optional<asset::Image> image =
          load_valid_local_profile_skin_render_image(
              config_.paths.arvane_home_path(),
              skin);
      if (image.has_value()) {
        new_images[skin.skin_hash] = std::move(image.value());
      }
    };
    for (const client::LocalPlayerIdentity& profile : store.profiles) {
      for (const client::LocalProfileSkin& skin : profile.skins) {
        queue_skin(skin);
      }
    }
    for (const client::LocalProfileSkin& skin : menu_state_.pending_create_skins()) {
      queue_skin(skin);
    }
    for (const auto& [profile_id, skins] : menu_state_.pending_edit_skins()) {
      (void)profile_id;
      for (const client::LocalProfileSkin& skin : skins) {
        queue_skin(skin);
      }
    }

    for (const auto& [skin_hash, image] : new_images) {
      std::optional<render::TextureRegionHandle> handle =
          renderer_.upload_player_skin_texture(skin_hash, texture_upload_from_image(image));
      if (handle.has_value()) {
        player_skin_regions_[skin_hash] = handle.value();
      }
    }
    if (!new_images.empty()) {
      invalidate_ui_tree_cache();
    }

    HashMap<std::string, ui::PlayerSkinTextureView> texture_views;
    ui::PlayerSkinTextureView default_texture;
    for (const auto& [skin_hash, handle] : player_skin_regions_) {
      const render::ResidentSkinTexture* resident =
          renderer_.resident_player_skin_texture(skin_hash);
      const std::optional<render::AtlasRegion> region =
          renderer_.resolve_player_skin_texture(handle);
      if (resident == nullptr || !region.has_value()) {
        continue;
      }
      (void)renderer_.mark_player_skin_texture_used(handle, true);
      ui::PlayerSkinTextureView view =
          player_skin_texture_view(renderer_, *resident, region.value());
      if (skin_hash == DefaultMenuPlayerSkinKey) {
        default_texture = view;
      } else {
        texture_views[skin_hash] = view;
      }
    }
    menu_state_.set_player_skin_texture_views(std::move(texture_views), default_texture);
  }

  [[nodiscard]] usize build_world_frame(
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
        populate_chunk_draw_lists(frame, active_session->chunk_view);
    populate_world_frame_draw_items(
        frame,
        *active_session,
        assets_.entity_render_assets,
        player_skin_regions_,
        config_.render.show_local_player_body,
        now,
        app_start_time_,
        interpolation_alpha);
    return visible_chunk_count;
  }

  void render_debug_menu_if_available(
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
        config_.paths.config_path() / "item_animations.json",
        assets_.entity_registry,
        world_jobs_,
        renderer_,
        active_session->chunk_view);
    if (debug_menu_result.inventory_changed) {
      sessions_.refresh_inventory_slots(*active_session);
    }
  }

  void append_menu_player_skin_preview(
      render::FramePacket& frame,
      const ui::UIContext& menu_context) {
    const ui::UIScreen* screen = screen_manager_.current();
    if (screen == nullptr ||
        assets_.entity_render_assets.player_skin_model.value == 0U) {
      return;
    }
    std::optional<ui::PlayerSkinPreviewRequest> request =
        screen->player_skin_preview(menu_context);
    if (!request.has_value()) {
      return;
    }
    const ui::UiElement* preview_element =
        ui_tree_.find_by_stable_id(request->element_id);
    if (preview_element == nullptr ||
        preview_element->rect.width <= 0.0F ||
        preview_element->rect.height <= 0.0F) {
      return;
    }

    std::string skin_hash = request->skin_hash;
    if (skin_hash.empty()) {
      skin_hash = std::string{DefaultMenuPlayerSkinKey};
    }
    auto skin_region = player_skin_regions_.find(skin_hash);
    if (skin_region == player_skin_regions_.end() || !skin_region->second.valid()) {
      skin_region = player_skin_regions_.find(std::string{DefaultMenuPlayerSkinKey});
    }
    if (skin_region == player_skin_regions_.end() || !skin_region->second.valid()) {
      return;
    }

    render::EntityPreviewDrawItem preview;
    preview.viewport_min = {preview_element->rect.x, preview_element->rect.y};
    preview.viewport_size = {preview_element->rect.width, preview_element->rect.height};
    preview.view = menu_player_preview_view();
    preview.sky = menu_player_preview_sky();
    preview.clear_color = {0.018F, 0.023F, 0.030F, 1.0F};

    render::EntityDrawItem player;
    player.model_id = assets_.entity_render_assets.player_skin_model;
    player.position = {0.0F, -0.02F, 0.0F};
    player.rotation = model_forward_yaw_rotation(request->yaw_radians + PiRadians);
    player.scale = 1.0F;
    player.player_skin_region = skin_region->second;
    preview.entities.push_back(std::move(player));
    frame.entity_previews.push_back(std::move(preview));
  }

  void build_ui(
      render::FramePacket& frame,
      const platform::InputSnapshot& platform_input,
      ui::UIContext& menu_context,
      DesktopGameSession* active_session,
      bool has_active_session,
      bool has_active_integrated_session,
      bool fixed_tick_ran) {
    ui_tree_.set_theme(&ui_theme_);
    if (has_active_session) {
      update_gameplay_ui_snapshots(
          *active_session,
          has_active_integrated_session,
          fixed_tick_ran);
    }

    menu_context.viewport = {
        static_cast<f32>(framebuffer_.width),
        static_cast<f32>(framebuffer_.height),
    };
    menu_context.screens = &screen_manager_;

    const UiBuildCacheKey next_cache_key{
        framebuffer_.width,
        framebuffer_.height,
        has_active_session,
        screen_manager_.has_active_screen(),
        screen_manager_.revision(),
        has_active_session
            ? gameplay_ui_signature(*active_session, platform_input, displayed_fps_)
            : 0U};
    const bool rebuild_ui_tree =
        !ui_tree_cache_valid_ || !(next_cache_key == ui_tree_cache_key_);
    if (rebuild_ui_tree) {
      if (has_active_session) {
        build_gameplay_ui(*active_session, platform_input);
      } else {
        ui_tree_.clear();
      }

      if (screen_manager_.has_active_screen()) {
        screen_manager_.append_to(menu_context);
      }
      ui_tree_.layout(
          {
              static_cast<f32>(framebuffer_.width),
              static_cast<f32>(framebuffer_.height),
          },
          &ui_text_measurer_);
      ui_tree_cache_key_ = next_cache_key;
      ui_tree_cache_valid_ = true;
    }
    (void)ui_tree_.route_mouse_move({platform_input.cursor_x, platform_input.cursor_y});
    append_menu_player_skin_preview(frame, menu_context);
    ui::append_ui_draw_commands(
        ui_tree_,
        assets_.font_system,
        assets_.ui_font_stacks,
        frame);
    if (assets_.font_system.atlas_dirty()) {
      renderer_.set_ui_font_atlas(
          texture_array_upload_from_image(assets_.font_system.atlas_image()));
      assets_.font_system.clear_atlas_dirty();
    }
  }

  void update_gameplay_ui_snapshots(
      DesktopGameSession& active_session,
      bool has_active_integrated_session,
      bool fixed_tick_ran) {
    if (!active_session.ui_snapshot_initialized || fixed_tick_ran) {
      sessions_.refresh_inventory_slots(active_session);
      if (has_active_integrated_session && active_session.client->tab_menu_visible()) {
        active_session.tab_players =
            player_list_for_ui(*active_session.integrated_server);
      } else {
        active_session.tab_players.clear();
      }
      active_session.ui_snapshot_initialized = true;
    } else if (has_active_integrated_session &&
               active_session.client->tab_menu_visible() &&
               active_session.tab_players.empty()) {
      active_session.tab_players =
          player_list_for_ui(*active_session.integrated_server);
    } else if (!active_session.client->tab_menu_visible() &&
               !active_session.tab_players.empty()) {
      active_session.tab_players.clear();
    }

    active_session.chat_lines.clear();
    for (const std::string& message : active_session.client->chat_messages()) {
      active_session.chat_lines.emplace_back(message, 1.0F);
    }
  }

  void build_gameplay_ui(
      DesktopGameSession& active_session,
      const platform::InputSnapshot& platform_input) {
    ui::build_gameplay_ui(
        ui_tree_,
        {
            {
                active_session.client->selected_hotbar_slot(),
                player::DefaultHotbarSlotCount,
                {
                    static_cast<f32>(framebuffer_.width),
                    static_cast<f32>(framebuffer_.height),
                },
                std::span<const ui::UiItemStackView>{
                    active_session.inventory_slots.data(),
                    std::min<usize>(
                        active_session.inventory_slots.size(),
                        player::DefaultHotbarSlotCount)},
            },
            {
                active_session.client->inventory_open(),
                "Inventory",
                active_session.client->selected_hotbar_slot(),
                player::DefaultHotbarSlotCount,
                {
                    static_cast<f32>(framebuffer_.width),
                    static_cast<f32>(framebuffer_.height),
                },
                std::span<const ui::UiItemStackView>{
                    active_session.inventory_slots.data(),
                    active_session.inventory_slots.size()},
                active_session.carried_inventory_stack,
                {
                    platform_input.cursor_x,
                    platform_input.cursor_y,
                },
            },
            {
                true,
                active_session.client->chat_open(),
                std::span<const ui::ChatLineView>{
                    active_session.chat_lines.data(),
                    active_session.chat_lines.size()},
                active_session.client->chat_draft(),
            },
            {
                active_session.client->tab_menu_visible(),
                "Players",
                "",
                std::span<const ui::PlayerListEntryView>{
                    active_session.tab_players.data(),
                    active_session.tab_players.size()},
            },
            {},
            {true, displayed_fps_},
        });
  }

  void submit_render_and_present(
      render::FramePacket frame,
      const FrameTiming& timing,
      bool has_active_session,
      DesktopGameSession* active_session) {
    std::chrono::steady_clock::time_point render_submit_start{};
    if constexpr (core::profiling_enabled()) {
      render_submit_start = std::chrono::steady_clock::now();
    }
    renderer_.submit_frame(std::move(frame));
    renderer_.render();
    if constexpr (core::profiling_enabled()) {
      const auto render_submit_end = std::chrono::steady_clock::now();
      core::record_thread_timing(
          core::PerfMetric::RenderSubmit,
          std::chrono::duration<double, std::milli>(
              render_submit_end - render_submit_start)
              .count(),
          "main/render");
    }
    debug_imgui_.render();
    record_frame_work_profile(timing, has_active_session, active_session);
    swap_buffers();
    pace_frame(config_.render, timing.now);
  }

  void record_frame_work_profile(
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
      if (frame_work_ms > static_cast<double>(config_.chunk_pipeline.frame_spike_log_ms)) {
        core::warn(
            "frame_profile",
            "cpu frame work spike {}ms frame_delta={}ms queued_mesh={} pending_uploads={} uploads_last_frame={}",
            frame_work_ms,
            timing.frame_delta_ms,
            has_active_session ? active_session->chunk_view.pending_meshes.size() : 0U,
            renderer_.pending_mesh_upload_count(),
            renderer_.mesh_uploads_last_frame());
      }
    } else {
      (void)timing;
      (void)has_active_session;
      (void)active_session;
      last_frame_work_ms_ = 0.0;
    }
  }

  void swap_buffers() {
    ARV_PROFILE_ZONE("GPU wait time");
    std::chrono::steady_clock::time_point gpu_wait_start{};
    if constexpr (core::profiling_enabled()) {
      gpu_wait_start = std::chrono::steady_clock::now();
    }
    window_.swap_buffers();
    if constexpr (core::profiling_enabled()) {
      const auto gpu_wait_end = std::chrono::steady_clock::now();
      core::record_thread_timing(
          core::PerfMetric::GpuWaitTime,
          std::chrono::duration<double, std::milli>(
              gpu_wait_end - gpu_wait_start)
              .count(),
          "main/render");
    }
  }

  const io::EngineConfig& config_;
  asset::ResourceManager& resources_;
  platform::OpenGLWindow& window_;
  render::OpenGLRenderer& renderer_;
  DebugImGuiLayer& debug_imgui_;
  DebugMenuState& debug_menu_;
  DesktopLoadedAssets& assets_;
  sched::Scheduler& world_jobs_;
  DesktopSessionManager& sessions_;
  ui::UiTree& ui_tree_;
  ui::FontUiTextMeasurer& ui_text_measurer_;
  const ui::UiTheme& ui_theme_;
  const voxel::FluidDefinition& water_fluid_;
  DesktopMenuState& menu_state_;
  ui::UIScreenManager& screen_manager_;
  platform::FramebufferSize framebuffer_{};
  FrameArena frame_arena_{};
  std::chrono::steady_clock::time_point previous_time_{};
  std::chrono::steady_clock::time_point app_start_time_{};
  std::chrono::steady_clock::time_point last_fps_display_time_{};
  u32 displayed_fps_{};
  u32 fps_sample_frames_{};
  Seconds fps_sample_seconds_{};
  double last_frame_work_ms_{};
  u64 profile_frame_index_{};
  HashMap<std::string, render::TextureRegionHandle> player_skin_regions_{};
  UiBuildCacheKey ui_tree_cache_key_{};
  bool ui_tree_cache_valid_{};

  static constexpr auto FpsHudRefreshInterval = std::chrono::milliseconds(250);
};

} // namespace

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
      }};

  DesktopMenuState menu_state{
      config.paths.saves_path(),
      config.paths.server_list_file,
      config.paths.arvane_home_path()};
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
