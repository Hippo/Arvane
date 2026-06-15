#include "arv/text/chat_message.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>

namespace arv::text {
namespace {

struct ChatScope {
  std::optional<RgbColor> color{};
  std::optional<ClickEvent> click{};
  std::optional<std::string> hover_text{};
};

struct ActiveChatStyle {
  TextStylePatch style{};
  std::optional<ClickEvent> click{};
  std::optional<std::string> hover_text{};
};

struct ColorName {
  std::string_view name;
  RgbColor color;
};

constexpr std::array<ColorName, 27> ChatColors{{
    {"aqua", {0x55U, 0xFFU, 0xFFU}},
    {"black", {0x00U, 0x00U, 0x00U}},
    {"blue", {0x55U, 0x55U, 0xFFU}},
    {"cyan", {0x00U, 0xFFU, 0xFFU}},
    {"dark_aqua", {0x00U, 0xAAU, 0xAAU}},
    {"dark_blue", {0x00U, 0x00U, 0xAAU}},
    {"dark_gray", {0x55U, 0x55U, 0x55U}},
    {"dark_green", {0x00U, 0xAAU, 0x00U}},
    {"dark_purple", {0xAAU, 0x00U, 0xAAU}},
    {"dark_red", {0xAAU, 0x00U, 0x00U}},
    {"gold", {0xFFU, 0xAAU, 0x00U}},
    {"gray", {0xAAU, 0xAAU, 0xAAU}},
    {"green", {0x55U, 0xFFU, 0x55U}},
    {"grey", {0xAAU, 0xAAU, 0xAAU}},
    {"light_purple", {0xFFU, 0x55U, 0xFFU}},
    {"lime", {0x55U, 0xFFU, 0x55U}},
    {"navy", {0x00U, 0x00U, 0x80U}},
    {"orange", {0xFFU, 0x88U, 0x00U}},
    {"pink", {0xFFU, 0x55U, 0xFFU}},
    {"purple", {0xAAU, 0x00U, 0xAAU}},
    {"red", {0xFFU, 0x55U, 0x55U}},
    {"silver", {0xC0U, 0xC0U, 0xC0U}},
    {"teal", {0x00U, 0x80U, 0x80U}},
    {"white", {0xFFU, 0xFFU, 0xFFU}},
    {"yellow", {0xFFU, 0xFFU, 0x55U}},
    {"light_gray", {0xAAU, 0xAAU, 0xAAU}},
    {"light_grey", {0xAAU, 0xAAU, 0xAAU}},
}};

[[nodiscard]] std::optional<RgbColor> parse_hex_color(std::string_view token) noexcept {
  if (token.size() != 7U || token.front() != '#') {
    return std::nullopt;
  }
  auto parse_byte = [](std::string_view text) -> std::optional<u8> {
    u32 value{};
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(first, last, value, 16);
    if (result.ec != std::errc{} || result.ptr != last || value > 0xFFU) {
      return std::nullopt;
    }
    return static_cast<u8>(value);
  };
  std::optional<u8> r = parse_byte(token.substr(1U, 2U));
  std::optional<u8> g = parse_byte(token.substr(3U, 2U));
  std::optional<u8> b = parse_byte(token.substr(5U, 2U));
  if (!r || !g || !b) {
    return std::nullopt;
  }
  return RgbColor{r.value(), g.value(), b.value()};
}

[[nodiscard]] std::optional<ClickAction> parse_click_action(
    std::string_view action) noexcept {
  if (action == "open_url") {
    return ClickAction::OpenUrl;
  }
  if (action == "suggest_command") {
    return ClickAction::SuggestCommand;
  }
  if (action == "run_command") {
    return ClickAction::RunCommand;
  }
  if (action == "copy_to_clipboard") {
    return ClickAction::CopyToClipboard;
  }
  return std::nullopt;
}

[[nodiscard]] bool variables_are_well_formed(std::string_view text) noexcept {
  for (usize index = 0U; index < text.size(); ++index) {
    if (text[index] != '{') {
      continue;
    }
    const usize close = text.find('}', index + 1U);
    if (close == std::string_view::npos) {
      return false;
    }
    const std::string_view name = text.substr(index + 1U, close - index - 1U);
    if (!valid_text_variable_name(name)) {
      return false;
    }
    index = close;
  }
  return true;
}

[[nodiscard]] ActiveChatStyle active_style(std::span<const ChatScope> scopes) {
  ActiveChatStyle active;
  for (const ChatScope& scope : scopes) {
    if (scope.color.has_value()) {
      active.style.color = scope.color;
    }
    if (scope.click.has_value()) {
      active.click = scope.click;
    }
    if (scope.hover_text.has_value()) {
      active.hover_text = scope.hover_text;
    }
  }
  return active;
}

void close_active_color(std::vector<ChatScope>& scopes) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    if (!it->color.has_value()) {
      continue;
    }
    scopes.erase(std::next(it).base());
    return;
  }
}

void append_segment(
    RichText& root,
    std::string_view segment,
    std::span<const ChatScope> scopes) {
  if (segment.empty()) {
    return;
  }
  ActiveChatStyle active = active_style(scopes);
  RichText node = plain_rich_text(std::string{segment}, std::move(active.style));
  if (active.click.has_value()) {
    node.click_event = std::move(active.click.value());
  }
  if (active.hover_text.has_value()) {
    node.hover_event = make_show_text_hover(plain_rich_text(active.hover_text.value()));
  }
  root.children.push_back(std::move(node));
}

