#include "arv/text/variables.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <utility>

namespace arv::text {
namespace {

[[nodiscard]] bool text_variable_char(char c) noexcept {
  const unsigned char value = static_cast<unsigned char>(c);
  return std::isalnum(value) != 0 || c == '_' || c == '-' || c == '.' || c == ':' ||
         c == '/';
}

[[nodiscard]] bool binding_matches(
    TextVariableBinding binding,
    TextVariableResolution resolution) noexcept {
  switch (resolution) {
    case TextVariableResolution::All:
      return true;
    case TextVariableResolution::ResolvedOnly:
      return binding == TextVariableBinding::Resolved;
    case TextVariableResolution::LiveOnly:
      return binding == TextVariableBinding::Live;
  }
  return false;
}

[[nodiscard]] const TextVariableValue* find_entry(
    const TextVariableMap& values,
    std::string_view name) {
  const auto found = values.find(std::string{name});
  return found == values.end() ? nullptr : &found->second;
}

} // namespace

void TextVariableStore::clear() {
  globals_.clear();
  players_.clear();
}

void TextVariableStore::clear_player(PlayerID player_id) {
  players_.erase(player_id);
}

void TextVariableStore::set_global(
    std::string name,
    std::string value,
    TextVariableBinding binding) {
  globals_.insert_or_assign(
      std::move(name),
      TextVariableValue{std::move(value), binding});
}

void TextVariableStore::set_player(
    PlayerID player_id,
    std::string name,
    std::string value,
    TextVariableBinding binding) {
  players_[player_id].insert_or_assign(
      std::move(name),
      TextVariableValue{std::move(value), binding});
}

bool TextVariableStore::clear_global(std::string_view name) {
  return globals_.erase(std::string{name}) > 0U;
}

bool TextVariableStore::clear_player_value(PlayerID player_id, std::string_view name) {
  auto player = players_.find(player_id);
  if (player == players_.end()) {
    return false;
  }
  const bool removed = player->second.erase(std::string{name}) > 0U;
  if (player->second.empty()) {
    players_.erase(player);
  }
  return removed;
}

std::optional<std::string_view> TextVariableStore::global(std::string_view name) const {
  const TextVariableValue* entry = find_entry(globals_, name);
  if (entry == nullptr) {
    return std::nullopt;
  }
  return std::string_view{entry->value};
}

std::optional<std::string_view> TextVariableStore::player(
    PlayerID player_id,
    std::string_view name) const {
  const auto player = players_.find(player_id);
  if (player == players_.end()) {
    return std::nullopt;
  }
  const TextVariableValue* entry = find_entry(player->second, name);
  if (entry == nullptr) {
    return std::nullopt;
  }
  return std::string_view{entry->value};
}

std::optional<std::string_view> TextVariableStore::resolve(
    std::optional<PlayerID> player_id,
    std::string_view name,
    TextVariableResolution resolution) const {
  if (player_id.has_value()) {
    const auto player_values = players_.find(player_id.value());
    if (player_values != players_.end()) {
      if (const TextVariableValue* entry = find_entry(player_values->second, name);
          entry != nullptr) {
        return binding_matches(entry->binding, resolution)
                   ? std::optional<std::string_view>{std::string_view{entry->value}}
                   : std::nullopt;
      }
    }
  }
  if (const TextVariableValue* entry = find_entry(globals_, name); entry != nullptr) {
    return binding_matches(entry->binding, resolution)
               ? std::optional<std::string_view>{std::string_view{entry->value}}
               : std::nullopt;
  }
  return std::nullopt;
}

std::optional<TextVariableBinding> TextVariableStore::global_binding(
    std::string_view name) const {
  const TextVariableValue* entry = find_entry(globals_, name);
  return entry == nullptr ? std::nullopt
                          : std::optional<TextVariableBinding>{entry->binding};
}

std::optional<TextVariableBinding> TextVariableStore::player_binding(
    PlayerID player_id,
    std::string_view name) const {
  const auto player = players_.find(player_id);
  if (player == players_.end()) {
    return std::nullopt;
  }
  const TextVariableValue* entry = find_entry(player->second, name);
  return entry == nullptr ? std::nullopt
                          : std::optional<TextVariableBinding>{entry->binding};
}

const TextVariableMap& TextVariableStore::globals() const noexcept {
  return globals_;
}

const TextVariableMap* TextVariableStore::player_values(PlayerID player_id) const noexcept {
  const auto found = players_.find(player_id);
  return found == players_.end() ? nullptr : &found->second;
}

std::optional<std::string_view> TextVariableResolver::resolve(
    std::string_view name) const {
  if (store == nullptr) {
    return std::nullopt;
  }
  return store->resolve(player_id, name, resolution);
}

bool valid_text_variable_name(std::string_view name) noexcept {
  return !name.empty() && name.size() <= MaxTextVariableNameBytes &&
         std::all_of(name.begin(), name.end(), text_variable_char);
}

bool is_reserved_builtin_text_variable(std::string_view name) noexcept {
  constexpr std::string_view reserved[] = {
      "online_player_count",
      "player_health",
      "player_max_health",
      "player_name",
      "tps",
      "world_name",
  };
  return std::find(std::begin(reserved), std::end(reserved), name) !=
         std::end(reserved);
}

std::string text_variable_placeholder(std::string_view name) {
  std::string placeholder;
  placeholder.reserve(name.size() + 2U);
  placeholder.push_back('{');
  placeholder.append(name);
  placeholder.push_back('}');
  return placeholder;
}

std::string interpolate_text_variables(
    std::string_view text,
    const TextVariableResolver& resolver) {
  std::string output;
  output.reserve(text.size());
  for (usize index = 0U; index < text.size();) {
    if (text[index] != '{') {
      output.push_back(text[index++]);
      continue;
    }

    const usize close = text.find('}', index + 1U);
    if (close == std::string_view::npos) {
      output.push_back(text[index++]);
      continue;
    }

    const std::string_view name = text.substr(index + 1U, close - index - 1U);
    if (std::optional<std::string_view> value = resolver.resolve(name);
        value.has_value()) {
      output.append(value.value());
    } else {
      output.append(text_variable_placeholder(name));
    }
    index = close + 1U;
  }
  return output;
}

RichText interpolate_rich_text_variables(
    const RichText& source,
    const TextVariableResolver& resolver) {
  RichText result;
  result.kind = source.kind;
  result.text = interpolate_text_variables(source.text, resolver);
  result.translation_key = source.translation_key;
  result.style = source.style;
  if (source.click_event.has_value()) {
    result.click_event = ClickEvent{
        source.click_event->action,
        interpolate_text_variables(source.click_event->value, resolver)};
  }
  if (source.hover_event.has_value()) {
    HoverEvent hover;
    hover.action = source.hover_event->action;
    hover.fallback_text =
        interpolate_text_variables(source.hover_event->fallback_text, resolver);
    if (source.hover_event->show_text != nullptr) {
      hover.show_text = std::make_shared<RichText>(
          interpolate_rich_text_variables(*source.hover_event->show_text, resolver));
    }
    result.hover_event = std::move(hover);
  }
  result.children.reserve(source.children.size());
  for (const RichText& child : source.children) {
    result.children.push_back(interpolate_rich_text_variables(child, resolver));
  }
  return result;
}

} // namespace arv::text
