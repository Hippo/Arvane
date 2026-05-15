#pragma once

#include "arv/base/ui/screens/screen.hpp"
#include "arv/core/text_edit.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arv::ui::screens_detail {

inline constexpr f32 MenuButtonWidth = 300.0F;
inline constexpr f32 MenuButtonHeight = 40.0F;
inline constexpr Vec4f BackdropColor{0.012F, 0.014F, 0.018F, 0.90F};
inline constexpr Vec4f PauseBackdropColor{0.010F, 0.012F, 0.016F, 0.66F};
inline constexpr Vec4f PanelColor{0.036F, 0.042F, 0.050F, 0.96F};
inline constexpr Vec4f ButtonColor{0.105F, 0.120F, 0.140F, 0.96F};
inline constexpr Vec4f ButtonSelectedColor{0.150F, 0.170F, 0.190F, 0.98F};
inline constexpr Vec4f ButtonDisabledColor{0.070F, 0.076F, 0.084F, 0.72F};
inline constexpr Vec4f TextColor{0.930F, 0.945F, 0.960F, 1.0F};
inline constexpr Vec4f MutedTextColor{0.660F, 0.700F, 0.740F, 1.0F};
inline constexpr Vec4f ErrorTextColor{0.980F, 0.420F, 0.380F, 1.0F};
inline constexpr Vec4f AccentColor{0.400F, 0.760F, 0.860F, 1.0F};
inline constexpr u8 ModalLayer = 1U;

[[nodiscard]] inline std::string tr(const UIContext& ctx, std::string_view key) {
  return ctx.language == nullptr ? std::string{key} : ctx.language->text(key);
}

[[nodiscard]] inline std::string tr(
    const UIContext& ctx,
    std::string_view key,
    std::initializer_list<arv::text::FormatArg> args) {
  return ctx.language == nullptr ? std::string{key} : ctx.language->format(key, args);
}

enum class TextField : u8 {
  None,
  Name,
  Seed,
  ServerName,
  Address,
};

using TextEditBuffer = arv::core::TextEditBuffer;

[[nodiscard]] inline arv::core::TextEditCommand core_text_edit_command_from(
    UITextEditCommand command) noexcept {
  switch (command) {
    case UITextEditCommand::MoveLeft:
      return arv::core::TextEditCommand::MoveLeft;
    case UITextEditCommand::MoveRight:
      return arv::core::TextEditCommand::MoveRight;
    case UITextEditCommand::MoveUp:
      return arv::core::TextEditCommand::MoveUp;
    case UITextEditCommand::MoveDown:
      return arv::core::TextEditCommand::MoveDown;
    case UITextEditCommand::DeleteForward:
      return arv::core::TextEditCommand::DeleteForward;
    case UITextEditCommand::Copy:
      return arv::core::TextEditCommand::Copy;
    case UITextEditCommand::Paste:
      return arv::core::TextEditCommand::Paste;
    case UITextEditCommand::Cut:
      return arv::core::TextEditCommand::Cut;
    case UITextEditCommand::SelectAll:
      return arv::core::TextEditCommand::SelectAll;
  }
  return arv::core::TextEditCommand::MoveLeft;
}

inline bool handle_text_edit(
    TextEditBuffer& buffer,
    const UITextEditInput& input,
    UIContext& ctx,
    std::optional<usize> max_size = std::nullopt) {
  arv::core::TextEditInput core_input;
  core_input.command = core_text_edit_command_from(input.command);
  core_input.extend_selection = input.extend_selection;
  if (input.command == UITextEditCommand::Paste && ctx.clipboard != nullptr) {
    core_input.clipboard_text = ctx.clipboard->text();
  }
  const arv::core::TextEditResult result = buffer.handle_edit(core_input, max_size);
  if (result.clipboard_text.has_value() && ctx.clipboard != nullptr) {
    ctx.clipboard->set_text(result.clipboard_text.value());
  }
  return result.handled;
}

