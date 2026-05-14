#pragma once

#include "arv/asset/font.hpp"
#include "arv/core/id.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"
#include "arv/render/render_types.hpp"
#include "arv/ui/theme.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::ui {
struct UiNodeTag {};
using UiNodeID = ID<UiNodeTag, u32>;

struct UiRect {
  f32 x{};
  f32 y{};
  f32 width{};
  f32 height{};
};

struct UiDisplayMetrics {
  Vec2f logical_size{};
  Vec2f framebuffer_size{};
  Vec2f framebuffer_scale{1.0F, 1.0F};

  [[nodiscard]] static UiDisplayMetrics from_logical_size(Vec2f logical_size) noexcept;
  [[nodiscard]] static UiDisplayMetrics from_window_and_framebuffer(
      Vec2f window_size,
      Vec2f framebuffer_size) noexcept;

  [[nodiscard]] Vec2f framebuffer_from_logical(Vec2f point) const noexcept;
  [[nodiscard]] UiRect framebuffer_from_logical(UiRect rect) const noexcept;
  [[nodiscard]] Vec2f logical_from_framebuffer(Vec2f point) const noexcept;
  [[nodiscard]] f32 scalar_to_framebuffer(f32 value) const noexcept;
  [[nodiscard]] u32 font_pixel_size(u32 logical_pixel_size) const noexcept;
};

enum class UiElementType : u8 {
  Root,
  Panel,
  Text,
  Button,
  VerticalStack,
  HorizontalStack,
  Grid,
  Spacer,
  Slot,
  Image,
};

enum class UiSizeMode : u8 {
  Content,
  Fixed,
  Fill,
};

enum class UiAnchor : u8 {
  TopLeft,
  TopCenter,
  TopRight,
  BottomLeft,
  BottomCenter,
  BottomRight,
  Center,
};

enum class UiMouseButton : u8 {
  Left,
  Right,
};

struct UiPadding {
  f32 left{};
  f32 right{};
  f32 top{};
  f32 bottom{};
};

struct UiLayout {
  UiSizeMode width_mode{UiSizeMode::Content};
  UiSizeMode height_mode{UiSizeMode::Content};
  f32 width{};
  f32 height{};
  f32 min_width{};
  f32 min_height{};
  f32 spacing{};
  UiPadding padding{};
  UiAnchor anchor{UiAnchor::TopLeft};
  Vec2f offset{};
  u32 grid_columns{1};
};

struct UiStyle {
  Vec4f background{0.0F, 0.0F, 0.0F, 0.0F};
  Vec4f background_hover{0.0F, 0.0F, 0.0F, 0.0F};
  Vec4f background_pressed{0.0F, 0.0F, 0.0F, 0.0F};
  Vec4f background_selected{0.0F, 0.0F, 0.0F, 0.0F};
  Vec4f foreground{1.0F, 1.0F, 1.0F, 1.0F};
  Vec4f accent{1.0F, 1.0F, 1.0F, 1.0F};
  Vec4f border{0.0F, 0.0F, 0.0F, 0.0F};
  Vec4f border_focus{0.0F, 0.0F, 0.0F, 0.0F};
  f32 border_width{};
  f32 focus_border_width{};
  f32 radius{};
  f32 letter_spacing{};
  UiTextRole text_role{UiTextRole::Body};
  UiControlRole control_role{UiControlRole::Neutral};
};

struct UiImageData {
  bool valid{};
  render::UiTextureSource source{render::UiTextureSource::White};
  u32 page{};
  std::array<Vec2f, 4> uvs{};
  bool custom_points{};
  std::array<Vec2f, 4> points{};
  Vec4f tint{1.0F, 1.0F, 1.0F, 1.0F};
};

struct UiElement {
  UiNodeID node_id{};
  std::string stable_id{};
  UiElementType type{UiElementType::Panel};
  UiLayout layout{};
  UiStyle style{};
  std::string text{};
  std::string action{};
  bool visible{true};
  bool enabled{true};
  bool focusable{};
  bool hit_test{true};
  bool selected{};
  bool clip_children{};
  bool clip_active{};
  UiImageData image{};
  UiRect rect{};
  UiRect clip{};
  std::vector<UiNodeID> children{};
};

struct UiAction {
  std::string element_id{};
  std::string action_id{};
  std::string interaction{};
};

class IUiTextMeasurer {
public:
  virtual ~IUiTextMeasurer() = default;
  [[nodiscard]] virtual Vec2f measure(std::string_view text, UiTextRole role = UiTextRole::Body) const = 0;
  [[nodiscard]] virtual f32 line_height(UiTextRole role = UiTextRole::Body) const = 0;
};

struct UiFontStacks {
  asset::FontStack small{};
  asset::FontStack body{};
  asset::FontStack heading{};
  asset::FontStack title{};
  asset::FontStack mono{};
};

class FontUiTextMeasurer final : public IUiTextMeasurer {
public:
  FontUiTextMeasurer(
      const asset::FontSystem& fonts,
      asset::FontStack stack,
      f32 logical_to_framebuffer_scale = 1.0F);
  FontUiTextMeasurer(
      const asset::FontSystem& fonts,
      UiFontStacks stacks,
      f32 logical_to_framebuffer_scale = 1.0F);
  void set_display_scale(f32 logical_to_framebuffer_scale);
  [[nodiscard]] Vec2f measure(std::string_view text, UiTextRole role = UiTextRole::Body) const override;
  [[nodiscard]] f32 line_height(UiTextRole role = UiTextRole::Body) const override;

private:
  [[nodiscard]] const asset::FontStack& stack_for(UiTextRole role) const noexcept;
  [[nodiscard]] std::string measure_cache_key(std::string_view text, UiTextRole role) const;
  [[nodiscard]] f32 framebuffer_to_logical_scale() const noexcept;

