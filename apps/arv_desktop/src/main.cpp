#include "app_config.hpp"
#include "asset_helpers.hpp"
#include "chunk_view.hpp"
#include "debug_imgui.hpp"
#include "debug_menu.hpp"
#include "input_adapter.hpp"
#include "renderer_bootstrap.hpp"
#include "save_state.hpp"
#include "ui_model.hpp"

#include "arv/asset/font.hpp"
#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/core/arena.hpp"
#include "arv/core/fixed_timestep.hpp"
#include "arv/core/log.hpp"
#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"
#include "arv/entity/components.hpp"
#include "arv/io/config.hpp"
#include "arv/io/input_bindings.hpp"
#include "arv/net/gns_transport.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/persistence/save.hpp"
#include "arv/platform/glfw_window.hpp"
#include "arv/player/item.hpp"
#include "arv/render/opengl_renderer.hpp"
#include "arv/sched/scheduler.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/ui/ui.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/world/world_runtime.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

[[nodiscard]] const arv::server::ServerSession* local_session(
    const arv::server::ServerRuntime& server) noexcept {
  return server.sessions().first();
}

[[nodiscard]] arv::player::PlayerState* local_player_state(
    arv::server::ServerRuntime& server) noexcept {
  const arv::server::ServerSession* session = local_session(server);
  return session == nullptr ? nullptr : server.voxel_world().find_player(session->player_id);
}

[[nodiscard]] const arv::player::PlayerState* local_player_state(
    const arv::server::ServerRuntime& server) noexcept {
  const arv::server::ServerSession* session = local_session(server);
  return session == nullptr ? nullptr : server.voxel_world().find_player(session->player_id);
}

[[nodiscard]] arv::Vec3f interpolate(
    arv::Vec3f from,
    arv::Vec3f to,
    arv::f32 alpha) noexcept {
  return {
      from.x + (to.x - from.x) * alpha,
      from.y + (to.y - from.y) * alpha,
      from.z + (to.z - from.z) * alpha};
}

[[nodiscard]] arv::player::PlayerTuning make_player_tuning(
    const arv::io::PhysicsConfig& physics) noexcept {
  arv::player::PlayerTuning tuning = arv::player::default_player_tuning();
  tuning.standing_eye_height = physics.standing_eye_height;
  tuning.sneaking_eye_height = physics.sneaking_eye_height;
  tuning.swimming_eye_height = physics.swimming_eye_height;
  tuning.eye_height_lerp_per_second = physics.eye_height_lerp_per_second;
  tuning.reach_distance = physics.reach_distance;
  tuning.mouse_sensitivity_radians = physics.mouse_sensitivity_radians;
  tuning.walk_speed = physics.walk_speed;
  tuning.sprint_speed = physics.sprint_speed;
  tuning.sneak_speed = physics.sneak_speed;
  tuning.air_speed = physics.air_speed;
  tuning.fly_speed = physics.fly_speed;
  tuning.fly_vertical_speed = physics.fly_vertical_speed;
  tuning.jump_velocity = physics.jump_velocity;
  tuning.gravity_per_second = physics.gravity_per_second;
  tuning.terminal_velocity = physics.terminal_velocity;
  tuning.body_width = physics.body_width;
  tuning.body_height = physics.body_height;
  tuning.sneaking_body_height = physics.sneaking_body_height;
  tuning.swimming_body_width = physics.swimming_body_width;
  tuning.swimming_body_height = physics.swimming_body_height;
  tuning.sneak_support_probe_depth = physics.sneak_support_probe_depth;
  tuning.collision_epsilon = physics.collision_epsilon;
  tuning.surface_swim_speed = physics.surface_swim_speed;
  tuning.underwater_swim_speed = physics.underwater_swim_speed;
  tuning.fast_surface_swim_speed = physics.fast_surface_swim_speed;
  tuning.fast_underwater_swim_speed = physics.fast_underwater_swim_speed;
  tuning.upward_swim_acceleration = physics.upward_swim_acceleration;
  tuning.downward_swim_acceleration = physics.downward_swim_acceleration;
  tuning.liquid_drag = physics.liquid_drag;
  tuning.surface_float_depth = physics.surface_float_depth;
  tuning.surface_buoyancy = physics.surface_buoyancy;
  tuning.underwater_buoyancy = physics.underwater_buoyancy;
  tuning.breath_seconds = physics.breath_seconds;
  return tuning;
}

[[nodiscard]] std::optional<arv::usize> inventory_slot_from_ui_action(
    const arv::ui::UiAction& action) noexcept {
  if (action.action_id != "inventory_slot") {
    return std::nullopt;
  }

  constexpr std::array<std::string_view, 2> prefixes{{
      "hud.hotbar.slot.",
      "inventory.slot.",
  }};
  for (std::string_view prefix : prefixes) {
    const std::string_view id{action.element_id};
    if (!id.starts_with(prefix)) {
      continue;
    }

    arv::usize value = 0;
    const std::string_view suffix = id.substr(prefix.size());
    if (suffix.empty()) {
      return std::nullopt;
    }
    for (const char ch : suffix) {
      if (ch < '0' || ch > '9') {
        return std::nullopt;
      }
      value = value * 10U + static_cast<arv::usize>(ch - '0');
    }
    return value;
  }
  return std::nullopt;
}

[[nodiscard]] bool apply_inventory_action(
    arv::client::ClientRuntime& client,
    const std::optional<arv::ui::UiAction>& action,
    arv::player::InventoryClickButton button) {
  if (!action.has_value()) {
    return false;
  }
  if (const std::optional<arv::usize> slot = inventory_slot_from_ui_action(action.value());
      slot.has_value()) {
    (void)client.request_inventory_slot_click(slot.value(), button);
    return true;
  }
  return false;
}

[[nodiscard]] bool route_inventory_mouse_input(
    arv::ui::UiTree& ui_tree,
    arv::client::ClientRuntime& client,
    const arv::platform::InputSnapshot& platform_input,
    bool has_carried_stack) {
  bool changed = false;
  const arv::Vec2f cursor{platform_input.cursor_x, platform_input.cursor_y};
  (void)ui_tree.route_mouse_move(cursor);
  if (platform_input.mouse_left_pressed) {
    const std::optional<arv::ui::UiNodeID> hit = ui_tree.hit_test(cursor);
    changed = apply_inventory_action(
                  client,
                  ui_tree.route_mouse_down(cursor, arv::ui::UiMouseButton::Left),
                  arv::player::InventoryClickButton::Primary) ||
              changed;
    if (!changed && !hit.has_value() && has_carried_stack) {
      (void)client.request_inventory_drop_cursor(false);
      changed = true;
    }
  }
  if (platform_input.mouse_right_pressed) {
    const std::optional<arv::ui::UiNodeID> hit = ui_tree.hit_test(cursor);
    changed = apply_inventory_action(
                  client,
                  ui_tree.route_mouse_down(cursor, arv::ui::UiMouseButton::Right),
                  arv::player::InventoryClickButton::Secondary) ||
              changed;
    if (!changed && !hit.has_value() && has_carried_stack) {
      (void)client.request_inventory_drop_cursor(true);
      changed = true;
    }
  }
  if (platform_input.mouse_left_released) {
    (void)ui_tree.route_mouse_up(cursor, arv::ui::UiMouseButton::Left);
  }
  if (platform_input.mouse_right_released) {
    (void)ui_tree.route_mouse_up(cursor, arv::ui::UiMouseButton::Right);
  }
  return changed;
}