[[nodiscard]] inline UiElement make_text(
    std::string stable_id,
    std::string text,
    Vec4f color = TextColor,
    UiTextRole role = UiTextRole::Body) {
  UiElement element;
  element.stable_id = std::move(stable_id);
  element.type = UiElementType::Text;
  element.text = std::move(text);
  element.style.foreground = color;
  element.style.text_role = role;
  element.hit_test = false;
  return element;
}

[[nodiscard]] inline UiElement make_panel(
    std::string stable_id,
    f32 width,
    f32 height,
    Vec4f background = PanelColor) {
  UiElement panel;
  panel.stable_id = std::move(stable_id);
  panel.type = UiElementType::Panel;
  panel.layout.width_mode = UiSizeMode::Fixed;
  panel.layout.height_mode = UiSizeMode::Fixed;
  panel.layout.width = width;
  panel.layout.height = height;
  panel.style.background = background;
  panel.hit_test = true;
  return panel;
}

[[nodiscard]] inline f32 menu_button_width(const UiTheme& theme) noexcept {
  return std::max({MenuButtonWidth, theme.widgets.button.min_width, theme.widgets.input.min_width});
}

inline void apply_panel_style(UiElement& panel, const UiTheme& theme, bool elevated = false) {
  panel.style.background = elevated ? theme.colors.surface : theme.colors.surface_alt;
  panel.style.border = theme.colors.border;
  panel.style.border_width = theme.layout.border_width;
  panel.style.radius = theme.widgets.panel.radius;
}

inline void apply_control_style(
    UiElement& control,
    const UiTheme& theme,
    UiControlRole role,
    bool enabled,
    bool selected) {
  control.style.control_role = role;
  control.style.border = theme.colors.border;
  control.style.border_focus = theme.colors.border_focus;
  control.style.border_width = theme.layout.border_width;
  control.style.focus_border_width = std::max(theme.layout.border_width * 2.0F, 2.0F);
  control.style.radius = theme.widgets.button.radius;
  control.style.accent = theme.colors.accent;
  if (!enabled) {
    control.style.background = theme.colors.surface_pressed;
    control.style.background_hover = theme.colors.surface_pressed;
    control.style.background_pressed = theme.colors.surface_pressed;
    control.style.background_selected = theme.colors.surface_pressed;
    return;
  }
  if (role == UiControlRole::Primary) {
    control.style.background = selected ? theme.colors.accent : theme.colors.surface_alt;
    control.style.background_hover = selected ? theme.colors.accent_hover : theme.colors.surface_hover;
    control.style.background_pressed = selected ? theme.colors.accent_pressed : theme.colors.surface_pressed;
    control.style.background_selected = theme.colors.accent;
    control.style.border = selected ? theme.colors.accent_hover : theme.colors.border;
  } else if (role == UiControlRole::Danger) {
    control.style.background = theme.colors.danger;
    control.style.background_hover = theme.colors.danger_hover;
    control.style.background_pressed = theme.colors.danger_pressed;
    control.style.background_selected = theme.colors.danger_hover;
    control.style.border = theme.colors.danger_hover;
    control.style.accent = theme.colors.warning;
  } else {
    control.style.background = selected ? theme.colors.surface_selected : theme.colors.surface_alt;
    control.style.background_hover = theme.colors.surface_hover;
    control.style.background_pressed = theme.colors.surface_pressed;
    control.style.background_selected = theme.colors.surface_selected;
  }
}

[[nodiscard]] inline UiElement make_stack(std::string stable_id, f32 spacing = 8.0F) {
  UiElement stack;
  stack.stable_id = std::move(stable_id);
  stack.type = UiElementType::VerticalStack;
  stack.layout.spacing = spacing;
  stack.hit_test = false;
  return stack;
}

inline void add_backdrop(UiTree& tree, std::string stable_id, Vec4f color, u8 layer = 0U) {
  UiElement backdrop;
  backdrop.stable_id = std::move(stable_id);
  backdrop.type = UiElementType::Panel;
  backdrop.layout.width_mode = UiSizeMode::Fill;
  backdrop.layout.height_mode = UiSizeMode::Fill;
  backdrop.style.background = color;
  backdrop.layer = layer;
  backdrop.hit_test = true;
  tree.add_child(tree.root_id(), std::move(backdrop));
}

