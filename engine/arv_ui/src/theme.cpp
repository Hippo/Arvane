#include "arv/ui/theme.hpp"

#include "arv/core/log.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <system_error>

#include <yaml-cpp/yaml.h>

namespace arv::ui {
namespace {

constexpr std::string_view ConfigDirectoryName = "config";
constexpr std::string_view ThemesDirectoryName = "themes";
constexpr std::string_view StyleFileName = "style.yaml";

[[nodiscard]] f32 clamp_scale(f32 value, f32 fallback) noexcept {
  if (!std::isfinite(value)) {
    return fallback;
  }
  return std::clamp(value, 0.25F, 4.0F);
}

void add_warning(UiThemeLoadResult& result, std::string message) {
  core::warn("ui/theme", message);
  result.warnings.push_back(std::move(message));
}

[[nodiscard]] std::optional<u8> hex_byte(std::string_view text) noexcept {
  if (text.size() != 2U) {
    return std::nullopt;
  }
  u32 value = 0;
  const char* first = text.data();
  const char* last = text.data() + text.size();
  const auto parsed = std::from_chars(first, last, value, 16);
  if (parsed.ec != std::errc{} || parsed.ptr != last || value > 255U) {
    return std::nullopt;
  }
  return static_cast<u8>(value);
}

[[nodiscard]] std::optional<Vec4f> parse_hex_color(std::string_view text) noexcept {
  if (text.size() != 7U && text.size() != 9U) {
    return std::nullopt;
  }
  if (text.front() != '#') {
    return std::nullopt;
  }
  const std::optional<u8> r = hex_byte(text.substr(1U, 2U));
  const std::optional<u8> g = hex_byte(text.substr(3U, 2U));
  const std::optional<u8> b = hex_byte(text.substr(5U, 2U));
  const std::optional<u8> a = text.size() == 9U ? hex_byte(text.substr(7U, 2U))
                                                : std::optional<u8>{static_cast<u8>(255U)};
  if (!r.has_value() || !g.has_value() || !b.has_value() || !a.has_value()) {
    return std::nullopt;
  }
  constexpr f32 inv = 1.0F / 255.0F;
  return Vec4f{
      static_cast<f32>(r.value()) * inv,
      static_cast<f32>(g.value()) * inv,
      static_cast<f32>(b.value()) * inv,
      static_cast<f32>(a.value()) * inv};
}

void read_color_token(
    const YAML::Node& colors,
    const char* key,
    Vec4f& target,
    UiThemeLoadResult& result) {
  const YAML::Node value = colors[key];
  if (!value) {
    return;
  }
  if (!value.IsScalar()) {
    add_warning(result, std::string{"theme color is not a scalar: "} + key);
    return;
  }
  const std::string text = value.as<std::string>();
  if (std::optional<Vec4f> color = parse_hex_color(text); color.has_value()) {
    target = color.value();
  } else {
    add_warning(result, std::string{"theme color is invalid: "} + key);
  }
}

template<typename T>
void read_text_token(const YAML::Node& parent, const char* key, T& target) {
  const YAML::Node value = parent[key];
  if (value && value.IsScalar()) {
    target = value.as<T>();
  }
}

void read_number_token(
    const YAML::Node& parent,
    const char* key,
    f32& target,
    UiThemeLoadResult& result,
    f32 minimum,
    f32 maximum) {
  const YAML::Node value = parent[key];
  if (!value) {
    return;
  }
  try {
    const f32 parsed = value.as<f32>();
    if (!std::isfinite(parsed) || parsed < minimum || parsed > maximum) {
      add_warning(result, std::string{"theme numeric token is outside safe bounds: "} + key);
      return;
    }
    target = parsed;
  } catch (const YAML::Exception&) {
    add_warning(result, std::string{"theme numeric token is invalid: "} + key);
  }
}

void read_u32_token(
    const YAML::Node& parent,
    const char* key,
    u32& target,
    UiThemeLoadResult& result,
    u32 minimum,
    u32 maximum) {
  const YAML::Node value = parent[key];
  if (!value) {
    return;
  }
  try {
    const u32 parsed = value.as<u32>();
    if (parsed < minimum || parsed > maximum) {
      add_warning(result, std::string{"theme numeric token is outside safe bounds: "} + key);
      return;
    }
    target = parsed;
  } catch (const YAML::Exception&) {
    add_warning(result, std::string{"theme numeric token is invalid: "} + key);
  }
}

[[nodiscard]] f32 read_scale_preference(
    const YAML::Node& parent,
    const char* key,
    f32 current,
    UiThemeLoadResult& result,
    f32 minimum,
    f32 maximum) {
  const YAML::Node value = parent[key];
  if (!value) {
    return current;
  }
  try {
    const f32 parsed = value.as<f32>();
    if (!std::isfinite(parsed)) {
      add_warning(result, std::string{"ui scale token is invalid: "} + key);
      return current;
    }
    const f32 clamped = std::clamp(parsed, minimum, maximum);
    if (clamped != parsed) {
      add_warning(result, std::string{"ui scale token was clamped: "} + key);
    }
    return clamped;
  } catch (const YAML::Exception&) {
    add_warning(result, std::string{"ui scale token is invalid: "} + key);
    return current;
  }
}

void parse_theme_node(const YAML::Node& root, UiTheme& theme, UiThemeLoadResult& result) {
  if (const YAML::Node name = root["name"]; name && name.IsScalar()) {
    theme.name = name.as<std::string>();
  }
  if (const YAML::Node description = root["description"]; description && description.IsScalar()) {
    theme.description = description.as<std::string>();
  }

  const YAML::Node colors = root["colors"];
  if (colors && colors.IsMap()) {
    read_color_token(colors, "background", theme.colors.background, result);
    read_color_token(colors, "backdrop", theme.colors.backdrop, result);
    read_color_token(colors, "pause_backdrop", theme.colors.pause_backdrop, result);
    read_color_token(colors, "surface", theme.colors.surface, result);
    read_color_token(colors, "surface_alt", theme.colors.surface_alt, result);
    read_color_token(colors, "surface_hover", theme.colors.surface_hover, result);
    read_color_token(colors, "surface_pressed", theme.colors.surface_pressed, result);
    read_color_token(colors, "surface_selected", theme.colors.surface_selected, result);
    read_color_token(colors, "text", theme.colors.text, result);
    read_color_token(colors, "text_muted", theme.colors.text_muted, result);
    read_color_token(colors, "text_disabled", theme.colors.text_disabled, result);
    read_color_token(colors, "border", theme.colors.border, result);
    read_color_token(colors, "border_focus", theme.colors.border_focus, result);
    read_color_token(colors, "accent", theme.colors.accent, result);
    read_color_token(colors, "accent_hover", theme.colors.accent_hover, result);
    read_color_token(colors, "accent_pressed", theme.colors.accent_pressed, result);
    read_color_token(colors, "accent_text", theme.colors.accent_text, result);
    read_color_token(colors, "danger", theme.colors.danger, result);
    read_color_token(colors, "danger_hover", theme.colors.danger_hover, result);
    read_color_token(colors, "danger_pressed", theme.colors.danger_pressed, result);
    read_color_token(colors, "warning", theme.colors.warning, result);
    read_color_token(colors, "success", theme.colors.success, result);
  }

  const YAML::Node layout = root["layout"];
  if (layout && layout.IsMap()) {
    read_number_token(layout, "spacing_xs", theme.layout.spacing_xs, result, 0.0F, 96.0F);
    read_number_token(layout, "spacing_sm", theme.layout.spacing_sm, result, 0.0F, 128.0F);
    read_number_token(layout, "spacing_md", theme.layout.spacing_md, result, 0.0F, 160.0F);
    read_number_token(layout, "spacing_lg", theme.layout.spacing_lg, result, 0.0F, 192.0F);
    read_number_token(layout, "spacing_xl", theme.layout.spacing_xl, result, 0.0F, 256.0F);
    read_number_token(layout, "radius_sm", theme.layout.radius_sm, result, 0.0F, 48.0F);
    read_number_token(layout, "radius_md", theme.layout.radius_md, result, 0.0F, 64.0F);
    read_number_token(layout, "radius_lg", theme.layout.radius_lg, result, 0.0F, 96.0F);
    read_number_token(layout, "border_width", theme.layout.border_width, result, 0.0F, 8.0F);
  }

  const YAML::Node typography = root["typography"];
  if (typography && typography.IsMap()) {
    read_text_token(typography, "default_font", theme.typography.default_font);
    read_text_token(typography, "heading_font", theme.typography.heading_font);
    read_text_token(typography, "monospace_font", theme.typography.monospace_font);
    read_u32_token(typography, "size_sm", theme.typography.size_sm, result, 8U, 48U);
    read_u32_token(typography, "size_md", theme.typography.size_md, result, 10U, 64U);
    read_u32_token(typography, "size_lg", theme.typography.size_lg, result, 12U, 96U);
    read_u32_token(typography, "size_xl", theme.typography.size_xl, result, 16U, 128U);
  }

  const YAML::Node widgets = root["widgets"];
  if (widgets && widgets.IsMap()) {
    const YAML::Node button = widgets["button"];
    if (button && button.IsMap()) {
      read_number_token(button, "height", theme.widgets.button.height, result, 24.0F, 120.0F);
      read_number_token(button, "min_width", theme.widgets.button.min_width, result, 40.0F, 640.0F);
      read_number_token(button, "padding_x", theme.widgets.button.padding_x, result, 0.0F, 96.0F);
      read_number_token(button, "padding_y", theme.widgets.button.padding_y, result, 0.0F, 64.0F);
      read_number_token(button, "radius", theme.widgets.button.radius, result, 0.0F, 64.0F);
    }
    const YAML::Node input = widgets["input"];
    if (input && input.IsMap()) {
      read_number_token(input, "height", theme.widgets.input.height, result, 24.0F, 120.0F);
      read_number_token(input, "min_width", theme.widgets.input.min_width, result, 60.0F, 720.0F);
      read_number_token(input, "padding_x", theme.widgets.input.padding_x, result, 0.0F, 96.0F);
      read_number_token(input, "padding_y", theme.widgets.input.padding_y, result, 0.0F, 64.0F);
      read_number_token(input, "radius", theme.widgets.input.radius, result, 0.0F, 64.0F);
    }
    const YAML::Node panel = widgets["panel"];
    if (panel && panel.IsMap()) {
      read_number_token(panel, "padding", theme.widgets.panel.padding, result, 0.0F, 128.0F);
      read_number_token(panel, "radius", theme.widgets.panel.radius, result, 0.0F, 96.0F);
    }
  }
}

[[nodiscard]] std::optional<UiTheme> read_theme_file(
    const std::filesystem::path& path,
    UiThemeLoadResult& result) {
  try {
    YAML::Node root = YAML::LoadFile(path.string());
    UiTheme theme = make_default_ui_theme();
    parse_theme_node(root, theme, result);
    return theme;
  } catch (const YAML::Exception& error) {
    add_warning(result, "could not load theme file " + path.string() + ": " + error.what());
  } catch (const std::exception& error) {
    add_warning(result, "could not load theme file " + path.string() + ": " + error.what());
  }
  return std::nullopt;
}

void parse_style_file(const std::filesystem::path& path, UiThemeLoadResult& result) {
  try {
    const YAML::Node root = YAML::LoadFile(path.string());
    if (const YAML::Node theme = root["theme"]; theme && theme.IsScalar()) {
      result.config.theme = theme.as<std::string>();
    }
    const YAML::Node ui = root["ui"];
    if (ui && ui.IsMap()) {
      result.config.ui.scale = read_scale_preference(
          ui,
          "scale",
          result.config.ui.scale,
          result,
          0.25F,
          4.0F);
      result.config.ui.font_scale = read_scale_preference(
          ui,
          "font_scale",
          result.config.ui.font_scale,
          result,
          0.25F,
          4.0F);
      result.config.ui.animation_scale = read_scale_preference(
          ui,
          "animation_scale",
          result.config.ui.animation_scale,
          result,
          0.0F,
          4.0F);
    }
  } catch (const YAML::Exception& error) {
    add_warning(result, "could not load style config " + path.string() + ": " + error.what());
  } catch (const std::exception& error) {
    add_warning(result, "could not load style config " + path.string() + ": " + error.what());
  }
}

[[nodiscard]] bool file_exists(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error);
}

void apply_preferences(UiTheme& theme, const UiStylePreferences& preferences) noexcept {
  const f32 scale = clamp_scale(preferences.scale, 1.0F);
  const f32 font_scale = clamp_scale(preferences.font_scale, 1.0F);
  theme.layout.spacing_xs *= scale;
  theme.layout.spacing_sm *= scale;
  theme.layout.spacing_md *= scale;
  theme.layout.spacing_lg *= scale;
  theme.layout.spacing_xl *= scale;
  theme.layout.radius_sm *= scale;
  theme.layout.radius_md *= scale;
  theme.layout.radius_lg *= scale;
  theme.layout.border_width *= scale;
  theme.widgets.button.height *= scale;
  theme.widgets.button.min_width *= scale;
  theme.widgets.button.padding_x *= scale;
  theme.widgets.button.padding_y *= scale;
  theme.widgets.button.radius *= scale;
  theme.widgets.input.height *= scale;
  theme.widgets.input.min_width *= scale;
  theme.widgets.input.padding_x *= scale;
  theme.widgets.input.padding_y *= scale;
  theme.widgets.input.radius *= scale;
  theme.widgets.panel.padding *= scale;
  theme.widgets.panel.radius *= scale;
  theme.typography.size_sm = std::clamp<u32>(
      static_cast<u32>(std::lround(static_cast<f32>(theme.typography.size_sm) * font_scale)),
      8U,
      96U);
  theme.typography.size_md = std::clamp<u32>(
      static_cast<u32>(std::lround(static_cast<f32>(theme.typography.size_md) * font_scale)),
      10U,
      128U);
  theme.typography.size_lg = std::clamp<u32>(
      static_cast<u32>(std::lround(static_cast<f32>(theme.typography.size_lg) * font_scale)),
      12U,
      160U);
  theme.typography.size_xl = std::clamp<u32>(
      static_cast<u32>(std::lround(static_cast<f32>(theme.typography.size_xl) * font_scale)),
      16U,
      192U);
}

} // namespace

