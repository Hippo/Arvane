#include "arv/ui/theme.hpp"

#include "arv/asset/pack.hpp"
#include "arv/asset/resource.hpp"
#include "arv/core/log.hpp"
#include "arv/io/data_codecs.hpp"
#include "arv/io/files.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::ui {
namespace {

constexpr std::string_view ConfigDirectoryName = "config";
constexpr std::string_view ThemesDirectoryName = "themes";
constexpr std::string_view StyleFileName = "style.yaml";
constexpr std::string_view ThemeResourcePrefix = "theme/";
constexpr std::string_view DefaultThemeAsset = "arvane:theme/default";

struct ThemeDocument {
  io::DataDocument root{};
  std::optional<std::string> inherits{};
};

[[nodiscard]] const io::DataValue* member(
    const io::DataValue& parent,
    std::string_view key) noexcept {
  return parent.get(key);
}

[[nodiscard]] bool is_object(const io::DataValue& value) noexcept {
  return value.kind() == io::DataKind::Object;
}

[[nodiscard]] bool is_string(const io::DataValue& value) noexcept {
  return value.kind() == io::DataKind::String;
}

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
    const io::DataValue& colors,
    const char* key,
    Vec4f& target,
    UiThemeLoadResult& result) {
  const io::DataValue* value = member(colors, key);
  if (value == nullptr) {
    return;
  }
  if (!is_string(*value)) {
    add_warning(result, std::string{"theme color is not a scalar: "} + key);
    return;
  }
  const std::string text{value->as_string_or({})};
  if (std::optional<Vec4f> color = parse_hex_color(text); color.has_value()) {
    target = color.value();
  } else {
    add_warning(result, std::string{"theme color is invalid: "} + key);
  }
}

void read_text_token(const io::DataValue& parent, const char* key, std::string& target) {
  const io::DataValue* value = member(parent, key);
  if (value != nullptr && is_string(*value)) {
    target = std::string{value->as_string_or({})};
  }
}

void read_number_token(
    const io::DataValue& parent,
    const char* key,
    f32& target,
    UiThemeLoadResult& result,
    f32 minimum,
    f32 maximum) {
  const io::DataValue* value = member(parent, key);
  if (value == nullptr) {
    return;
  }
  if (value->kind() != io::DataKind::I64 && value->kind() != io::DataKind::U64 &&
      value->kind() != io::DataKind::F64) {
    add_warning(result, std::string{"theme numeric token is invalid: "} + key);
    return;
  }
  const f32 parsed = static_cast<f32>(value->as_f64_or(target));
  if (!std::isfinite(parsed) || parsed < minimum || parsed > maximum) {
    add_warning(result, std::string{"theme numeric token is outside safe bounds: "} + key);
    return;
  }
  target = parsed;
}

void read_u32_token(
    const io::DataValue& parent,
    const char* key,
    u32& target,
    UiThemeLoadResult& result,
    u32 minimum,
    u32 maximum) {
  const io::DataValue* value = member(parent, key);
  if (value == nullptr) {
    return;
  }
  if (value->kind() != io::DataKind::I64 && value->kind() != io::DataKind::U64) {
    add_warning(result, std::string{"theme numeric token is invalid: "} + key);
    return;
  }
  const u32 parsed = static_cast<u32>(value->as_u64_or(target));
  if (parsed < minimum || parsed > maximum) {
    add_warning(result, std::string{"theme numeric token is outside safe bounds: "} + key);
    return;
  }
  target = parsed;
}

[[nodiscard]] f32 read_scale_preference(
    const io::DataValue& parent,
    const char* key,
    f32 current,
    UiThemeLoadResult& result,
    f32 minimum,
    f32 maximum) {
  const io::DataValue* value = member(parent, key);
  if (value == nullptr) {
    return current;
  }
  if (value->kind() != io::DataKind::I64 && value->kind() != io::DataKind::U64 &&
      value->kind() != io::DataKind::F64) {
    add_warning(result, std::string{"ui scale token is invalid: "} + key);
    return current;
  }
  const f32 parsed = static_cast<f32>(value->as_f64_or(current));
  if (!std::isfinite(parsed)) {
    add_warning(result, std::string{"ui scale token is invalid: "} + key);
    return current;
  }
  const f32 clamped = std::clamp(parsed, minimum, maximum);
  if (clamped != parsed) {
    add_warning(result, std::string{"ui scale token was clamped: "} + key);
  }
  return clamped;
}