inline UiNodeID add_button(
    UiTree& tree,
    UiNodeID parent,
    std::string stable_id,
    std::string label,
    std::string action,
    bool enabled = true,
    bool selected = false,
    UiControlRole role = UiControlRole::Neutral) {
  const UiTheme& theme = tree.theme();
  UiElement button;
  button.stable_id = stable_id;
  button.type = UiElementType::Button;
  button.layout.width_mode = UiSizeMode::Fixed;
  button.layout.height_mode = UiSizeMode::Fixed;
  button.layout.width = menu_button_width(theme);
  button.layout.height = std::max(MenuButtonHeight, theme.widgets.button.height);
  button.layout.anchor = UiAnchor::TopCenter;
  button.layout.padding = {
      theme.widgets.button.padding_x,
      theme.widgets.button.padding_x,
      theme.widgets.button.padding_y,
      theme.widgets.button.padding_y};
  apply_control_style(button, theme, role, enabled, selected);
  button.focusable = enabled;
  button.hit_test = enabled;
  button.enabled = enabled;
  button.selected = selected;
  button.action = enabled ? std::move(action) : std::string{};
  const UiNodeID button_id = tree.add_child(parent, std::move(button));
  tree.add_child(
      button_id,
      make_text(
          stable_id + ".label",
          std::move(label),
          !enabled ? theme.colors.text_disabled :
          (role == UiControlRole::Primary && selected) || role == UiControlRole::Danger
              ? theme.colors.accent_text
              : theme.colors.text));
  if (UiElement* label_element = tree.find_by_stable_id(stable_id + ".label");
      label_element != nullptr) {
    label_element->layout.anchor = UiAnchor::Center;
  }
  return button_id;
}

inline UiNodeID add_list_row(
    UiTree& tree,
    UiNodeID parent,
    std::string stable_id,
    std::string title,
    std::string detail,
    std::string action,
    bool enabled = true,
    bool selected = false) {
  const UiTheme& theme = tree.theme();
  UiElement row;
  row.stable_id = stable_id;
  row.type = UiElementType::Button;
  row.layout.width_mode = UiSizeMode::Fixed;
  row.layout.height_mode = UiSizeMode::Fixed;
  row.layout.width = std::max(340.0F, menu_button_width(theme));
  row.layout.height = std::max(58.0F, theme.widgets.button.height + theme.layout.spacing_md);
  row.layout.anchor = UiAnchor::TopCenter;
  row.layout.padding = {
      theme.widgets.button.padding_x,
      theme.widgets.button.padding_x,
      theme.layout.spacing_sm,
      theme.layout.spacing_sm};
  apply_control_style(row, theme, UiControlRole::Neutral, enabled, selected);
  row.focusable = enabled;
  row.hit_test = enabled;
  row.enabled = enabled;
  row.selected = selected;
  row.action = enabled ? std::move(action) : std::string{};
  const UiNodeID row_id = tree.add_child(parent, std::move(row));
  tree.add_child(
      row_id,
      make_text(stable_id + ".label", std::move(title), theme.colors.text));
  UiElement detail_text =
      make_text(stable_id + ".detail", std::move(detail), theme.colors.text_muted, UiTextRole::Small);
  detail_text.layout.offset = {0.0F, theme.typography.size_md + theme.layout.spacing_xs};
  tree.add_child(row_id, std::move(detail_text));
  return row_id;
}

