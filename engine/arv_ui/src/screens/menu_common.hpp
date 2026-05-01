#pragma once

#include "arv/ui/screens/screen.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
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

enum class TextField : u8 {
  None,
  Name,
  Seed,
  ServerName,
  Address,
};

[[nodiscard]] inline UiElement make_text(
    std::string stable_id,
    std::string text,
    Vec4f color = TextColor) {
  UiElement element;
  element.stable_id = std::move(stable_id);
  element.type = UiElementType::Text;
  element.text = std::move(text);
  element.style.foreground = color;
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

[[nodiscard]] inline UiElement make_stack(std::string stable_id, f32 spacing = 8.0F) {
  UiElement stack;
  stack.stable_id = std::move(stable_id);
  stack.type = UiElementType::VerticalStack;
  stack.layout.spacing = spacing;
  stack.hit_test = false;
  return stack;
}

inline void add_backdrop(UiTree& tree, std::string stable_id, Vec4f color) {
  UiElement backdrop;
  backdrop.stable_id = std::move(stable_id);
  backdrop.type = UiElementType::Panel;
  backdrop.layout.width_mode = UiSizeMode::Fill;
  backdrop.layout.height_mode = UiSizeMode::Fill;
  backdrop.style.background = color;
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
    bool selected = false) {
  UiElement button;
  button.stable_id = stable_id;
  button.type = UiElementType::Button;
  button.layout.width_mode = UiSizeMode::Fixed;
  button.layout.height_mode = UiSizeMode::Fixed;
  button.layout.width = MenuButtonWidth;
  button.layout.height = MenuButtonHeight;
  button.layout.padding = {12.0F, 12.0F, 10.0F, 8.0F};
  button.style.background = !enabled ? ButtonDisabledColor
                                     : (selected ? ButtonSelectedColor : ButtonColor);
  button.style.accent = AccentColor;
  button.focusable = enabled;
  button.hit_test = enabled;
  button.enabled = enabled;
  button.selected = selected;
  button.action = enabled ? std::move(action) : std::string{};
  const UiNodeID button_id = tree.add_child(parent, std::move(button));
  tree.add_child(
      button_id,
      make_text(stable_id + ".label", std::move(label), enabled ? TextColor : MutedTextColor));
  return button_id;
}

[[nodiscard]] inline UiNodeID add_menu_shell(
    UIContext& ctx,
    std::string_view id,
    std::string title,
    f32 width,
    f32 height,
    Vec4f backdrop_color = BackdropColor) {
  add_backdrop(ctx.tree, std::string{id} + ".backdrop", backdrop_color);

  UiElement panel = make_panel(std::string{id} + ".panel", width, height);
  panel.layout.anchor = UiAnchor::Center;
  panel.layout.padding = {22.0F, 22.0F, 20.0F, 20.0F};
  const UiNodeID panel_id = ctx.tree.add_child(ctx.tree.root_id(), std::move(panel));

  UiElement stack = make_stack(std::string{id} + ".stack", 10.0F);
  const UiNodeID stack_id = ctx.tree.add_child(panel_id, std::move(stack));
  ctx.tree.add_child(stack_id, make_text(std::string{id} + ".title", std::move(title)));
  return stack_id;
}

inline void add_error_text(
    UiTree& tree,
    UiNodeID parent,
    std::string stable_id,
    const std::string& error) {
  if (!error.empty()) {
    tree.add_child(parent, make_text(std::move(stable_id), error, ErrorTextColor));
  }
}

inline void add_text_box(
    UiTree& tree,
    UiNodeID parent,
    std::string stable_id,
    std::string label,
    std::string value,
    std::string placeholder,
    std::string action,
    bool active) {
  tree.add_child(parent, make_text(stable_id + ".label", std::move(label), MutedTextColor));

  UiElement box =
      make_panel(stable_id, MenuButtonWidth, MenuButtonHeight, active ? ButtonSelectedColor : ButtonColor);
  box.type = UiElementType::Button;
  box.layout.padding = {12.0F, 12.0F, 10.0F, 8.0F};
  box.focusable = true;
  box.selected = active;
  box.style.accent = AccentColor;
  box.action = std::move(action);
  const UiNodeID box_id = tree.add_child(parent, std::move(box));
  const bool empty = value.empty();
  tree.add_child(
      box_id,
      make_text(
          stable_id + ".text",
          empty ? std::move(placeholder) : std::move(value),
          empty ? MutedTextColor : TextColor));
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