[[nodiscard]] bool theme_stack_contains(
    const std::vector<std::string>& stack,
    std::string_view theme_id) noexcept {
  return std::any_of(
      stack.begin(),
      stack.end(),
      [theme_id](const std::string& entry) {
        return entry == theme_id;
      });
}

[[nodiscard]] std::string describe_theme_chain(
    const std::vector<std::string>& stack,
    std::string_view next) {
  std::string chain;
  for (const std::string& entry : stack) {
    if (!chain.empty()) {
      chain += " -> ";
    }
    chain += entry;
  }
  if (!chain.empty()) {
    chain += " -> ";
  }
  chain += next;
  return chain;
}

[[nodiscard]] std::filesystem::path with_yaml_extension(std::filesystem::path path) {
  if (path.extension().empty()) {
    path += ".yaml";
  }
  return path;
}

[[nodiscard]] std::optional<std::string> theme_asset_id_from_reference(
    std::string_view theme,
    UiThemeLoadResult& result,
    std::string_view context) {
  if (theme.find(':') != std::string_view::npos) {
    auto parsed = asset::ResourceID::parse(theme);
    if (!parsed || !parsed.value().path.starts_with(ThemeResourcePrefix)) {
      add_warning(
          result,
          "invalid inherited theme resource ID in " + std::string{context} + ": " +
              std::string{theme});
      return std::nullopt;
    }
    return parsed.value().string();
  }
  if (!valid_theme_name(theme)) {
    add_warning(
        result,
        "invalid inherited theme name in " + std::string{context} + ": " + std::string{theme});
    return std::nullopt;
  }
  return "arvane:theme/" + std::string{theme};
}

[[nodiscard]] std::optional<ThemeDocument> read_theme_document(
    const std::filesystem::path& path,
    UiThemeLoadResult& result) {
  auto text = io::read_text_file_contents(path);
  if (!text) {
    add_warning(result, "could not load theme file " + path.string() + ": " + text.error().message);
    return std::nullopt;
  }
  auto decoded = io::decode_yaml_value(text.value());
  if (!decoded) {
    add_warning(result, "could not load theme file " + path.string() + ": " + decoded.error().describe());
    return std::nullopt;
  }
  ThemeDocument document;
  document.root = std::move(decoded.value());
  const io::DataValue* inherits = member(document.root.root, "inherits");
  if (inherits != nullptr) {
    if (!is_string(*inherits)) {
      add_warning(result, "theme inherits is not a scalar: " + path.string());
      return std::nullopt;
    }
    document.inherits = std::string{inherits->as_string_or({})};
    if (document.inherits->empty()) {
      add_warning(result, "theme inherits is empty: " + path.string());
      return std::nullopt;
    }
  }
  return document;
}