[[nodiscard]] inline UiNodeID add_menu_shell(
    UIContext& ctx,
    std::string_view id,
    std::string title,
    f32 width,
    f32 height,
    Vec4f backdrop_color = BackdropColor) {
  const UiTheme& theme = theme_or_default(ctx.theme);
  add_backdrop(
      ctx.tree,
      std::string{id} + ".backdrop",
      backdrop_color.w == BackdropColor.w ? theme.colors.backdrop : backdrop_color);

  const f32 safe_width = std::min(width, std::max(280.0F, ctx.viewport.x - theme.layout.spacing_xl * 2.0F));
  const f32 safe_height = std::min(height, std::max(180.0F, ctx.viewport.y - theme.layout.spacing_xl * 2.0F));
  UiElement panel = make_panel(std::string{id} + ".panel", safe_width, safe_height);
  panel.layout.anchor = UiAnchor::Center;
  panel.layout.padding = {
      theme.widgets.panel.padding,
      theme.widgets.panel.padding,
      theme.widgets.panel.padding,
      theme.widgets.panel.padding};
  apply_panel_style(panel, theme, true);
  const UiNodeID panel_id = ctx.tree.add_child(ctx.tree.root_id(), std::move(panel));

  UiElement stack = make_stack(std::string{id} + ".stack", theme.layout.spacing_md);
  stack.layout.width_mode = UiSizeMode::Fixed;
  stack.layout.width = std::min(
      menu_button_width(theme),
      std::max(0.0F, safe_width - theme.widgets.panel.padding * 2.0F));
  stack.layout.anchor = UiAnchor::TopCenter;
  const UiNodeID stack_id = ctx.tree.add_child(panel_id, std::move(stack));
  UiElement title_element = make_text(
      std::string{id} + ".title",
      std::move(title),
      theme.colors.text,
      UiTextRole::Title);
  title_element.layout.anchor = UiAnchor::TopCenter;
  ctx.tree.add_child(stack_id, std::move(title_element));
  return stack_id;
}

inline void add_error_text(
    UiTree& tree,
    UiNodeID parent,
    std::string stable_id,
    const std::string& error) {
  if (!error.empty()) {
    tree.add_child(parent, make_text(std::move(stable_id), error, tree.theme().colors.danger));
  }
}

inline void add_text_box(
    UiTree& tree,
    UiNodeID parent,
    std::string stable_id,
    std::string label,
    const TextEditBuffer& value,
    std::string placeholder,
    std::string action,
    bool active) {
  const UiTheme& theme = tree.theme();
  tree.add_child(
      parent,
      make_text(
          stable_id + ".label",
          std::move(label),
          theme.colors.text_muted,
          UiTextRole::Small));

  UiElement box =
      make_panel(stable_id, menu_button_width(theme), theme.widgets.input.height);
  box.type = UiElementType::Button;
  box.layout.padding = {
      theme.widgets.input.padding_x,
      theme.widgets.input.padding_x,
      theme.widgets.input.padding_y,
      theme.widgets.input.padding_y};
  box.focusable = true;
  box.selected = active;
  box.clip_children = true;
  box.text = value.value;
  box.text_input = UiTextInputVisual{
      active,
      value.cursor,
      value.selection_start(),
      value.selection_end()};
  apply_control_style(box, theme, UiControlRole::Neutral, true, active);
  box.style.radius = theme.widgets.input.radius;
  box.style.text_role = UiTextRole::Body;
  box.action = std::move(action);
  const UiNodeID box_id = tree.add_child(parent, std::move(box));
  const bool empty = value.value.empty();
  tree.add_child(
      box_id,
      make_text(
          stable_id + ".text",
          empty ? std::move(placeholder) : value.value,
          empty ? theme.colors.text_muted : theme.colors.text));
}

[[nodiscard]] inline bool parse_index_suffix(
    std::string_view stable_id,
    std::string_view prefix,
    usize& value) noexcept {
  if (!stable_id.starts_with(prefix)) {
    return false;
  }
  const std::string_view suffix = stable_id.substr(prefix.size());
  if (suffix.empty()) {
    return false;
  }
  usize parsed = 0;
  for (const char ch : suffix) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    parsed = parsed * 10U + static_cast<usize>(ch - '0');
  }
  value = parsed;
  return true;
}