[[nodiscard]] bool route_screen_mouse_input(
    arv::ui::UIScreenManager& screen_manager,
    arv::ui::UIContext& ui_context,
    const arv::platform::InputSnapshot& platform_input) {
  bool handled = false;
  const arv::Vec2f cursor{platform_input.cursor_x, platform_input.cursor_y};
  (void)ui_context.tree.route_mouse_move(cursor);
  if (platform_input.mouse_left_pressed) {
    const std::optional<arv::ui::UiAction> action =
        ui_context.tree.route_mouse_down(cursor, arv::ui::UiMouseButton::Left);
    if (action.has_value()) {
      handled = screen_manager.route_action(action.value(), ui_context) || handled;
    }
  }
  if (platform_input.mouse_left_released) {
    (void)ui_context.tree.route_mouse_up(cursor, arv::ui::UiMouseButton::Left);
  }
  if (platform_input.mouse_right_pressed) {
    (void)ui_context.tree.route_mouse_down(cursor, arv::ui::UiMouseButton::Right);
  }
  if (platform_input.mouse_right_released) {
    (void)ui_context.tree.route_mouse_up(cursor, arv::ui::UiMouseButton::Right);
  }
  return handled;
}

void route_ui_mouse_focus(
    arv::ui::UiTree& ui_tree,
    const arv::platform::InputSnapshot& platform_input) {
  const arv::Vec2f cursor{platform_input.cursor_x, platform_input.cursor_y};
  (void)ui_tree.route_mouse_move(cursor);
  if (platform_input.mouse_left_pressed) {
    (void)ui_tree.route_mouse_down(cursor, arv::ui::UiMouseButton::Left);
  }
  if (platform_input.mouse_right_pressed) {
    (void)ui_tree.route_mouse_down(cursor, arv::ui::UiMouseButton::Right);
  }
  if (platform_input.mouse_left_released) {
    (void)ui_tree.route_mouse_up(cursor, arv::ui::UiMouseButton::Left);
  }
  if (platform_input.mouse_right_released) {
    (void)ui_tree.route_mouse_up(cursor, arv::ui::UiMouseButton::Right);
  }
}

[[nodiscard]] char lowercase_ascii(char ch) noexcept {
  return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

[[nodiscard]] bool is_save_name_character(char ch) noexcept {
  return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
}

[[nodiscard]] std::string sanitize_save_name(std::string_view name) {
  std::string result;
  result.reserve(name.size());
  bool previous_separator = false;
  for (char ch : name) {
    ch = lowercase_ascii(ch);
    if (is_save_name_character(ch)) {
      result.push_back(ch);
      previous_separator = false;
      continue;
    }
    if ((ch == '-' || ch == '_' || ch == ' ' || ch == '.') && !result.empty() &&
        !previous_separator) {
      result.push_back('-');
      previous_separator = true;
    }
  }
  while (!result.empty() && result.back() == '-') {
    result.pop_back();
  }
  if (result.empty() || result == "." || result == "..") {
    return "world";
  }
  return result;
}

[[nodiscard]] std::string unique_server_save_name(
    const arv::io::EngineConfig& config,
    std::string_view display_name) {
  const std::string base = sanitize_save_name(display_name);
  std::string candidate = base;
  for (arv::u32 suffix = 2; std::filesystem::exists(config.paths.server_save_path(candidate));
       ++suffix) {
    candidate = base + "-" + std::to_string(suffix);
  }
  return candidate;
}

[[nodiscard]] arv::u64 generated_world_seed() noexcept {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return static_cast<arv::u64>(now == 0 ? 1 : now);
}

[[nodiscard]] arv::u64 parse_or_generate_seed(std::string_view seed_text) noexcept {
  if (seed_text.empty()) {
    return generated_world_seed();
  }

  arv::u64 seed = 0;
  const char* begin = seed_text.data();
  const char* end = seed_text.data() + seed_text.size();
  const auto parsed = std::from_chars(begin, end, seed);
  if (parsed.ec != std::errc{} || parsed.ptr != end) {
    return generated_world_seed();
  }
  return seed;
}

[[nodiscard]] std::optional<arv::net::NetworkAddress>
parse_server_address(std::string_view address) {
  const arv::usize colon = address.rfind(':');
  arv::net::NetworkAddress parsed;
  if (colon == std::string_view::npos) {
    if (address.empty()) {
      return std::nullopt;
    }
    parsed.host = std::string{address};
    return parsed;
  }

  if (colon == 0U || colon + 1U >= address.size() ||
      address.find(':') != colon) {
    return std::nullopt;
  }

  arv::u32 port = 0;
  const std::string_view port_text = address.substr(colon + 1U);
  const char* begin = port_text.data();
  const char* end = port_text.data() + port_text.size();
  const auto port_result = std::from_chars(begin, end, port);
  if (port_result.ec != std::errc{} || port_result.ptr != end || port == 0U ||
      port > std::numeric_limits<arv::u16>::max()) {
    return std::nullopt;
  }

  parsed.host = std::string{address.substr(0U, colon)};
  parsed.port = static_cast<arv::u16>(port);
  return parsed;
}

struct DesktopGameSession {
  explicit DesktopGameSession(arv::TickRate base_tps) : ticks(base_tps) {
  }

  std::string server_save_name{};
  std::string server_save_display_name{};
  std::filesystem::path server_save_root{};
  std::string world_save_name{"default"};
  arv::u64 world_seed{1};
  arv::world::WorldGenerationConfig generation_config{};
  arv::i32 min_chunk_y{};
  arv::i32 max_chunk_y{};
  std::unique_ptr<arv::server::ServerRuntime> integrated_server{};
  std::unique_ptr<arv::client::ClientRuntime> client{};
  arv::world::WorldRuntime client_world{arv::server::DefaultWorldID};
  arv::desktop::ChunkViewState chunk_view{};
  std::deque<arv::protocol::ChunkUnload> pending_client_chunk_unloads{};
  std::deque<arv::protocol::ChunkData> pending_client_chunk_data{};
  std::deque<arv::protocol::BlockUpdateBatch> pending_client_block_updates{};
  arv::core::FixedTimestepAccumulator ticks;
  arv::TickIndex client_tick_index{};
  std::vector<arv::ui::UiItemStackView> inventory_slots{};
  std::optional<arv::ui::UiItemStackView> carried_inventory_stack{};
  std::vector<arv::ui::PlayerListEntryView> tab_players{};
  std::vector<arv::ui::ChatLineView> chat_lines{};
  bool ui_snapshot_initialized{};
};

class DesktopMenuState final : public arv::ui::UIScreenDataSource,
                               public arv::ui::UICommandSink {
public:
  DesktopMenuState(
      std::filesystem::path saves_root,
      std::filesystem::path server_list_file)
      : saves_root_(std::move(saves_root)),
        server_list_file_(std::move(server_list_file)) {
    refresh_worlds();
    load_servers();
  }

  [[nodiscard]] std::vector<arv::ui::SinglePlayerSaveEntry> singleplayer_saves() const override {
    return saves;
  }

  [[nodiscard]] std::vector<arv::ui::ServerListEntry> multiplayer_servers() const override {
    return servers;
  }

  void submit(arv::ui::UICommand command) override {
    std::visit([this](auto&& concrete) { handle(concrete); }, std::move(command));
  }

  void refresh_worlds() {
    saves.clear();
    for (const arv::persistence::SavedServerSummary& save :
         arv::persistence::discover_saved_servers(saves_root_)) {
      saves.push_back({save.name, save.server_save_name, save.primary_world_name, {}});
    }
  }

  [[nodiscard]] std::optional<arv::ui::StartSinglePlayerCommand>
  take_start_singleplayer_request() noexcept {
    return std::exchange(start_singleplayer_request_, std::nullopt);
  }

  [[nodiscard]] std::optional<arv::ui::CreateWorldCommand>
  take_create_world_request() noexcept {
    return std::exchange(create_world_request_, std::nullopt);
  }

  [[nodiscard]] std::optional<arv::ui::DeleteSinglePlayerSaveCommand>
  take_delete_save_request() noexcept {
    return std::exchange(delete_save_request_, std::nullopt);
  }

  [[nodiscard]] std::optional<arv::ui::JoinServerCommand>
  take_join_server_request() noexcept {
    return std::exchange(join_server_request_, std::nullopt);
  }

  [[nodiscard]] bool take_back_to_main_menu_request() noexcept {
    return std::exchange(back_to_main_menu_requested_, false);
  }

  std::vector<arv::ui::SinglePlayerSaveEntry> saves{};
  std::vector<arv::ui::ServerListEntry> servers{};
  bool quit_requested{};

private:
  void handle(const arv::ui::StartSinglePlayerCommand& command) {
    start_singleplayer_request_ = command;
  }

  void handle(const arv::ui::CreateWorldCommand& command) {
    create_world_request_ = command;
  }

  void handle(const arv::ui::DeleteSinglePlayerSaveCommand& command) {
    delete_save_request_ = command;
  }

  void handle(const arv::ui::JoinServerCommand& command) {
    join_server_request_ = command;
  }

  void handle(const arv::ui::SaveServerEntryCommand& command) {
    if (command.edit_index.has_value() && command.edit_index.value() < servers.size()) {
      servers[command.edit_index.value()] = command.server;
    } else {
      servers.push_back(command.server);
    }
    persist_servers();
  }

  void handle(const arv::ui::RemoveServerEntryCommand& command) {
    if (command.index < servers.size()) {
      servers.erase(servers.begin() + static_cast<std::ptrdiff_t>(command.index));
      persist_servers();
    }
  }

  void handle(const arv::ui::ResumeGameCommand&) {
  }
  void handle(const arv::ui::BackToMainMenuCommand&) {
    back_to_main_menu_requested_ = true;
  }

  void handle(const arv::ui::QuitGameCommand&) {
    quit_requested = true;
  }

  void load_servers() {
    servers.clear();
    auto loaded = arv::io::load_server_list_file(server_list_file_);
    if (!loaded) {
      arv::core::warn("config", loaded.error().message);
      return;
    }

    servers.reserve(loaded.value().size());
    for (const arv::io::SavedServerListEntry& server : loaded.value()) {
      servers.push_back({server.name, server.address});
    }
  }

  void persist_servers() const {
    std::vector<arv::io::SavedServerListEntry> saved_servers;
    saved_servers.reserve(servers.size());
    for (const arv::ui::ServerListEntry& server : servers) {
      saved_servers.push_back({server.name, server.address});
    }

    auto saved = arv::io::save_server_list_file(server_list_file_, saved_servers);
    if (!saved) {
      arv::core::warn("config", saved.error().message);
    }
  }

  std::filesystem::path saves_root_{};
  std::filesystem::path server_list_file_{};
  std::optional<arv::ui::StartSinglePlayerCommand> start_singleplayer_request_{};
  std::optional<arv::ui::CreateWorldCommand> create_world_request_{};
  std::optional<arv::ui::DeleteSinglePlayerSaveCommand> delete_save_request_{};
  std::optional<arv::ui::JoinServerCommand> join_server_request_{};
  bool back_to_main_menu_requested_{};
};

void pace_frame(
    const arv::io::RenderConfig& render,
    std::chrono::steady_clock::time_point frame_start) {
  if (!render.frame_pacing_enabled || render.frame_pacing_target_fps == 0U) {
    return;
  }

  ARV_PROFILE_ZONE("GPU wait time");
  const auto target_frame =
      std::chrono::duration<double>(1.0 / static_cast<double>(render.frame_pacing_target_fps));
  const auto target_end =
      frame_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(target_frame);
  const auto spin_margin = std::chrono::microseconds(render.frame_pacing_spin_us);

  while (true) {
    const auto now = std::chrono::steady_clock::now();
    if (now + spin_margin >= target_end) {
      break;
    }
    std::this_thread::sleep_until(target_end - spin_margin);
  }

  while (std::chrono::steady_clock::now() < target_end) {
  }
}

} // namespace