  const asset::FontSystem* fonts_{};
  UiFontStacks stacks_{};
  f32 logical_to_framebuffer_scale_{1.0F};
  mutable HashMap<std::string, Vec2f> measure_cache_{};
};

class UiTree {
public:
  UiTree();

  void clear();
  void set_theme(const UiTheme* theme) noexcept {
    theme_ = theme;
  }

  [[nodiscard]] const UiTheme& theme() const noexcept {
    return theme_or_default(theme_);
  }

  [[nodiscard]] UiNodeID root_id() const noexcept {
    return root_id_;
  }

  [[nodiscard]] UiElement* element(UiNodeID id) noexcept;
  [[nodiscard]] const UiElement* element(UiNodeID id) const noexcept;
  [[nodiscard]] UiElement* find_by_stable_id(std::string_view stable_id) noexcept;
  [[nodiscard]] const UiElement* find_by_stable_id(std::string_view stable_id) const noexcept;

  UiNodeID add_child(UiNodeID parent, UiElement element);

  void layout(Vec2f viewport, const IUiTextMeasurer* text_measurer);
  void layout_with_metrics(const UiDisplayMetrics& metrics, const IUiTextMeasurer* text_measurer);

  [[nodiscard]] std::optional<UiNodeID> hit_test(Vec2f point) const;
  [[nodiscard]] std::optional<UiNodeID> hit_test_framebuffer(
      Vec2f point,
      const UiDisplayMetrics& metrics) const;

  [[nodiscard]] std::optional<UiAction> route_mouse_move(Vec2f point);
  [[nodiscard]] std::optional<UiAction> route_mouse_down(Vec2f point, UiMouseButton button);
  [[nodiscard]] std::optional<UiAction> route_mouse_up(Vec2f point, UiMouseButton button);
  [[nodiscard]] std::optional<UiAction> route_mouse_move_framebuffer(
      Vec2f point,
      const UiDisplayMetrics& metrics);
  [[nodiscard]] std::optional<UiAction> route_mouse_down_framebuffer(
      Vec2f point,
      UiMouseButton button,
      const UiDisplayMetrics& metrics);
  [[nodiscard]] std::optional<UiAction> route_mouse_up_framebuffer(
      Vec2f point,
      UiMouseButton button,
      const UiDisplayMetrics& metrics);

  [[nodiscard]] UiNodeID hovered() const noexcept {
    return hovered_;
  }

  [[nodiscard]] UiNodeID active() const noexcept {
    return active_;
  }

  [[nodiscard]] UiNodeID keyboard_focus() const noexcept {
    return keyboard_focus_;
  }

  [[nodiscard]] std::span<const UiElement> elements() const noexcept {
    return elements_;
  }

private:
  [[nodiscard]] UiNodeID allocate_node();
  [[nodiscard]] Vec2f measure_element(UiNodeID id, Vec2f available, const IUiTextMeasurer* text_measurer);
  void arrange_element(UiNodeID id, UiRect rect, const IUiTextMeasurer* text_measurer);
  void arrange_element(
      UiNodeID id,
      UiRect rect,
      const IUiTextMeasurer* text_measurer,
      std::optional<UiRect> inherited_clip);
  [[nodiscard]] std::optional<UiNodeID> hit_test_recursive(UiNodeID id, Vec2f point) const;

  UiNodeID root_id_{UiNodeID{1}};
  UiNodeID next_id_{UiNodeID{2}};
  std::vector<UiElement> elements_{};
  HashMap<std::string, UiNodeID> stable_ids_{};
  UiNodeID hovered_{};
  UiNodeID active_{};
  UiNodeID keyboard_focus_{};
  UiNodeID mouse_capture_{};
  const UiTheme* theme_{};
};
struct UiDocumentElement {
  std::string type{};
  std::string id{};
  std::string text{};
  std::string action{};
  std::vector<UiDocumentElement> children{};
};

struct UiDocument {
  u32 version{1};
  std::string screen_id{};
  std::string owner{};
  UiDocumentElement root{};
};

struct UiDocumentLimits {
  u32 max_elements{256};
  u32 max_depth{16};
  u32 max_children_per_element{64};
  u32 max_text_length{2048};
};

struct UiDocumentValidation {
  bool accepted{};
  std::vector<std::string> errors{};
};

[[nodiscard]] UiDocumentValidation validate_ui_document(
    const UiDocument& document,
    UiDocumentLimits limits = {});
void append_ui_draw_commands(
    const UiTree& tree,
    asset::FontSystem& fonts,
    const UiFontStacks& font_stacks,
    render::FramePacket& frame);
void append_ui_draw_commands(
    const UiTree& tree,
    asset::FontSystem& fonts,
    const UiFontStacks& font_stacks,
    render::FramePacket& frame,
    const UiDisplayMetrics& metrics);
void append_ui_draw_commands(
    const UiTree& tree,
    asset::FontSystem& fonts,
    const asset::FontStack& font_stack,
    render::FramePacket& frame);
void append_ui_draw_commands(
    const UiTree& tree,
    asset::FontSystem& fonts,
    const asset::FontStack& font_stack,
    render::FramePacket& frame,
    const UiDisplayMetrics& metrics);

} // namespace arv::ui
