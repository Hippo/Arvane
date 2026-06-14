#pragma once

#include <emscripten/html5.h>
#include <GLES3/gl3.h>

#include "arv/client/block_render_assets.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/client/entity_render_assets.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/base/ui/screens/screen.hpp"
#include "arv/base/ui/widgets/chat.hpp"
#include "arv/net/transport.hpp"
#include "arv/platform/input_bindings.hpp"
#include "arv/render/chunk_mesher.hpp"
#include "arv/text/language.hpp"
#include "arv/ui/core/ui.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::web {

inline constexpr usize MaxEntityShaderBones = 128U;

struct BrowserInputState {
  bool forward{};
  bool back{};
  bool left{};
  bool right{};
  bool jump{};
  bool jump_pressed{};
  bool sneak{};
  bool sprint{};
  bool mount_pressed{};
  bool vehicle_roll_left_held{};
  bool vehicle_roll_right_held{};
  bool primary_held{};
  bool secondary_held{};
  bool primary_pressed{};
  bool secondary_pressed{};
  bool primary_animation_pressed{};
  bool secondary_animation_pressed{};
  bool toggle_fly_pressed{};
  bool open_inventory_pressed{};
  bool open_chat_pressed{};
  bool tab_menu_held{};
  std::optional<u8> hotbar_select{};
  i8 hotbar_scroll_steps{};
  std::string text_input{};
  bool text_backspace_pressed{};
  bool text_submit_pressed{};
  bool text_cancel_pressed{};
  bool pause_menu_pressed{};
  double cursor_x{};
  double cursor_y{};
  bool cursor_initialized{};
  bool mouse_left_held{};
  bool mouse_right_held{};
  bool mouse_left_pressed{};
  bool mouse_right_pressed{};
  bool mouse_left_released{};
  bool mouse_right_released{};
  double mouse_wheel_y{};
  double mouse_dx{};
  double mouse_dy{};
  double last_frame_time{};
  double movement_accumulator_seconds{};
  u64 client_tick{};
  client::HeldItemAnimationPlayer held_item_animation{};
  bool held_item_secondary_use_active{};
};

struct IndexedGpuMesh {
  GLuint vao{};
  GLuint vbo{};
  GLuint ebo{};
  GLsizei index_count{};
};

struct ChunkGpuMesh {
  ChunkSectionCoord coord{};
  u64 content_version{};
  u64 light_version{};
  u64 dependency_signature{};
  IndexedGpuMesh opaque{};
  IndexedGpuMesh transparent{};
};

struct EntityGpuModel {
  GLuint vao{};
  GLuint vbo{};
  GLuint ebo{};
  GLsizei index_count{};
};

struct WebUiVertex {
  Vec2f position{};
  Vec2f uv{};
  Vec4f color{1.0F, 1.0F, 1.0F, 1.0F};
  float page{};
};

class WebUiTextMeasurer final : public ui::IUiTextMeasurer {
public:
  [[nodiscard]] Vec2f measure(
      std::string_view text,
      ui::UiTextRole role = ui::UiTextRole::Body) const override {
    const float height = line_height(role);
    return {
        static_cast<float>(text.size()) * height * 0.52F,
        height};
  }

  [[nodiscard]] float line_height(
      ui::UiTextRole role = ui::UiTextRole::Body) const override {
    switch (role) {
      case ui::UiTextRole::Small:
        return 14.0F;
      case ui::UiTextRole::Heading:
        return 22.0F;
      case ui::UiTextRole::Title:
        return 28.0F;
      case ui::UiTextRole::Mono:
      case ui::UiTextRole::Body:
        break;
    }
    return 18.0F;
  }
};

struct WebUiState {
  ui::UiTree tree{};
  asset::FontSystem fonts{};
  ui::UiFontStacks font_stacks{};
  WebUiTextMeasurer text_measurer{};
  ui::UiTheme theme{ui::make_default_ui_theme()};
  text::LanguageCatalog language{};
  ui::UIScreenManager screen_manager{};
  std::vector<ui::ChatLineView> chat_lines{};
  usize chat_history_scroll_offset{};
  f32 font_framebuffer_scale{1.0F};
  bool ready{};
};