const UiTheme& default_ui_theme() noexcept {
  static const UiTheme theme = make_default_ui_theme();
  return theme;
}

UiTheme make_default_ui_theme() {
  UiTheme theme;
  theme.name = "Default";
  theme.description = "Grounded clean default Arvane theme.";
  return theme;
}

UiStyleConfig make_default_ui_style_config() {
  return {};
}

bool valid_theme_name(std::string_view name) noexcept {
  if (name.empty() || name.ends_with(".yaml")) {
    return false;
  }
  for (const char ch : name) {
    const bool allowed =
        (ch >= 'a' && ch <= 'z') ||
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') ||
        ch == '_' ||
        ch == '-';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

UiThemeLoadResult load_ui_theme(const std::filesystem::path& arvane_home) {
  return load_ui_theme_from_paths(
      arvane_home / std::filesystem::path{ConfigDirectoryName} /
          std::filesystem::path{StyleFileName},
      arvane_home / std::filesystem::path{ThemesDirectoryName});
}

UiThemeLoadResult load_ui_theme_from_paths(
    const std::filesystem::path& style_file,
    const std::filesystem::path& themes_root) {
  UiThemeLoadResult result;
  result.config = make_default_ui_style_config();
  result.theme = make_default_ui_theme();

  if (file_exists(style_file)) {
    parse_style_file(style_file, result);
  } else {
    add_warning(result, "style config not found, using default theme: " + style_file.string());
  }

  if (!valid_theme_name(result.config.theme)) {
    add_warning(result, "invalid theme name, falling back to default: " + result.config.theme);
    result.config.theme = "default";
    result.used_theme_fallback = true;
  }

  const std::filesystem::path selected_path =
      themes_root / std::filesystem::path{result.config.theme + ".yaml"};
  std::optional<UiTheme> loaded;
  if (file_exists(selected_path)) {
    loaded = read_theme_file(selected_path, result);
  } else {
    add_warning(result, "selected theme not found, falling back to default: " + selected_path.string());
  }

  if (!loaded.has_value() && result.config.theme != "default") {
    result.used_theme_fallback = true;
    const std::filesystem::path default_path = themes_root / "default.yaml";
    if (file_exists(default_path)) {
      loaded = read_theme_file(default_path, result);
      result.loaded_theme = "default";
    } else {
      add_warning(result, "default theme not found, using built-in theme: " + default_path.string());
    }
  }

  if (loaded.has_value()) {
    result.theme = loaded.value();
    if (result.loaded_theme == "built-in") {
      result.loaded_theme = result.config.theme;
    }
  } else {
    result.used_builtin_theme = true;
    result.loaded_theme = "built-in";
  }
  apply_preferences(result.theme, result.config.ui);

  return result;
}

const UiTheme& theme_or_default(const UiTheme* theme) noexcept {
  return theme == nullptr ? default_ui_theme() : *theme;
}

} // namespace arv::ui