[[nodiscard]] bool apply_tag(
    std::string_view token,
    std::vector<ChatScope>& scopes,
    ChatMarkupPermissions permissions) {
  if (token == "/") {
    if (scopes.empty()) {
      return false;
    }
    scopes.pop_back();
    return true;
  }

  if (std::optional<RgbColor> color = parse_hex_color(token); color.has_value()) {
    if (!permissions.color) {
      return false;
    }
    close_active_color(scopes);
    if (scopes.size() >= RichTextLimits{}.max_depth) {
      return false;
    }
    ChatScope scope;
    scope.color = color.value();
    scopes.push_back(std::move(scope));
    return true;
  }
  if (std::optional<RgbColor> color = named_chat_color(token); color.has_value()) {
    if (!permissions.color) {
      return false;
    }
    close_active_color(scopes);
    if (scopes.size() >= RichTextLimits{}.max_depth) {
      return false;
    }
    ChatScope scope;
    scope.color = color.value();
    scopes.push_back(std::move(scope));
    return true;
  }

  if (scopes.size() >= RichTextLimits{}.max_depth) {
    return false;
  }

  constexpr std::string_view click_prefix = "click:";
  if (token.starts_with(click_prefix)) {
    if (!permissions.click) {
      return false;
    }
    const std::string_view rest = token.substr(click_prefix.size());
    const usize separator = rest.find(':');
    if (separator == std::string_view::npos || separator == 0U) {
      return false;
    }
    std::optional<ClickAction> action = parse_click_action(rest.substr(0U, separator));
    const std::string_view value = rest.substr(separator + 1U);
    if (!action.has_value() ||
        value.size() > RichTextLimits{}.max_click_value_bytes ||
        !variables_are_well_formed(value)) {
      return false;
    }
    ChatScope scope;
    scope.click = ClickEvent{action.value(), std::string{value}};
    scopes.push_back(std::move(scope));
    return true;
  }

  constexpr std::string_view hover_prefix = "hover:";
  if (token.starts_with(hover_prefix)) {
    if (!permissions.hover) {
      return false;
    }
    const std::string_view rest = token.substr(hover_prefix.size());
    constexpr std::string_view show_text_prefix = "show_text:";
    if (!rest.starts_with(show_text_prefix)) {
      return false;
    }
    const std::string_view value = rest.substr(show_text_prefix.size());
    if (value.size() > RichTextLimits{}.max_hover_text_bytes ||
        !variables_are_well_formed(value)) {
      return false;
    }
    ChatScope scope;
    scope.hover_text = std::string{value};
    scopes.push_back(std::move(scope));
    return true;
  }

  return false;
}

} // namespace

std::optional<RgbColor> named_chat_color(std::string_view name) {
  const auto found = std::find_if(
      ChatColors.begin(),
      ChatColors.end(),
      [&](const ColorName& color) {
        return color.name == name;
      });
  if (found == ChatColors.end()) {
    return std::nullopt;
  }
  return found->color;
}

ChatMessageTemplate ChatMessageTemplate::parse(std::string_view source) {
  return parse(source, {});
}

ChatMessageTemplate ChatMessageTemplate::parse(
    std::string_view source,
    ChatMarkupPermissions permissions) {
  if (!is_valid_utf8(source)) {
    return raw(std::string{source});
  }

  RichText root;
  std::vector<ChatScope> scopes;
  usize segment_start = 0U;
  for (usize index = 0U; index < source.size();) {
    if (source[index] == '{') {
      const usize close = source.find('}', index + 1U);
      if (close == std::string_view::npos) {
        return raw(std::string{source});
      }
      const std::string_view name = source.substr(index + 1U, close - index - 1U);
      if (!valid_text_variable_name(name)) {
        return raw(std::string{source});
      }
      index = close + 1U;
      continue;
    }

    if (source[index] != '<') {
      ++index;
      continue;
    }

    const usize close = source.find('>', index + 1U);
    if (close == std::string_view::npos) {
      return raw(std::string{source});
    }
    append_segment(root, source.substr(segment_start, index - segment_start), scopes);
    if (!apply_tag(
            source.substr(index + 1U, close - index - 1U),
            scopes,
            permissions)) {
      return raw(std::string{source});
    }
    index = close + 1U;
    segment_start = index;
  }
  append_segment(root, source.substr(segment_start), scopes);

  ChatMessageTemplate message;
  message.source_ = std::string{source};
  message.rich_text_ = std::move(root);
  message.parsed_ = true;
  return message;
}

ChatMessageTemplate ChatMessageTemplate::raw(std::string source) {
  ChatMessageTemplate message;
  message.source_ = std::move(source);
  message.rich_text_ = plain_rich_text(message.source_);
  message.parsed_ = false;
  return message;
}

const std::string& ChatMessageTemplate::source() const noexcept {
  return source_;
}

bool ChatMessageTemplate::parsed() const noexcept {
  return parsed_;
}

RichText ChatMessageTemplate::to_rich_text(
    const TextVariableResolver& resolver) const {
  if (!parsed_) {
    return plain_rich_text(source_);
  }
  if (resolver.store != nullptr &&
      resolver.resolution != TextVariableResolution::LiveOnly) {
    const std::string resolved_source = interpolate_text_variables(source_, resolver);
    if (resolved_source != source_) {
      ChatMessageTemplate reparsed = ChatMessageTemplate::parse(resolved_source);
      return reparsed.parsed_ ? std::move(reparsed.rich_text_)
                              : plain_rich_text(resolved_source);
    }
  }
  return interpolate_rich_text_variables(rich_text_, resolver);
}

std::string ChatMessageTemplate::to_string(
    const TextVariableResolver& resolver) const {
  if (!parsed_) {
    return source_;
  }
  return to_plain_text(to_rich_text(resolver));
}

} // namespace arv::text