struct ChunkRenderer {
  GLuint chunk_program{};
  GLint chunk_mvp_location{-1};
  GLint chunk_origin_location{-1};
  GLint chunk_atlas_location{-1};
  GLint chunk_atlas_inv_size_location{-1};
  GLint chunk_alpha_location{-1};
  GLint chunk_animation_time_location{-1};
  GLuint chunk_texture{};
  u32 chunk_texture_width{};
  u32 chunk_texture_height{};
  GLuint color_program{};
  GLint color_mvp_location{-1};
  GLuint entity_program{};
  GLint entity_mvp_location{-1};
  GLint entity_position_location{-1};
  GLint entity_rotation_location{-1};
  GLint entity_scale_location{-1};
  GLint entity_atlas_location{-1};
  GLint entity_atlas_size_location{-1};
  GLint entity_bone_count_location{-1};
  GLint entity_bone_pivots_location{-1};
  GLint entity_bone_rotations_location{-1};
  GLint entity_bone_positions_location{-1};
  GLuint entity_texture{};
  u32 entity_texture_width{};
  u32 entity_texture_height{};
  GLuint item_texture{};
  u32 item_texture_width{};
  u32 item_texture_height{};
  GLuint font_texture{};
  u32 font_texture_width{};
  u32 font_texture_height{};
  GLuint ui_program{};
  GLint ui_viewport_location{-1};
  GLint ui_atlas_location{-1};
  GLint ui_mode_location{-1};
  GLuint ui_vao{};
  GLuint ui_vbo{};
  std::vector<ChunkGpuMesh> meshes{};
  HashMap<render::EntityModelID, EntityGpuModel> entity_models{};
  GLuint entity_vao{};
  GLuint entity_vbo{};
  GLsizei entity_vertex_count{};
  std::array<Vec3f, MaxEntityShaderBones> entity_bone_pivots{};
  std::array<Vec3f, MaxEntityShaderBones> entity_bone_rotations{};
  std::array<Vec3f, MaxEntityShaderBones> entity_bone_positions{};
  bool announced_first_upload{};
  bool announced_first_entity_upload{};
};

struct WebBlockRenderAssets {
  client::ClientBlockRenderAssets loaded{};
  std::vector<std::filesystem::path> pack_cache_paths{};
  std::string registry_snapshot_json{};
  bool ready{};
};

struct WebEntityRenderAssets {
  client::ClientEntityRenderAssets loaded{};
  client::ItemAnimationLibrary item_animation_library{};
  asset::TextureAtlas item_atlas{};
  std::vector<std::filesystem::path> pack_cache_paths{};
  std::string registry_snapshot_json{};
  bool ready{};
};

using ChunkSectionIndex = HashMap<
    ChunkSectionCoord,
    const protocol::ClientboundChunkSectionData*>;

struct WebApplication {
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context{};
  int width{1};
  int height{1};
  double start_time{};
  bool pointer_locked{};
  bool focused{true};
  bool visible{true};
  bool connected{};
  bool connecting{};
  bool flushed_after_pack_ready{};
  net::ConnectionState last_connection_state{net::ConnectionState::Disconnected};
  std::unique_ptr<client::ClientRuntime> session{};
  std::vector<protocol::ClientboundChunkSectionData> chunk_sections{};
  platform::InputBindings input_bindings{platform::default_input_bindings()};
  BrowserInputState input{};
  ChunkRenderer renderer{};
  WebBlockRenderAssets block_assets{};
  WebEntityRenderAssets entity_assets{};
  WebUiState ui{};
};

extern WebApplication g_app;

void reset_input(bool keep_view = false);
[[nodiscard]] bool ui_wants_cursor();

} // namespace arv::web