[[nodiscard]] inline bool parse_index_after_prefix(
    std::string_view stable_id,
    std::string_view prefix,
    usize& value) noexcept {
  if (!stable_id.starts_with(prefix)) {
    return false;
  }
  std::string_view suffix = stable_id.substr(prefix.size());
  const usize dot = suffix.find('.');
  if (dot != std::string_view::npos) {
    suffix = suffix.substr(0U, dot);
  }
  if (suffix.empty()) {
    return false;
  }
  usize parsed = 0;
  for (const char ch : suffix) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    parsed = parsed * 10U + static_cast<usize>(ch - '0');
  }
  value = parsed;
  return true;
}

[[nodiscard]] inline std::string trim_copy(std::string_view value) {
  usize begin = 0;
  while (begin < value.size() &&
         std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  usize end = value.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(value[end - 1U])) != 0) {
    --end;
  }
  return std::string{value.substr(begin, end - begin)};
}

[[nodiscard]] inline std::string sanitize_world_folder_name(std::string_view name) {
  std::string output;
  bool previous_separator = false;
  for (const char ch : name) {
    const unsigned char value = static_cast<unsigned char>(ch);
    if (std::isalnum(value) != 0 || ch == '-' || ch == '_') {
      output.push_back(ch);
      previous_separator = false;
    } else if (!previous_separator) {
      output.push_back('_');
      previous_separator = true;
    }
  }
  while (!output.empty() && output.front() == '_') {
    output.erase(output.begin());
  }
  while (!output.empty() && output.back() == '_') {
    output.pop_back();
  }
  return output;
}

[[nodiscard]] inline bool valid_seed_text(std::string_view seed) noexcept {
  if (seed.empty()) {
    return true;
  }
  u64 parsed = 0;
  const char* first = seed.data();
  const char* last = seed.data() + seed.size();
  const auto result = std::from_chars(first, last, parsed);
  return result.ec == std::errc{} && result.ptr == last;
}

[[nodiscard]] inline bool valid_server_address(std::string_view address) noexcept {
  const usize colon = address.rfind(':');
  if (colon == std::string_view::npos) {
    return !address.empty();
  }
  if (colon == 0U || colon + 1U >= address.size() ||
      address.find(':') != colon) {
    return false;
  }
  const std::string_view port_text = address.substr(colon + 1U);
  u32 port = 0;
  const char* first = port_text.data();
  const char* last = port_text.data() + port_text.size();
  const auto result = std::from_chars(first, last, port);
  return result.ec == std::errc{} && result.ptr == last && port > 0U &&
         port <= std::numeric_limits<u16>::max();
}

inline void erase_last_utf8_codepoint(std::string& text) {
  if (text.empty()) {
    return;
  }
  usize index = text.size() - 1U;
  while (index > 0U &&
         (static_cast<unsigned char>(text[index]) & 0xC0U) == 0x80U) {
    --index;
  }
  text.erase(index);
}

inline void submit_command(UIContext& ctx, UICommand command) {
  if (ctx.command_sink != nullptr) {
    ctx.command_sink->submit(std::move(command));
  }
}

[[nodiscard]] inline std::vector<SinglePlayerSaveEntry> saves_from(const UIContext& ctx) {
  return ctx.data_source == nullptr ? std::vector<SinglePlayerSaveEntry>{}
                                    : ctx.data_source->singleplayer_saves();
}

[[nodiscard]] inline std::string save_display_name(const SinglePlayerSaveEntry& save) {
  if (!save.name.empty()) {
    return save.name;
  }
  if (!save.server_save_name.empty()) {
    return save.server_save_name;
  }
  return save.world_name.empty() ? std::string{"default"} : save.world_name;
}

[[nodiscard]] inline std::string server_save_name_for_command(const SinglePlayerSaveEntry& save) {
  return save.server_save_name.empty() ? save_display_name(save) : save.server_save_name;
}

[[nodiscard]] inline std::string primary_world_name_for_command(const SinglePlayerSaveEntry& save) {
  return save.world_name.empty() ? std::string{"default"} : save.world_name;
}

[[nodiscard]] inline std::vector<ServerListEntry> servers_from(const UIContext& ctx) {
  return ctx.data_source == nullptr ? std::vector<ServerListEntry>{}
                                    : ctx.data_source->multiplayer_servers();
}

} // namespace arv::ui::screens_detail