void parse_theme_node(const io::DataValue& root, UiTheme& theme, UiThemeLoadResult& result) {
  if (const io::DataValue* name = member(root, "name"); name != nullptr && is_string(*name)) {
    theme.name = std::string{name->as_string_or({})};
  }
  if (const io::DataValue* description = member(root, "description");
      description != nullptr && is_string(*description)) {
    theme.description = std::string{description->as_string_or({})};
  }

  const io::DataValue* colors = member(root, "colors");
  if (colors != nullptr && is_object(*colors)) {
    read_color_token(*colors, "background", theme.colors.background, result);
    read_color_token(*colors, "backdrop", theme.colors.backdrop, result);
    read_color_token(*colors, "pause_backdrop", theme.colors.pause_backdrop, result);
    read_color_token(*colors, "surface", theme.colors.surface, result);
    read_color_token(*colors, "surface_alt", theme.colors.surface_alt, result);
    read_color_token(*colors, "surface_hover", theme.colors.surface_hover, result);
    read_color_token(*colors, "surface_pressed", theme.colors.surface_pressed, result);
    read_color_token(*colors, "surface_selected", theme.colors.surface_selected, result);
    read_color_token(*colors, "text", theme.colors.text, result);
    read_color_token(*colors, "text_muted", theme.colors.text_muted, result);
    read_color_token(*colors, "text_disabled", theme.colors.text_disabled, result);
    read_color_token(*colors, "border", theme.colors.border, result);
    read_color_token(*colors, "border_focus", theme.colors.border_focus, result);
    read_color_token(*colors, "accent", theme.colors.accent, result);
    read_color_token(*colors, "accent_hover", theme.colors.accent_hover, result);
    read_color_token(*colors, "accent_pressed", theme.colors.accent_pressed, result);
    read_color_token(*colors, "accent_text", theme.colors.accent_text, result);
    read_color_token(*colors, "danger", theme.colors.danger, result);
    read_color_token(*colors, "danger_hover", theme.colors.danger_hover, result);
    read_color_token(*colors, "danger_pressed", theme.colors.danger_pressed, result);
    read_color_token(*colors, "warning", theme.colors.warning, result);
    read_color_token(*colors, "success", theme.colors.success, result);
  }

  const io::DataValue* layout = member(root, "layout");
  if (layout != nullptr && is_object(*layout)) {
    read_number_token(*layout, "spacing_xs", theme.layout.spacing_xs, result, 0.0F, 96.0F);
    read_number_token(*layout, "spacing_sm", theme.layout.spacing_sm, result, 0.0F, 128.0F);
    read_number_token(*layout, "spacing_md", theme.layout.spacing_md, result, 0.0F, 160.0F);
    read_number_token(*layout, "spacing_lg", theme.layout.spacing_lg, result, 0.0F, 192.0F);
    read_number_token(*layout, "spacing_xl", theme.layout.spacing_xl, result, 0.0F, 256.0F);
    read_number_token(*layout, "radius_sm", theme.layout.radius_sm, result, 0.0F, 48.0F);
    read_number_token(*layout, "radius_md", theme.layout.radius_md, result, 0.0F, 64.0F);
    read_number_token(*layout, "radius_lg", theme.layout.radius_lg, result, 0.0F, 96.0F);
    read_number_token(*layout, "border_width", theme.layout.border_width, result, 0.0F, 8.0F);
  }

  const io::DataValue* typography = member(root, "typography");
  if (typography != nullptr && is_object(*typography)) {
    read_text_token(*typography, "default_font", theme.typography.default_font);
    read_text_token(*typography, "heading_font", theme.typography.heading_font);
    read_text_token(*typography, "monospace_font", theme.typography.monospace_font);
    read_u32_token(*typography, "size_sm", theme.typography.size_sm, result, 8U, 48U);
    read_u32_token(*typography, "size_md", theme.typography.size_md, result, 10U, 64U);
    read_u32_token(*typography, "size_lg", theme.typography.size_lg, result, 12U, 96U);
    read_u32_token(*typography, "size_xl", theme.typography.size_xl, result, 16U, 128U);
  }

  const io::DataValue* widgets = member(root, "widgets");
  if (widgets != nullptr && is_object(*widgets)) {
    const io::DataValue* button = member(*widgets, "button");
    if (button != nullptr && is_object(*button)) {
      read_number_token(*button, "height", theme.widgets.button.height, result, 24.0F, 120.0F);
      read_number_token(*button, "min_width", theme.widgets.button.min_width, result, 40.0F, 640.0F);
      read_number_token(*button, "padding_x", theme.widgets.button.padding_x, result, 0.0F, 96.0F);
      read_number_token(*button, "padding_y", theme.widgets.button.padding_y, result, 0.0F, 64.0F);
      read_number_token(*button, "radius", theme.widgets.button.radius, result, 0.0F, 64.0F);
    }
    const io::DataValue* input = member(*widgets, "input");
    if (input != nullptr && is_object(*input)) {
      read_number_token(*input, "height", theme.widgets.input.height, result, 24.0F, 120.0F);
      read_number_token(*input, "min_width", theme.widgets.input.min_width, result, 60.0F, 720.0F);
      read_number_token(*input, "padding_x", theme.widgets.input.padding_x, result, 0.0F, 96.0F);
      read_number_token(*input, "padding_y", theme.widgets.input.padding_y, result, 0.0F, 64.0F);
      read_number_token(*input, "radius", theme.widgets.input.radius, result, 0.0F, 64.0F);
    }
    const io::DataValue* panel = member(*widgets, "panel");
    if (panel != nullptr && is_object(*panel)) {
      read_number_token(*panel, "padding", theme.widgets.panel.padding, result, 0.0F, 128.0F);
      read_number_token(*panel, "radius", theme.widgets.panel.radius, result, 0.0F, 96.0F);
    }
  }
}

[[nodiscard]] std::optional<UiTheme> read_theme_asset(
    const asset::PackManager& packs,
    std::string theme_asset,
    UiThemeLoadResult& result,
    std::vector<std::string>& stack);

[[nodiscard]] std::optional<UiTheme> read_theme_path(
    const std::filesystem::path& path,
    std::string theme_id,
    const std::filesystem::path& themes_root,
    UiThemeLoadResult& result,
    std::vector<std::string>& stack);

