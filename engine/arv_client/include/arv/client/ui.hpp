#pragma once

#include "arv/asset/font.hpp"
#include "arv/core/id.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"
#include "arv/player/item.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/render/render_types.hpp"
#include "arv/voxel/block.hpp"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::client {

struct UiNodeTag {};
using UiNodeID = ID<UiNodeTag, u32>;

struct UiRect {
  f32 x{};
  f32 y{};
  f32 width{};
  f32 height{};
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
  ItemSlot,
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
  Vec4f foreground{1.0F, 1.0F, 1.0F, 1.0F};
  Vec4f accent{1.0F, 1.0F, 1.0F, 1.0F};
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
  [[nodiscard]] virtual Vec2f measure(std::string_view text) const = 0;
  [[nodiscard]] virtual f32 line_height() const = 0;
};

class FontUiTextMeasurer final : public IUiTextMeasurer {
public:
  FontUiTextMeasurer(const asset::FontSystem& fonts, asset::FontStack stack);
  [[nodiscard]] Vec2f measure(std::string_view text) const override;
  [[nodiscard]] f32 line_height() const override;

private:
  const asset::FontSystem* fonts_{};
  asset::FontStack stack_{};
};

class UiTree {
public:
  UiTree();

  void clear();

  [[nodiscard]] UiNodeID root_id() const noexcept {
    return root_id_;
  }

  [[nodiscard]] UiElement* element(UiNodeID id) noexcept;
  [[nodiscard]] const UiElement* element(UiNodeID id) const noexcept;
  [[nodiscard]] UiElement* find_by_stable_id(std::string_view stable_id) noexcept;
  [[nodiscard]] const UiElement* find_by_stable_id(std::string_view stable_id) const noexcept;

  UiNodeID add_child(UiNodeID parent, UiElement element);

  void layout(Vec2f viewport, const IUiTextMeasurer* text_measurer);

  [[nodiscard]] std::optional<UiNodeID> hit_test(Vec2f point) const;

  [[nodiscard]] std::optional<UiAction> route_mouse_move(Vec2f point);
  [[nodiscard]] std::optional<UiAction> route_mouse_down(Vec2f point, UiMouseButton button);
  [[nodiscard]] std::optional<UiAction> route_mouse_up(Vec2f point, UiMouseButton button);

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
  [[nodiscard]] std::optional<UiNodeID> hit_test_recursive(UiNodeID id, Vec2f point) const;

  UiNodeID root_id_{UiNodeID{1}};
  UiNodeID next_id_{UiNodeID{2}};
  std::vector<UiElement> elements_{};
  HashMap<std::string, UiNodeID> stable_ids_{};
  UiNodeID hovered_{};
  UiNodeID active_{};
  UiNodeID keyboard_focus_{};
  UiNodeID mouse_capture_{};
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

struct UiItemStackView {
  std::string item_name{};
  std::string display_name{};
  u16 count{};
  u16 durability{};
  u16 max_durability{};
  player::ItemRenderType render_type{player::ItemRenderType::Texture};
  UiImageData texture_icon{};
  std::array<UiImageData, voxel::BlockFaceCount> block_faces{};

  [[nodiscard]] bool empty() const noexcept {
    return item_name.empty() || count == 0U;
  }
};

struct HotbarHudModel {
  u8 selected_slot{};
  u8 slot_count{player::DefaultHotbarSlotCount};
  Vec2f viewport{};
  std::span<const UiItemStackView> slots{};
};

struct InventoryUiModel {
  bool visible{};
  std::string title{"Inventory"};
  u8 selected_hotbar_slot{};
  u8 hotbar_slot_count{player::DefaultHotbarSlotCount};
  Vec2f viewport{};
  std::span<const UiItemStackView> slots{};
  std::optional<UiItemStackView> cursor_stack{};
  Vec2f cursor_position{};
};

struct ChatLineView {
  std::string text{};
  f32 opacity{1.0F};
};

struct ChatUiModel {
  bool visible{true};
  bool input_active{};
  std::span<const ChatLineView> messages{};
  std::string draft{};
};

struct PlayerListEntryView {
  std::string name{};
  std::optional<u32> latency_ms{};
};

struct TabMenuUiModel {
  bool visible{};
  std::string header{};
  std::string footer{};
  std::span<const PlayerListEntryView> players{};
};

using ScoreboardVariableMap = HashMap<std::string, std::string>;

struct ScoreboardUiModel {
  bool visible{};
  std::string title{};
  std::span<const std::string> lines{};
  const ScoreboardVariableMap* variables{};
};

struct DebugHudModel {
  bool show_fps{};
  u32 fps{};
};

struct GameplayUiModel {
  HotbarHudModel hotbar{};
  InventoryUiModel inventory{};
  ChatUiModel chat{};
  TabMenuUiModel tab_menu{};
  ScoreboardUiModel scoreboard{};
  DebugHudModel debug{};
};

void build_hotbar_hud(UiTree& tree, HotbarHudModel model);
void build_inventory_ui(UiTree& tree, InventoryUiModel model);
void build_chat_ui(UiTree& tree, ChatUiModel model);
void build_tab_menu_ui(UiTree& tree, TabMenuUiModel model);
void build_scoreboard_ui(UiTree& tree, ScoreboardUiModel model);
void build_gameplay_ui(UiTree& tree, GameplayUiModel model);

void append_ui_draw_commands(
    const UiTree& tree,
    asset::FontSystem& fonts,
    const asset::FontStack& font_stack,
    render::FramePacket& frame);

} // namespace arv::client