int main(int argc, char** argv) {
  arv::core::info("desktop", "starting Arvane desktop client");

  auto config_result = arv::io::load_config(arv::desktop::parse_config_options(argc, argv));
  if (!config_result) {
    arv::core::error("config", config_result.error().message);
    return 1;
  }
  const arv::io::EngineConfig config = config_result.value();
  arv::asset::ResourceManager resources(config.paths.assets_path());

  arv::platform::GlfwLibrary glfw;
  if (!glfw.initialized()) {
    arv::core::error("platform", "failed to initialize GLFW");
    return 1;
  }

  const bool swap_interval_vsync =
      config.window.vsync && !config.render.frame_pacing_enabled;
  auto window_result = arv::platform::OpenGLWindow::create({config.window.width,
                                                            config.window.height,
                                                            config.window.title,
                                                            swap_interval_vsync});
  if (!window_result) {
    arv::core::error("platform", window_result.error().message);
    return 1;
  }
  arv::core::info(
      "render",
      "vsync={} frame_pacing={} target_fps={}",
      swap_interval_vsync ? "enabled" : "disabled",
      config.render.frame_pacing_enabled ? "enabled" : "disabled",
      config.render.frame_pacing_target_fps);
  arv::platform::OpenGLWindow window = std::move(window_result.value());
  arv::core::set_profile_thread_name("main/render");
  arv::desktop::DebugImGuiLayer debug_imgui;
  if (!debug_imgui.initialize(window.native_handle())) {
    arv::core::error("debug_ui", "failed to initialize ImGui debug UI");
    return 1;
  }
  arv::desktop::DebugMenuState debug_menu;
  const auto input_bindings_result =
      arv::io::load_input_bindings_file_or_defaults(config.paths.input_bindings_file);
  if (!input_bindings_result) {
    arv::core::warn("input", input_bindings_result.error().message);
  } else {
    window.set_input_bindings(input_bindings_result.value());
  }

  arv::render::OpenGLRenderer renderer;
  if (!arv::desktop::configure_renderer(renderer, resources, config)) {
    return 1;
  }

  auto framebuffer = window.framebuffer_size();
  renderer.resize(framebuffer.width, framebuffer.height);

  auto block_definition_text = arv::io::read_text_file(
      config.paths.config_path() / "blocks.json",
      "startup block definitions");
  if (!block_definition_text) {
    arv::core::error("blocks", block_definition_text.error().message);
    return 1;
  }

  auto block_definitions = arv::voxel::parse_block_definitions_json(block_definition_text.value());
  if (!block_definitions) {
    arv::core::error("blocks", block_definitions.error().message);
    return 1;
  }

  auto block_registry_result =
      arv::voxel::BlockRegistry::build(std::move(block_definitions.value()));
  if (!block_registry_result) {
    arv::core::error("blocks", block_registry_result.error().message);
    return 1;
  }
  const arv::voxel::BlockRegistry block_registry = std::move(block_registry_result.value());
  const arv::u64 block_registry_hash = arv::persistence::hash_block_registry(block_registry);

  auto block_atlas_result = arv::asset::build_block_texture_atlas(resources, block_registry);
  if (!block_atlas_result) {
    arv::core::error("assets", block_atlas_result.error().message);
    return 1;
  }
  const arv::asset::BlockTextureAtlas block_atlas = std::move(block_atlas_result.value());
  if (block_atlas.atlas.pages.empty()) {
    arv::core::error("assets", "block texture atlas did not produce a page");
    return 1;
  }
  arv::render::TextureUploadData terrain_atlas_upload =
      arv::desktop::texture_upload_from_image(block_atlas.atlas.pages.front().image);
  terrain_atlas_upload.limit_mipmap_levels = true;
  terrain_atlas_upload.max_mipmap_level = block_atlas.atlas.mip_safe_max_level;
  renderer.set_chunk_texture_atlas(std::move(terrain_atlas_upload));

  arv::asset::FontSystem font_system;
  auto ui_font = font_system.load_face(
      resources.resolve_asset_path({"arvane:font/font"}, arv::asset::AssetKind::Font),
      18,
      {"arvane:font/font"});
  if (!ui_font) {
    arv::core::error("assets", ui_font.error().message);
    return 1;
  }
  std::vector<arv::asset::FontFaceHandle> ui_faces{ui_font.value()};
  for (const arv::asset::AssetID fallback : {
           arv::asset::AssetID{"arvane:font/symbol"},
           arv::asset::AssetID{"arvane:font/arabic"},
           arv::asset::AssetID{"arvane:font/emoji"},
       }) {
    const std::filesystem::path path =
        resources.resolve_asset_path(fallback, arv::asset::AssetKind::Font);
    if (!std::filesystem::exists(path)) {
      continue;
    }
    auto face = font_system.load_face(path, 18, fallback);
    if (face) {
      ui_faces.emplace_back(face.value());
    } else {
      arv::core::warn("assets", face.error().message);
    }
  }
  const arv::asset::FontStack ui_font_stack = font_system.make_stack(ui_faces);
  arv::ui::FontUiTextMeasurer ui_text_measurer(font_system, ui_font_stack);
  arv::ui::UiTree ui_tree;

  auto item_definition_text = arv::io::read_text_file(
      config.paths.config_path() / "items.json",
      "startup item definitions");
  if (!item_definition_text) {
    arv::core::error("items", item_definition_text.error().message);
    return 1;
  }
  auto item_definitions = arv::player::parse_item_definitions_json(item_definition_text.value());
  if (!item_definitions) {
    arv::core::error("items", item_definitions.error().message);
    return 1;
  }

  auto item_registry_result = arv::player::ItemRegistry::build_from_blocks_and_items(
      block_registry,
      std::move(item_definitions.value()));
  if (!item_registry_result) {
    arv::core::error("items", item_registry_result.error().message);
    return 1;
  }
  const arv::player::ItemRegistry item_registry = std::move(item_registry_result.value());
  const arv::u64 item_registry_hash = arv::persistence::hash_item_registry(item_registry);

  auto item_atlas_result = arv::desktop::build_item_texture_atlas(resources, item_registry);
  if (!item_atlas_result) {
    arv::core::error("assets", item_atlas_result.error().message);
    return 1;
  }
  const arv::asset::TextureAtlas item_atlas = std::move(item_atlas_result.value());
  if (item_atlas.pages.empty()) {
    arv::core::error("assets", "item texture atlas did not produce a page");
    return 1;
  }
  renderer.set_ui_item_atlas(
      arv::desktop::texture_upload_from_image(item_atlas.pages.front().image));

  auto entity_definition_text = arv::io::read_text_file(
      config.paths.config_path() / "entities.json",
      "startup entity definitions");
  if (!entity_definition_text) {
    arv::core::error("entities", entity_definition_text.error().message);
    return 1;
  }
  auto entity_definitions =
      arv::entity::parse_entity_definitions_json(entity_definition_text.value());
  if (!entity_definitions) {
    arv::core::error("entities", entity_definitions.error().message);
    return 1;
  }
  auto entity_registry_result =
      arv::entity::EntityRegistry::build_with_builtins(std::move(entity_definitions.value()));
  if (!entity_registry_result) {
    arv::core::error("entities", entity_registry_result.error().message);
    return 1;
  }
  const arv::entity::EntityRegistry entity_registry =
      std::move(entity_registry_result.value());

  auto entity_render_assets_result =
      arv::desktop::build_entity_render_assets(resources, entity_registry);
  if (!entity_render_assets_result) {
    arv::core::error("entities", entity_render_assets_result.error().message);
    return 1;
  }
  const arv::desktop::EntityRenderAssets entity_render_assets =
      std::move(entity_render_assets_result.value());
  if (!entity_render_assets.models.empty()) {
    if (entity_render_assets.texture_atlas.pages.empty()) {
      arv::core::error("entities", "entity models were loaded without an entity texture atlas");
      return 1;
    }
    arv::render::TextureUploadData entity_atlas_upload =
        arv::desktop::texture_upload_from_image(entity_render_assets.texture_atlas.pages.front().image);
    entity_atlas_upload.limit_mipmap_levels = true;
    entity_atlas_upload.max_mipmap_level = entity_render_assets.texture_atlas.mip_safe_max_level;
    renderer.set_entity_texture_atlas(std::move(entity_atlas_upload));
    for (const arv::render::EntityModelAsset& model : entity_render_assets.models) {
      renderer.set_entity_model(model);
    }
  }

  auto worldgen_text = arv::io::read_text_file(
      config.paths.config_path() / "worldgen/overworld.yaml",
      "overworld generator definition");
  if (!worldgen_text) {
    arv::core::error("worldgen", worldgen_text.error().message);
    return 1;
  }

  auto worldgen_config = arv::world::parse_world_generation_yaml(worldgen_text.value());
  if (!worldgen_config) {
    arv::core::error("worldgen", worldgen_config.error().message);
    return 1;
  }
  const arv::world::WorldGenerationConfig default_generation_config =
      std::move(worldgen_config.value());

  arv::server::ServerChunkStreamingConfig chunk_streaming;
  chunk_streaming.max_view_distance = config.chunks.load_distance;
  chunk_streaming.simulation_distance = config.chunks.simulation_distance;
  chunk_streaming.unload_margin = config.chunks.unload_margin;
  chunk_streaming.max_chunks_sent_per_player_tick =
      std::max<arv::u32>(config.chunk_pipeline.max_client_chunk_applies_per_frame, 1U);
  chunk_streaming.max_unloads_sent_per_player_tick = 64;

  arv::world::WorldTickPolicy world_tick_policy;
  world_tick_policy.block_update_interval =
      config.simulation.systems.block_updates.interval;
  world_tick_policy.block_update_budget =
      config.simulation.systems.block_updates.budget;
  world_tick_policy.fluid_interval =
      config.simulation.systems.fluids.interval;
  world_tick_policy.fluid_budget =
      config.simulation.systems.fluids.budget;

  arv::voxel::FluidDefinition water_fluid =
      arv::voxel::make_water_fluid_definition();
  water_fluid.flow_interval_ticks =
      config.simulation.fluids.water.flow_interval_ticks;
  const arv::player::PlayerTuning player_tuning = make_player_tuning(config.physics);

  arv::sched::RuntimeThreadBudgetConfig thread_budget_config;
  thread_budget_config.workers.worker_thread_count = config.jobs.worker_threads;
  thread_budget_config.workers.reserved_thread_count = config.jobs.reserved_threads;
  thread_budget_config.workers.min_worker_thread_count = config.jobs.min_worker_threads;
  thread_budget_config.workers.max_worker_thread_count = config.jobs.max_worker_threads;
  thread_budget_config.split_primary_secondary_pools = true;
  const arv::sched::RuntimeThreadBudgetPlan thread_budget =
      arv::sched::plan_runtime_thread_budget(thread_budget_config);

  arv::sched::Scheduler world_jobs{thread_budget.secondary_workers};
  arv::core::info(
      "jobs",
      "hardware_threads={} reserved_threads={} server_workers={} client_workers={}",
      thread_budget.hardware_thread_count,
      thread_budget.reserved_thread_count,
      thread_budget.primary_worker_thread_count,
      world_jobs.worker_count());

  std::unique_ptr<DesktopGameSession> game_session;
  std::unique_ptr<arv::net::GameNetworkingSocketsEnvironment> gns_environment;

  auto refresh_inventory_slots = [&](DesktopGameSession& session) {
    session.inventory_slots.clear();
    session.carried_inventory_stack.reset();
    if (session.client != nullptr && session.client->has_authoritative_inventory()) {
      session.inventory_slots = arv::desktop::inventory_slots_for_ui(
          session.client->inventory(),
          item_registry,
          item_atlas,
          block_atlas);
      session.carried_inventory_stack = arv::desktop::item_stack_for_ui(
          session.client->inventory_cursor_stack(),
          item_registry,
          item_atlas,
          block_atlas);
    }
  };

  auto collect_client_chunk_stream = [](DesktopGameSession& session) {
    std::vector<arv::protocol::ChunkUnload> drained_unloads =
        session.client->drain_received_chunk_unloads();
    for (const arv::protocol::ChunkUnload& unload : drained_unloads) {
      session.pending_client_chunk_unloads.emplace_back(unload);
    }

    std::vector<arv::protocol::ChunkData> drained_chunks =
        session.client->drain_received_chunk_data();
    for (arv::protocol::ChunkData& data : drained_chunks) {
      session.pending_client_chunk_data.emplace_back(std::move(data));
    }

    std::vector<arv::protocol::BlockUpdateBatch> drained_block_updates =
        session.client->drain_received_block_updates();
    for (arv::protocol::BlockUpdateBatch& batch : drained_block_updates) {
      session.pending_client_block_updates.emplace_back(std::move(batch));
    }
  };

  auto apply_pending_client_chunk_stream =
      [&](DesktopGameSession& session, arv::u32 chunk_budget, arv::u32 unload_budget) {
        bool changed = false;
        arv::u32 unloads_applied = 0;
        while (unloads_applied < unload_budget && !session.pending_client_chunk_unloads.empty()) {
          const arv::protocol::ChunkUnload unload = session.pending_client_chunk_unloads.front();
          session.pending_client_chunk_unloads.pop_front();
          ++unloads_applied;
          if (unload.world_id != session.client_world.id()) {
            continue;
          }
          if (session.client_world.unload_chunk(unload.coord)) {
            renderer.remove_chunk_mesh({unload.world_id, unload.coord});
            arv::desktop::remove_chunk_view_chunk(session.chunk_view, unload.coord);
            changed = true;
          }
        }

        arv::u32 chunks_applied = 0;
        while (chunks_applied < chunk_budget && !session.pending_client_chunk_data.empty()) {
          arv::protocol::ChunkData data = std::move(session.pending_client_chunk_data.front());
          session.pending_client_chunk_data.pop_front();
          ++chunks_applied;
          if (data.world_id != session.client_world.id()) {
            continue;
          }

          std::optional<std::array<arv::voxel::LightWord, arv::voxel::ChunkVolume>> lights;
          if (data.has_lighting) {
            lights = std::move(data.lights);
          }
          session.client_world.apply_chunk_snapshot(
              data.coord,
              std::move(data.blocks),
              std::move(lights),
              std::move(data.fluids),
              data.has_lighting && data.lighting_valid);
          changed = true;
        }

        while (!session.pending_client_block_updates.empty()) {
          arv::protocol::BlockUpdateBatch batch =
              std::move(session.pending_client_block_updates.front());
          session.pending_client_block_updates.pop_front();
          if (batch.world_id != session.client_world.id()) {
            continue;
          }
          for (const arv::protocol::BlockUpdate& update : batch.updates) {
            if (session.client_world.find_chunk(arv::block_to_chunk(update.pos)) == nullptr) {
              continue;
            }
            if (session.client_world.set_block(update.pos, update.new_state)) {
              changed = true;
            }
          }
          (void)session.client_world.drain_replicated_block_updates();
        }

        if (changed) {
          arv::desktop::request_chunk_view_mesh_rescan(session.chunk_view);
        }
      };

  auto save_and_shutdown_session = [&]() -> bool {
    if (!game_session) {
      return true;
    }

    DesktopGameSession& session = *game_session;
    if (session.integrated_server == nullptr) {
      for (const auto& [coord, item] : session.chunk_view.draw_items) {
        (void)coord;
        renderer.remove_chunk_mesh(item.key);
      }
      world_jobs.wait_for_all();
      game_session.reset();
      return true;
    }

    if (const arv::server::ServerSession* server_session =
            local_session(*session.integrated_server);
        server_session != nullptr) {
      (void)session.integrated_server->close_player_inventory_menu(server_session->player_id);
    }

    const std::vector<arv::persistence::SavedChunkSection> saved_chunks =
        arv::desktop::capture_saved_chunks(session.integrated_server->voxel_world());
    const std::vector<arv::persistence::PlayerSaveData> player_saves =
        arv::desktop::capture_player_saves(*session.integrated_server, session.world_save_name);
    const std::vector<arv::entity::PersistentEntityRecord> entity_saves =
        arv::desktop::capture_saved_entities(session.integrated_server->voxel_world());
    const arv::persistence::SaveMetadata save_metadata{
        arv::persistence::CurrentSaveFormatVersion,
        std::string{arv::persistence::CurrentGameVersion},
        block_registry_hash,
        item_registry_hash,
        session.server_save_display_name};
    const arv::persistence::WorldMetadata world_metadata{
        session.world_save_name,
        session.world_seed,
        session.integrated_server->voxel_world().tick_index(),
        session.generation_config,
        session.min_chunk_y,
        session.max_chunk_y,
        block_registry_hash,
        item_registry_hash};
    auto save_result = arv::persistence::save_server_root(
        session.server_save_root,
        save_metadata,
        world_metadata,
        saved_chunks,
        player_saves,
        entity_saves,
        block_registry,
        item_registry);
    if (!save_result) {
      arv::core::error("save", save_result.error().message);
      return false;
    }

    for (const auto& [coord, item] : session.chunk_view.draw_items) {
      (void)coord;
      renderer.remove_chunk_mesh(item.key);
    }
    world_jobs.wait_for_all();
    session.integrated_server->shutdown();
    game_session.reset();
    return true;
  };

  auto start_singleplayer_session =
      [&](std::string server_save_name,
          std::string world_name,
          std::optional<arv::u64> create_seed,
          bool create_new,
          std::string save_display_name) -> bool {
    if (server_save_name.empty()) {
      arv::core::error("save", "cannot start a singleplayer world without a server save name");
      return false;
    }
    if (world_name.empty()) {
      world_name = "default";
    }
    if (!save_and_shutdown_session()) {
      return false;
    }

    const std::filesystem::path server_save_root = config.paths.server_save_path(server_save_name);
    auto loaded_save_result = arv::persistence::load_server_root(
        server_save_root,
        world_name,
        block_registry,
        item_registry);
    if (!loaded_save_result) {
      arv::core::error("save", loaded_save_result.error().message);
      return false;
    }
    const arv::persistence::LoadedSaveData loaded_save =
        std::move(loaded_save_result.value());
    if (!create_new && !loaded_save.has_world_metadata) {
      arv::core::error(
          "save",
          "world '{}' was not found in server save '{}'",
          world_name,
          server_save_name);
      return false;
    }

    auto session = std::make_unique<DesktopGameSession>(config.simulation.base_tps);
    session->server_save_name = std::move(server_save_name);
    session->server_save_display_name = save_display_name.empty()
                                            ? loaded_save.save.name
                                            : std::move(save_display_name);
    if (session->server_save_display_name.empty()) {
      session->server_save_display_name = session->server_save_name;
    }
    session->server_save_root = server_save_root;
    session->world_save_name = std::move(world_name);
    session->world_seed = create_seed.value_or(generated_world_seed());
    session->generation_config = default_generation_config;
    std::pair<arv::i32, arv::i32> visible_chunk_y =
        arv::desktop::visible_worldgen_chunk_y_range(session->generation_config);
    if (loaded_save.has_world_metadata) {
      session->generation_config = loaded_save.world.generation_config;
      session->world_seed = loaded_save.world.seed;
      session->world_save_name = loaded_save.world.name;
      visible_chunk_y = {loaded_save.world.min_chunk_y, loaded_save.world.max_chunk_y};
    }
    session->min_chunk_y = visible_chunk_y.first;
    session->max_chunk_y = visible_chunk_y.second;

    auto worldgen_palette = arv::world::resolve_world_generation_palette(
        session->generation_config,
        block_registry);
    if (!worldgen_palette) {
      arv::core::error("worldgen", worldgen_palette.error().message);
      return false;
    }

    arv::server::ServerChunkGenerationSettings chunk_generation;
    chunk_generation.enabled = true;
    chunk_generation.seed = session->world_seed;
    chunk_generation.config = session->generation_config;
    chunk_generation.palette = std::move(worldgen_palette.value());

    arv::server::ServerStartInfo server_start_info;
    server_start_info.mode = arv::server::ServerMode::Integrated;
    server_start_info.base_tps = config.simulation.base_tps;
    server_start_info.listen_address = {"in-process", 0};
    server_start_info.block_registry = &block_registry;
    server_start_info.item_registry = &item_registry;
    server_start_info.player_tuning = player_tuning;
    server_start_info.fluid_definition = water_fluid;
    server_start_info.chunk_streaming = chunk_streaming;
    server_start_info.chunk_generation = std::move(chunk_generation);
    server_start_info.max_generation_results_per_tick =
        config.chunk_pipeline.max_generation_results_per_tick;
    server_start_info.max_lighting_results_per_tick =
        config.chunk_pipeline.max_lighting_results_per_tick;
    server_start_info.max_pending_generation_jobs =
        config.chunk_pipeline.max_generation_jobs_per_frame;
    server_start_info.worker_threads = thread_budget.primary_workers;
    server_start_info.world_tick_policy = world_tick_policy;
    server_start_info.entity_registry = &entity_registry;

    auto transport_pair = arv::net::make_in_process_transport_pair();
    auto integrated_server =
        std::make_unique<arv::server::ServerRuntime>(std::move(transport_pair.server));
    auto server_start = integrated_server->start(server_start_info);
    if (!server_start) {
      arv::core::error("server", server_start.error().message);
      return false;
    }

    if (loaded_save.has_world_metadata) {
      integrated_server->voxel_world().set_tick_index(loaded_save.world.tick_index);
    }
    for (const arv::persistence::SavedChunkSection& chunk : loaded_save.chunks) {
      integrated_server->voxel_world().restore_saved_chunk(
          chunk.coord,
          chunk.blocks,
          chunk.lights,
          chunk.fluids);
    }
    integrated_server->voxel_world().restore_persistent_entities(
        loaded_save.entities,
        &entity_registry);

    auto client = std::make_unique<arv::client::ClientRuntime>(std::move(transport_pair.client));
    client->set_player_tuning(player_tuning);
    client->set_chunk_view_distance(config.chunks.stream_distance);
    auto connect_result = client->connect_integrated();
    if (!connect_result) {
      arv::core::error("client", connect_result.error().message);
      integrated_server->shutdown();
      return false;
    }

    session->integrated_server = std::move(integrated_server);
    session->client = std::move(client);
    session->integrated_server->tick();
    session->client->fixed_tick(session->integrated_server->tick_index());
    collect_client_chunk_stream(*session);
    apply_pending_client_chunk_stream(
        *session,
        config.chunk_pipeline.max_client_chunk_applies_per_frame,
        config.chunk_pipeline.max_client_chunk_unloads_per_frame);
    arv::desktop::apply_loaded_player_saves(
        *session->integrated_server,
        session->world_save_name,
        loaded_save.players);
    if (!loaded_save.players.empty()) {
      session->integrated_server->tick();
      session->client->fixed_tick(session->integrated_server->tick_index());
      collect_client_chunk_stream(*session);
      apply_pending_client_chunk_stream(
          *session,
          config.chunk_pipeline.max_client_chunk_applies_per_frame,
          config.chunk_pipeline.max_client_chunk_unloads_per_frame);
    }

    if (!arv::desktop::update_chunk_view(
            session->client_world,
            renderer,
            world_jobs,
            session->chunk_view,
            config.chunks,
            config.chunk_pipeline,
            arv::desktop::camera_chunk_coord(session->client->camera()),
            session->min_chunk_y,
            session->max_chunk_y,
            block_registry,
            block_atlas.render_data)) {
      session->integrated_server->shutdown();
      return false;
    }
    refresh_inventory_slots(*session);
    arv::core::info(
        "desktop",
        "entered singleplayer world '{}' from server save '{}'",
        session->world_save_name,
        session->server_save_name);
    game_session = std::move(session);
    return true;
  };

  auto start_remote_session =
      [&](const arv::ui::JoinServerCommand& command) -> bool {
    const std::optional<arv::net::NetworkAddress> address =
        parse_server_address(command.address);
    if (!address.has_value()) {
      arv::core::error("client", "invalid server address '{}'", command.address);
      return false;
    }

    if (!save_and_shutdown_session()) {
      return false;
    }

    if (gns_environment == nullptr) {
      gns_environment = std::make_unique<arv::net::GameNetworkingSocketsEnvironment>();
    }
    if (!gns_environment->initialized()) {
      arv::core::error("net", gns_environment->error_message());
      gns_environment.reset();
      return false;
    }

    auto session = std::make_unique<DesktopGameSession>(config.simulation.base_tps);
    session->server_save_display_name = command.name;
    session->generation_config = default_generation_config;
    const std::pair<arv::i32, arv::i32> visible_chunk_y =
        arv::desktop::visible_worldgen_chunk_y_range(session->generation_config);
    session->min_chunk_y = visible_chunk_y.first;
    session->max_chunk_y = visible_chunk_y.second;

    auto client = std::make_unique<arv::client::ClientRuntime>(
        std::make_unique<arv::net::GameNetworkingSocketsTransport>());
    client->set_player_tuning(player_tuning);
    client->set_chunk_view_distance(config.chunks.stream_distance);
    auto connect_result = client->connect(address.value());
    if (!connect_result) {
      arv::core::error("client", connect_result.error().message);
      return false;
    }

    session->client = std::move(client);
    arv::core::info(
        "desktop",
        "connecting to server '{}' at {}:{}",
        command.name,
        address->host,
        address->port);
    game_session = std::move(session);
    return true;
  };

  auto delete_singleplayer_save = [&](const arv::ui::DeleteSinglePlayerSaveCommand& command) {
    if (command.server_save_name.empty()) {
      return;
    }
    if (game_session != nullptr && game_session->server_save_name == command.server_save_name &&
        !save_and_shutdown_session()) {
      return;
    }

    const std::filesystem::path save_root = config.paths.server_save_path(command.server_save_name);
    std::error_code error;
    const std::uintmax_t removed = std::filesystem::remove_all(save_root, error);
    if (error) {
      arv::core::warn(
          "save",
          "failed to delete save '{}': {}",
          command.server_save_name,
          error.message());
    } else if (removed > 0) {
      arv::core::info("save", "deleted server save '{}'", command.server_save_name);
    }
  };

  auto previous_time = std::chrono::steady_clock::now();
  const auto app_start_time = previous_time;
  auto last_fps_display_time = previous_time;
  arv::u32 displayed_fps = 0;
  arv::u32 fps_sample_frames = 0;
  arv::Seconds fps_sample_seconds = 0.0F;
  double last_frame_work_ms = 0.0;
  arv::u64 profile_frame_index = 0;
  constexpr auto FpsHudRefreshInterval = std::chrono::milliseconds(250);
  arv::FrameArena frame_arena;
  DesktopMenuState menu_state{config.paths.saves_path(), config.paths.server_list_file};
  arv::ui::UIScreenManager screen_manager;
  screen_manager.push(arv::ui::make_main_menu_screen());

  while (!window.should_close()) {
    ARV_PROFILE_ZONE("Frame time");
    frame_arena.begin_frame();
    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<arv::Seconds> delta = now - previous_time;
    previous_time = now;
    const double frame_delta_ms =
        std::chrono::duration<double, std::milli>(delta).count();
    ++profile_frame_index;
    arv::core::set_current_profile_frame_index(profile_frame_index);
    arv::core::record_thread_timing(
        arv::core::PerfMetric::FrameDelta,
        frame_delta_ms,
        "main/render");
    ++fps_sample_frames;
    fps_sample_seconds += delta.count();
    if (displayed_fps == 0U ||
        now - last_fps_display_time >= FpsHudRefreshInterval) {
      displayed_fps = fps_sample_seconds > 0.0F
                          ? static_cast<arv::u32>(
                                std::round(static_cast<double>(fps_sample_frames) /
                                           static_cast<double>(fps_sample_seconds)))
                          : 0U;
      last_fps_display_time = now;
      fps_sample_frames = 0;
      fps_sample_seconds = 0.0F;
    }

    std::chrono::steady_clock::time_point frame_work_start{};
    if constexpr (arv::core::profiling_enabled()) {
      frame_work_start = std::chrono::steady_clock::now();
    }
    arv::platform::OpenGLWindow::poll_events();
    const arv::platform::InputSnapshot platform_input = window.poll_input();
    if (platform_input.debug_menu_toggle_pressed) {
      debug_menu.open = !debug_menu.open;
    }
    debug_imgui.begin_frame(
        window.native_handle(),
        platform_input,
        window.framebuffer_size(),
        delta.count());
    DesktopGameSession* active_session = game_session.get();
    const bool has_session =
        active_session != nullptr && active_session->client != nullptr;
    const bool was_inventory_open = has_session && active_session->client->inventory_open();
    arv::ui::UIContext menu_context{
        ui_tree,
        {
            static_cast<arv::f32>(framebuffer.width),
            static_cast<arv::f32>(framebuffer.height),
        },
        &menu_state,
        &menu_state,
        &screen_manager};
    if (!debug_menu.open && platform_input.pause_menu_pressed) {
      if (screen_manager.has_active_screen()) {
        (void)screen_manager.handle_cancel(menu_context);
      } else if (has_session && !active_session->client->inventory_open() &&
                 !active_session->client->chat_open()) {
        screen_manager.push(arv::ui::make_pause_menu_screen());
      } else if (!has_session) {
        screen_manager.push(arv::ui::make_main_menu_screen());
      }
    }

    const bool menu_screen_open = screen_manager.has_active_screen();
    arv::client::InputFrame client_input =
        has_session ? arv::desktop::to_client_input(platform_input)
                    : arv::client::InputFrame{};
    if (debug_menu.open || menu_screen_open) {
      client_input = {};
    }
    bool inventory_changed_by_ui = false;
    if (!debug_menu.open && menu_screen_open) {
      (void)route_screen_mouse_input(screen_manager, menu_context, platform_input);
      if (!platform_input.text_input.empty()) {
        (void)screen_manager.handle_text_input(platform_input.text_input, menu_context);
      }
      if (platform_input.text_backspace_pressed) {
        (void)screen_manager.handle_backspace(menu_context);
      }
      if (platform_input.text_submit_pressed) {
        (void)screen_manager.handle_submit(menu_context);
      }
    } else if (!debug_menu.open && has_session && active_session->client->inventory_open()) {
      inventory_changed_by_ui = route_inventory_mouse_input(
          ui_tree,
          *active_session->client,
          platform_input,
          active_session->client->inventory_cursor_stack().has_value());
    } else if (!debug_menu.open && has_session && active_session->client->chat_open()) {
      route_ui_mouse_focus(ui_tree, platform_input);
    }

    if (const auto back_to_main_menu = menu_state.take_back_to_main_menu_request();
        back_to_main_menu) {
      if (!save_and_shutdown_session()) {
        return 1;
      }
      menu_state.refresh_worlds();
      active_session = game_session.get();
    }
    if (const std::optional<arv::ui::DeleteSinglePlayerSaveCommand> command =
            menu_state.take_delete_save_request();
        command.has_value()) {
      delete_singleplayer_save(command.value());
      menu_state.refresh_worlds();
      active_session = game_session.get();
    }
    if (const std::optional<arv::ui::JoinServerCommand> command =
            menu_state.take_join_server_request();
        command.has_value()) {
      if (start_remote_session(command.value())) {
        screen_manager.clear();
      } else if (game_session == nullptr && screen_manager.empty()) {
        screen_manager.push(arv::ui::make_multi_player_menu_screen());
      }
      active_session = game_session.get();
    }
    if (const std::optional<arv::ui::CreateWorldCommand> command =
            menu_state.take_create_world_request();
        command.has_value()) {
      const std::string server_save_name = unique_server_save_name(config, command->name);
      const arv::u64 seed = parse_or_generate_seed(command->seed);
      if (start_singleplayer_session(server_save_name, "default", seed, true, command->name)) {
        menu_state.refresh_worlds();
        screen_manager.clear();
      } else if (game_session == nullptr && screen_manager.empty()) {
        screen_manager.push(arv::ui::make_single_player_menu_screen());
      }
      active_session = game_session.get();
    }
    if (const std::optional<arv::ui::StartSinglePlayerCommand> command =
            menu_state.take_start_singleplayer_request();
        command.has_value()) {
      if (start_singleplayer_session(
              command->server_save_name,
              command->world_name,
              std::nullopt,
              false,
              {})) {
        screen_manager.clear();
      } else if (game_session == nullptr && screen_manager.empty()) {
        screen_manager.push(arv::ui::make_single_player_menu_screen());
      }
      active_session = game_session.get();
    }

    const bool has_active_session =
        active_session != nullptr && active_session->client != nullptr;
    const bool has_active_integrated_session =
        has_active_session && active_session->integrated_server != nullptr;
    if (menu_state.quit_requested) {
      window.request_close();
    }
    if (has_active_session) {
      active_session->client->submit_input(std::move(client_input));
    }
    if (has_active_session && was_inventory_open && !active_session->client->inventory_open()) {
      (void)active_session->client->request_inventory_close();
      inventory_changed_by_ui = true;
    }
    if (has_active_session && inventory_changed_by_ui) {
      refresh_inventory_slots(*active_session);
    }
    window.set_cursor_captured(
        has_active_session &&
        !(active_session->client->inventory_open() || active_session->client->chat_open() ||
          debug_menu.open || screen_manager.blocks_gameplay_input()));

    bool fixed_tick_ran = false;
    if (has_active_session) {
      active_session->ticks.add_time(delta.count());
      const bool singleplayer_paused =
          has_active_integrated_session && screen_manager.pauses_singleplayer();
      while (active_session->ticks.consume_tick()) {
        ARV_PROFILE_ZONE("Simulation tick");
        if (!singleplayer_paused) {
          std::chrono::steady_clock::time_point simulation_start{};
          if constexpr (arv::core::profiling_enabled()) {
            simulation_start = std::chrono::steady_clock::now();
          }
          if (has_active_integrated_session) {
            active_session->integrated_server->tick();
            active_session->client->fixed_tick(active_session->integrated_server->tick_index());
          } else {
            ++active_session->client_tick_index;
            active_session->client->fixed_tick(active_session->client_tick_index);
          }
          collect_client_chunk_stream(*active_session);
          if constexpr (arv::core::profiling_enabled()) {
            const auto simulation_end = std::chrono::steady_clock::now();
            arv::core::record_thread_timing(
                arv::core::PerfMetric::SimulationTick,
                std::chrono::duration<double, std::milli>(
                    simulation_end - simulation_start)
                    .count(),
                "main/world");
          }
          fixed_tick_ran = true;
        }
      }
      apply_pending_client_chunk_stream(
          *active_session,
          config.chunk_pipeline.max_client_chunk_applies_per_frame,
          config.chunk_pipeline.max_client_chunk_unloads_per_frame);

      if (!arv::desktop::update_chunk_view(
              active_session->client_world,
              renderer,
              world_jobs,
              active_session->chunk_view,
              config.chunks,
              config.chunk_pipeline,
              arv::desktop::camera_chunk_coord(active_session->client->camera()),
              active_session->min_chunk_y,
              active_session->max_chunk_y,
              block_registry,
              block_atlas.render_data)) {
        return 1;
      }
    }

    const auto next_framebuffer = window.framebuffer_size();
    if (next_framebuffer.width != framebuffer.width ||
        next_framebuffer.height != framebuffer.height) {
      framebuffer = next_framebuffer;
      renderer.resize(framebuffer.width, framebuffer.height);
    }
    arv::render::FramePacket frame;
    arv::usize visible_chunk_count = 0;
    if (has_active_session) {
      const arv::f32 interpolation_alpha = active_session->ticks.interpolation_alpha();
      frame = active_session->client->build_frame_packet(
          interpolation_alpha,
          framebuffer.width,
          framebuffer.height);
      const std::vector<arv::render::ChunkDrawItem>& visible_chunks =
          arv::desktop::chunk_draw_items(active_session->chunk_view);
      visible_chunk_count = visible_chunks.size();
      frame.opaque_chunks = visible_chunks;
      frame.transparent_chunks = visible_chunks;
      const arv::f32 entity_animation_time =
          std::chrono::duration<arv::f32>(now - app_start_time).count();
      for (const arv::client::ReplicatedEntityMirror& mirror :
           active_session->client->replicated_entity_mirrors()) {
        if (mirror.world_id != frame.world_view.world_id) {
          continue;
        }
        const auto model = entity_render_assets.model_by_type.find(mirror.entity_type);
        if (model == entity_render_assets.model_by_type.end()) {
          continue;
        }
        arv::render::EntityDrawItem item;
        item.model_id = model->second;
        item.position = interpolate(mirror.previous_position, mirror.position, interpolation_alpha);
        item.rotation = mirror.rotation;
        item.scale = 1.0F;
        item.animation_name = "walking";
        item.animation_time_seconds = entity_animation_time;
        frame.entities.push_back(std::move(item));
      }
    }

    if (has_active_integrated_session) {
      const arv::desktop::DebugMenuResult debug_menu_result =
          arv::desktop::render_debug_menu(
              debug_menu,
              {
                  profile_frame_index,
                  frame_delta_ms,
                  last_frame_work_ms,
                  displayed_fps,
                  visible_chunk_count,
                  active_session->pending_client_chunk_data.size(),
                  active_session->pending_client_chunk_unloads.size(),
              },
              *active_session->integrated_server,
              *active_session->client,
              item_registry,
              entity_registry,
              world_jobs,
              renderer,
              active_session->chunk_view);
      if (debug_menu_result.inventory_changed) {
        refresh_inventory_slots(*active_session);
      }
    }

    if (has_active_session) {
      if (!active_session->ui_snapshot_initialized || fixed_tick_ran) {
        refresh_inventory_slots(*active_session);
        if (has_active_integrated_session && active_session->client->tab_menu_visible()) {
          active_session->tab_players =
              arv::desktop::player_list_for_ui(*active_session->integrated_server);
        } else {
          active_session->tab_players.clear();
        }
        active_session->ui_snapshot_initialized = true;
      } else if (has_active_integrated_session &&
                 active_session->client->tab_menu_visible() &&
                 active_session->tab_players.empty()) {
        active_session->tab_players =
            arv::desktop::player_list_for_ui(*active_session->integrated_server);
      } else if (!active_session->client->tab_menu_visible() &&
                 !active_session->tab_players.empty()) {
        active_session->tab_players.clear();
      }
      active_session->chat_lines.clear();
      for (const std::string& message : active_session->client->chat_messages()) {
        active_session->chat_lines.emplace_back(message, 1.0F);
      }
      arv::ui::build_gameplay_ui(
          ui_tree,
          {
              {
                  active_session->client->selected_hotbar_slot(),
                  arv::player::DefaultHotbarSlotCount,
                  {
                      static_cast<arv::f32>(framebuffer.width),
                      static_cast<arv::f32>(framebuffer.height),
                  },
                  std::span<const arv::ui::UiItemStackView>{
                      active_session->inventory_slots.data(),
                      std::min<arv::usize>(
                          active_session->inventory_slots.size(),
                          arv::player::DefaultHotbarSlotCount)},
              },
              {
                  active_session->client->inventory_open(),
                  "Inventory",
                  active_session->client->selected_hotbar_slot(),
                  arv::player::DefaultHotbarSlotCount,
                  {
                      static_cast<arv::f32>(framebuffer.width),
                      static_cast<arv::f32>(framebuffer.height),
                  },
                  std::span<const arv::ui::UiItemStackView>{
                      active_session->inventory_slots.data(),
                      active_session->inventory_slots.size()},
                  active_session->carried_inventory_stack,
                  {
                      platform_input.cursor_x,
                      platform_input.cursor_y,
                  },
              },
              {
                  true,
                  active_session->client->chat_open(),
                  std::span<const arv::ui::ChatLineView>{
                      active_session->chat_lines.data(),
                      active_session->chat_lines.size()},
                  active_session->client->chat_draft(),
              },
              {
                  active_session->client->tab_menu_visible(),
                  "Players",
                  "",
                  std::span<const arv::ui::PlayerListEntryView>{
                      active_session->tab_players.data(),
                      active_session->tab_players.size()},
              },
              {},
              {true, displayed_fps},
          });
    } else {
      ui_tree.clear();
    }
    menu_context.viewport = {
        static_cast<arv::f32>(framebuffer.width),
        static_cast<arv::f32>(framebuffer.height),
    };
    if (screen_manager.has_active_screen()) {
      screen_manager.append_to(menu_context);
    }
    ui_tree.layout(
        {
            static_cast<arv::f32>(framebuffer.width),
            static_cast<arv::f32>(framebuffer.height),
        },
        &ui_text_measurer);
    arv::ui::append_ui_draw_commands(ui_tree, font_system, ui_font_stack, frame);
    if (font_system.atlas_dirty()) {
      renderer.set_ui_font_atlas(
          arv::desktop::texture_upload_from_image(font_system.atlas_image()));
      font_system.clear_atlas_dirty();
    }
    std::chrono::steady_clock::time_point render_submit_start{};
    if constexpr (arv::core::profiling_enabled()) {
      render_submit_start = std::chrono::steady_clock::now();
    }
    renderer.submit_frame(std::move(frame));
    renderer.render();
    if constexpr (arv::core::profiling_enabled()) {
      const auto render_submit_end = std::chrono::steady_clock::now();
      arv::core::record_thread_timing(
          arv::core::PerfMetric::RenderSubmit,
          std::chrono::duration<double, std::milli>(
              render_submit_end - render_submit_start)
              .count(),
          "main/render");
    }
    debug_imgui.render();
    if constexpr (arv::core::profiling_enabled()) {
      const auto frame_work_end = std::chrono::steady_clock::now();
      const double frame_work_ms =
          std::chrono::duration<double, std::milli>(
              frame_work_end - frame_work_start)
              .count();
      arv::core::record_thread_timing(
          arv::core::PerfMetric::CpuFrameWork,
          frame_work_ms,
          "main/render");
      last_frame_work_ms = frame_work_ms;
      if (frame_work_ms > static_cast<double>(config.chunk_pipeline.frame_spike_log_ms)) {
        arv::core::warn(
            "frame_profile",
            "cpu frame work spike {}ms frame_delta={}ms queued_mesh={} pending_uploads={} uploads_last_frame={}",
            frame_work_ms,
            frame_delta_ms,
            has_active_session ? active_session->chunk_view.pending_meshes.size() : 0U,
            renderer.pending_mesh_upload_count(),
            renderer.mesh_uploads_last_frame());
      }
    } else {
      last_frame_work_ms = 0.0;
    }
    {
      ARV_PROFILE_ZONE("GPU wait time");
      std::chrono::steady_clock::time_point gpu_wait_start{};
      if constexpr (arv::core::profiling_enabled()) {
        gpu_wait_start = std::chrono::steady_clock::now();
      }
      window.swap_buffers();
      if constexpr (arv::core::profiling_enabled()) {
        const auto gpu_wait_end = std::chrono::steady_clock::now();
        arv::core::record_thread_timing(
            arv::core::PerfMetric::GpuWaitTime,
            std::chrono::duration<double, std::milli>(
                gpu_wait_end - gpu_wait_start)
                .count(),
            "main/render");
      }
    }
    pace_frame(config.render, now);
    ARV_PROFILE_FRAME_MARK("Frame");
  }

  const bool save_result = save_and_shutdown_session();
  debug_imgui.shutdown();
  renderer.begin_shutdown();
  if (!save_result) {
    return 1;
  }
  arv::core::info("desktop", "shutdown complete");
  return 0;
}