[[nodiscard]] std::optional<UiTheme> read_theme_asset(
    const asset::PackManager& packs,
    std::string theme_asset,
    UiThemeLoadResult& result,
    std::vector<std::string>& stack) {
  if (theme_stack_contains(stack, theme_asset)) {
    add_warning(
        result,
        "cyclic theme inheritance detected: " + describe_theme_chain(stack, theme_asset));
    return std::nullopt;
  }

  auto resolved = packs.resolve_asset_path(theme_asset, asset::AssetKind::Theme);
  if (!resolved) {
    add_warning(result, "theme not found: " + resolved.error().describe());
    return std::nullopt;
  }

  stack.push_back(theme_asset);
  const std::optional<ThemeDocument> document = read_theme_document(resolved.value().path, result);
  if (!document.has_value()) {
    stack.pop_back();
    return std::nullopt;
  }

  UiTheme theme = make_default_ui_theme();
  if (document->inherits.has_value()) {
    std::optional<std::string> parent = theme_asset_id_from_reference(
        document->inherits.value(),
        result,
        resolved.value().path.string());
    if (!parent.has_value()) {
      stack.pop_back();
      return std::nullopt;
    }
    std::optional<UiTheme> inherited = read_theme_asset(packs, parent.value(), result, stack);
    if (!inherited.has_value()) {
      add_warning(result, "inherited theme could not be loaded: " + parent.value());
      stack.pop_back();
      return std::nullopt;
    }
    theme = std::move(inherited.value());
  }
  parse_theme_node(document->root.root, theme, result);
  stack.pop_back();
  return theme;
}

struct LocalThemeReference {
  std::string id{};
  std::filesystem::path path{};
};

[[nodiscard]] std::optional<LocalThemeReference> local_theme_reference(
    std::string_view theme,
    const std::filesystem::path& themes_root,
    UiThemeLoadResult& result,
    std::string_view context) {
  if (theme.find(':') != std::string_view::npos) {
    auto parsed = asset::ResourceID::parse(theme);
    if (!parsed || !parsed.value().path.starts_with(ThemeResourcePrefix)) {
      add_warning(
          result,
          "invalid inherited theme resource ID in " + std::string{context} + ": " +
              std::string{theme});
      return std::nullopt;
    }
    std::string logical = parsed.value().path.substr(ThemeResourcePrefix.size());
    std::filesystem::path path = themes_root / std::filesystem::path{logical};
    if (parsed.value().name_space != "arvane") {
      path = themes_root / std::filesystem::path{parsed.value().name_space} /
             std::filesystem::path{logical};
    }
    return LocalThemeReference{parsed.value().string(), with_yaml_extension(std::move(path))};
  }

  if (!valid_theme_name(theme)) {
    add_warning(
        result,
        "invalid inherited theme name in " + std::string{context} + ": " + std::string{theme});
    return std::nullopt;
  }

  std::string name{theme};
  return LocalThemeReference{name, themes_root / std::filesystem::path{name + ".yaml"}};
}

[[nodiscard]] std::optional<UiTheme> read_theme_path(
    const std::filesystem::path& path,
    std::string theme_id,
    const std::filesystem::path& themes_root,
    UiThemeLoadResult& result,
    std::vector<std::string>& stack) {
  if (theme_stack_contains(stack, theme_id)) {
    add_warning(
        result,
        "cyclic theme inheritance detected: " + describe_theme_chain(stack, theme_id));
    return std::nullopt;
  }
  if (!io::regular_file_exists(path)) {
    add_warning(result, "theme not found: " + path.string());
    return std::nullopt;
  }

  stack.push_back(std::move(theme_id));
  const std::optional<ThemeDocument> document = read_theme_document(path, result);
  if (!document.has_value()) {
    stack.pop_back();
    return std::nullopt;
  }

  UiTheme theme = make_default_ui_theme();
  if (document->inherits.has_value()) {
    std::optional<LocalThemeReference> parent =
        local_theme_reference(document->inherits.value(), themes_root, result, path.string());
    if (!parent.has_value()) {
      stack.pop_back();
      return std::nullopt;
    }
    std::optional<UiTheme> inherited =
        read_theme_path(parent->path, parent->id, themes_root, result, stack);
    if (!inherited.has_value()) {
      add_warning(result, "inherited theme could not be loaded: " + parent->id);
      stack.pop_back();
      return std::nullopt;
    }
    theme = std::move(inherited.value());
  }
  parse_theme_node(document->root.root, theme, result);
  stack.pop_back();
  return theme;
}

