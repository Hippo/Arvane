#pragma once

#include "debug_imgui.hpp"
#include "debug_menu.hpp"
#include "desktop_assets.hpp"
#include "desktop_menu_state.hpp"
#include "desktop_session.hpp"

#include "arv/asset/resource.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/command/command.hpp"
#include "arv/core/arena.hpp"
#include "arv/core/time.hpp"
#include "arv/physics/collision.hpp"
#include "arv/platform/glfw_window.hpp"
#include "arv/render/opengl_renderer.hpp"
#include "arv/sched/scheduler.hpp"
#include "arv/ui/ui.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace arv::desktop {

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
      ui::UIScreenManager& screen_manager);

  [[nodiscard]] int run();

private:
  [[nodiscard]] ui::UIContext make_menu_context();
  [[nodiscard]] FrameTiming begin_frame();
  void update_fps_sample(
      std::chrono::steady_clock::time_point now,
      Seconds delta_seconds);
  [[nodiscard]] platform::InputSnapshot poll_platform_input();
  void handle_debug_menu_input(
      platform::InputSnapshot& platform_input,
      DesktopGameSession* active_session,
      bool has_session);
  void begin_debug_frame(
      const platform::InputSnapshot& platform_input,
      const FrameTiming& timing);
  void handle_pause_menu_input(
      const platform::InputSnapshot& platform_input,
      ui::UIContext& menu_context,
      DesktopGameSession* active_session,
      bool has_session);
  [[nodiscard]] bool route_input(
      const platform::InputSnapshot& platform_input,
      ui::UIContext& menu_context,
      DesktopGameSession* active_session,
      bool has_session,
      client::InputFrame& client_input);

  [[nodiscard]] bool process_menu_commands();
  void invalidate_ui_tree_cache() noexcept;
  [[nodiscard]] bool handle_remote_disconnect(DesktopGameSession* active_session);
  void handle_remote_connection_ready(DesktopGameSession* active_session);

  [[nodiscard]] const player::ItemRecord* selected_held_item(
      const DesktopGameSession& session) const noexcept;
  [[nodiscard]] bool selected_water_bucket_can_leave_empty_bucket(
      const DesktopGameSession& session) const noexcept;
  [[nodiscard]] std::optional<BlockPos> local_secondary_place_pos(
      const DesktopGameSession& session,
      const physics::ICollisionQuery& query) const;
  [[nodiscard]] bool preview_block_placement_succeeds(
      const DesktopGameSession& session,
      const player::ItemRecord& item,
      const physics::ICollisionQuery& query) const;
  [[nodiscard]] bool preview_water_bucket_placement_succeeds(
      const DesktopGameSession& session,
      const physics::ICollisionQuery& query) const;
  [[nodiscard]] bool preview_held_item_placement_succeeds(
      const DesktopGameSession& session,
      const player::ItemRecord* item) const;
  [[nodiscard]] Aabb3f replicated_entity_attack_bounds(
      const client::ReplicatedEntityMirror& mirror) const noexcept;
  [[nodiscard]] std::optional<EntityNetID> targeted_attack_entity(
      const DesktopGameSession& session,
      const player::ItemRecord* held_item) const;
  void send_local_entity_attack_if_targeted(
      DesktopGameSession& session,
      const player::ItemRecord* held_item) const;
  [[nodiscard]] bool held_item_animation_condition_passes(
      const DesktopGameSession& session,
      const player::ItemRecord* item,
      player::HeldItemAnimationPlayCondition condition) const;
  [[nodiscard]] HeldItemActionPlayResult play_held_item_action(
      DesktopGameSession& session,
      const player::ItemRecord* item,
      std::string_view action,
      Seconds now_seconds) const noexcept;
  void update_held_item_animation_from_input(
      DesktopGameSession& session,
      const client::InputFrame& client_input,
      Seconds now_seconds) const noexcept;

  [[nodiscard]] command::CommandResult execute_chat_command(
      DesktopGameSession& session,
      std::string_view command_text);
  void handle_chat_command_submit(
      DesktopGameSession* active_session,
      bool has_active_session,
      client::InputFrame& client_input);

  void submit_client_input_and_update_cursor(
      DesktopGameSession* active_session,
      bool has_active_session,
      bool was_inventory_open,
      client::InputFrame client_input,
      bool inventory_changed_by_ui,
      std::chrono::steady_clock::time_point now);
  [[nodiscard]] bool update_simulation_and_chunks(
      DesktopGameSession& active_session,
      std::chrono::duration<Seconds> delta,
      bool& fixed_tick_ran);
  void update_framebuffer_size();
  void update_block_outline_target(DesktopGameSession& active_session);

  void refresh_player_skin_render_assets(DesktopGameSession& active_session);
  void refresh_menu_profile_render_assets();

  [[nodiscard]] usize build_world_frame(
      render::FramePacket& frame,
      DesktopGameSession* active_session,
      bool has_active_session,
      std::chrono::steady_clock::time_point now);
  void render_debug_menu_if_available(
      DesktopGameSession* active_session,
      bool has_active_integrated_session,
      const FrameTiming& timing,
      usize visible_chunk_count);
  void append_menu_player_skin_preview(
      render::FramePacket& frame,
      const ui::UIContext& menu_context);
  void build_ui(
      render::FramePacket& frame,
      const platform::InputSnapshot& platform_input,
      ui::UIContext& menu_context,
      DesktopGameSession* active_session,
      bool has_active_session,
      bool has_active_integrated_session,
      bool fixed_tick_ran,
      std::chrono::steady_clock::time_point now);
  void update_gameplay_ui_snapshots(
      DesktopGameSession& active_session,
      bool has_active_integrated_session,
      bool fixed_tick_ran);
  void update_hotbar_item_popup(
      DesktopGameSession& active_session,
      std::chrono::steady_clock::time_point now);
  [[nodiscard]] f32 hotbar_item_popup_alpha(
      std::chrono::steady_clock::time_point now) const noexcept;
  [[nodiscard]] bool append_inventory_hover_tooltip_if_needed(
      DesktopGameSession& active_session,
      const platform::InputSnapshot& platform_input,
      const std::optional<ui::UiAction>& hover_action);
  void build_gameplay_ui(
      DesktopGameSession& active_session,
      const platform::InputSnapshot& platform_input,
      std::chrono::steady_clock::time_point now);

  void submit_render_and_present(
      render::FramePacket frame,
      const FrameTiming& timing,
      bool has_active_session,
      DesktopGameSession* active_session);
  void record_frame_work_profile(
      const FrameTiming& timing,
      bool has_active_session,
      DesktopGameSession* active_session);
  void swap_buffers();

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
  std::chrono::steady_clock::time_point hotbar_item_popup_expires_at_{};
  std::string hotbar_item_popup_name_{};
  std::string observed_hotbar_item_name_{};
  u8 observed_hotbar_slot_{};
  bool hotbar_item_observation_initialized_{};

  static constexpr auto FpsHudRefreshInterval = std::chrono::milliseconds(250);
  static constexpr auto HotbarItemPopupVisibleDuration =
      std::chrono::milliseconds(1500);
  static constexpr auto HotbarItemPopupFadeDuration =
      std::chrono::milliseconds(350);
};

[[nodiscard]] bool session_world_ready(const DesktopGameSession* session) noexcept;

} // namespace arv::desktop