void parse_style_file(const std::filesystem::path& path, UiThemeLoadResult& result) {
  auto text = io::read_text_file_contents(path);
  if (!text) {
    add_warning(result, "could not load style config " + path.string() + ": " + text.error().message);
    return;
  }
  auto decoded = io::decode_yaml_value(text.value());
  if (!decoded) {
    add_warning(result, "could not load style config " + path.string() + ": " + decoded.error().describe());
    return;
  }
  const io::DataValue& root = decoded.value().root;
  if (const io::DataValue* theme = member(root, "theme"); theme != nullptr && is_string(*theme)) {
    result.config.theme = std::string{theme->as_string_or({})};
  }
  const io::DataValue* ui = member(root, "ui");
  if (ui != nullptr && is_object(*ui)) {
    result.config.ui.scale = read_scale_preference(
        *ui,
        "scale",
        result.config.ui.scale,
        result,
        0.25F,
        4.0F);
    result.config.ui.font_scale = read_scale_preference(
        *ui,
        "font_scale",
        result.config.ui.font_scale,
        result,
        0.25F,
        4.0F);
    result.config.ui.animation_scale = read_scale_preference(
        *ui,
        "animation_scale",
        result.config.ui.animation_scale,
        result,
        0.0F,
        4.0F);
  }
}

[[nodiscard]] std::string theme_asset_id_from_config(
    std::string_view theme,
    UiThemeLoadResult& result) {
  if (theme.find(':') != std::string_view::npos) {
    auto parsed = asset::ResourceID::parse(theme);
    if (!parsed || !parsed.value().path.starts_with("theme/")) {
      add_warning(result, "invalid theme resource ID, falling back to default: " + std::string{theme});
      result.used_theme_fallback = true;
      result.config.theme = "default";
      return std::string{DefaultThemeAsset};
    }
    return std::string{theme};
  }
  if (!valid_theme_name(theme)) {
    add_warning(result, "invalid theme name, falling back to default: " + std::string{theme});
    result.config.theme = "default";
    result.used_theme_fallback = true;
    return std::string{DefaultThemeAsset};
  }
  return "arvane:theme/" + std::string{theme};
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

UiThemeLoadResult load_ui_theme(
    const asset::PackManager& packs,
    const std::filesystem::path& style_file) {
  UiThemeLoadResult result;
  result.config = make_default_ui_style_config();
  result.theme = make_default_ui_theme();

  if (io::regular_file_exists(style_file)) {
    parse_style_file(style_file, result);
  } else {
    add_warning(result, "style config not found, using default theme: " + style_file.string());
  }

  std::string theme_asset = theme_asset_id_from_config(result.config.theme, result);
  std::optional<UiTheme> loaded;
  std::vector<std::string> theme_stack;
  loaded = read_theme_asset(packs, theme_asset, result, theme_stack);
  if (loaded.has_value()) {
    result.loaded_theme = theme_asset;
  } else {
    add_warning(
        result,
        "selected theme could not be loaded, falling back to default: " + theme_asset);
  }

  if (!loaded.has_value() && theme_asset != DefaultThemeAsset) {
    result.used_theme_fallback = true;
    theme_asset = std::string{DefaultThemeAsset};
    theme_stack.clear();
    loaded = read_theme_asset(packs, theme_asset, result, theme_stack);
    if (loaded.has_value()) {
      result.loaded_theme = theme_asset;
    } else {
      add_warning(
          result,
          "default theme could not be loaded, using built-in theme: " + theme_asset);
    }
  }

  if (loaded.has_value()) {
    result.theme = loaded.value();
  } else {
    result.used_builtin_theme = true;
    result.loaded_theme = "built-in";
  }
  apply_preferences(result.theme, result.config.ui);
  return result;
}

UiThemeLoadResult load_ui_theme_from_paths(
    const std::filesystem::path& style_file,
    const std::filesystem::path& themes_root) {
  UiThemeLoadResult result;
  result.config = make_default_ui_style_config();
  result.theme = make_default_ui_theme();

  if (io::regular_file_exists(style_file)) {
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
  std::vector<std::string> theme_stack;
  if (io::regular_file_exists(selected_path)) {
    loaded = read_theme_path(
        selected_path,
        result.config.theme,
        themes_root,
        result,
        theme_stack);
  } else {
    add_warning(
        result,
        "selected theme not found, falling back to default: " + selected_path.string());
  }

  if (!loaded.has_value() && result.config.theme != "default") {
    result.used_theme_fallback = true;
    const std::filesystem::path default_path = themes_root / "default.yaml";
    theme_stack.clear();
    if (io::regular_file_exists(default_path)) {
      loaded = read_theme_path(default_path, "default", themes_root, result, theme_stack);
      result.loaded_theme = "default";
    } else {
      add_warning(
          result,
          "default theme not found, using built-in theme: " + default_path.string());
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
