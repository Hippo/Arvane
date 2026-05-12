#pragma once

#include "arv/core/math.hpp"
#include "arv/core/types.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace arv::asset {
class PackManager;
} // namespace arv::asset

namespace arv::ui {

enum class UiTextRole : u8 {
  Small,
  Body,
  Heading,
  Title,
  Mono,
};

enum class UiControlRole : u8 {
  Neutral,
  Primary,
  Danger,
};

struct UiThemeColors {
  Vec4f background{0.055F, 0.067F, 0.082F, 1.0F};
  Vec4f backdrop{0.012F, 0.014F, 0.018F, 0.90F};
  Vec4f pause_backdrop{0.010F, 0.012F, 0.016F, 0.66F};
  Vec4f surface{0.092F, 0.108F, 0.130F, 0.96F};
  Vec4f surface_alt{0.060F, 0.073F, 0.090F, 0.95F};
  Vec4f surface_hover{0.125F, 0.148F, 0.176F, 0.98F};
  Vec4f surface_pressed{0.052F, 0.064F, 0.080F, 0.98F};
  Vec4f surface_selected{0.130F, 0.162F, 0.190F, 0.99F};
  Vec4f text{0.930F, 0.945F, 0.960F, 1.0F};
  Vec4f text_muted{0.660F, 0.700F, 0.740F, 1.0F};
  Vec4f text_disabled{0.430F, 0.470F, 0.520F, 1.0F};
  Vec4f border{0.185F, 0.220F, 0.270F, 1.0F};
  Vec4f border_focus{0.420F, 0.650F, 0.930F, 1.0F};
  Vec4f accent{0.330F, 0.560F, 0.860F, 1.0F};
  Vec4f accent_hover{0.430F, 0.640F, 0.940F, 1.0F};
  Vec4f accent_pressed{0.230F, 0.420F, 0.720F, 1.0F};
  Vec4f accent_text{0.965F, 0.980F, 0.990F, 1.0F};
  Vec4f danger{0.760F, 0.220F, 0.190F, 1.0F};
  Vec4f danger_hover{0.880F, 0.300F, 0.260F, 1.0F};
  Vec4f danger_pressed{0.560F, 0.140F, 0.130F, 1.0F};
  Vec4f warning{0.880F, 0.630F, 0.300F, 1.0F};
  Vec4f success{0.280F, 0.700F, 0.470F, 1.0F};
};

struct UiThemeLayout {
  f32 spacing_xs{4.0F};
  f32 spacing_sm{8.0F};
  f32 spacing_md{12.0F};
  f32 spacing_lg{16.0F};
  f32 spacing_xl{24.0F};
  f32 radius_sm{4.0F};
  f32 radius_md{8.0F};
  f32 radius_lg{12.0F};
  f32 border_width{1.0F};
};

struct UiThemeTypography {
  std::string default_font{"arvane:font/font"};
  std::string heading_font{"arvane:font/font"};
  std::string monospace_font{"arvane:font/font"};
  u32 size_sm{14};
  u32 size_md{18};
  u32 size_lg{22};
  u32 size_xl{34};
};

struct UiThemeButtonStyle {
  f32 height{44.0F};
  f32 min_width{148.0F};
  f32 padding_x{16.0F};
  f32 padding_y{10.0F};
  f32 radius{8.0F};
};

struct UiThemeInputStyle {
  f32 height{44.0F};
  f32 min_width{300.0F};
  f32 padding_x{14.0F};
  f32 padding_y{10.0F};
  f32 radius{8.0F};
};

struct UiThemePanelStyle {
  f32 padding{20.0F};
  f32 radius{12.0F};
};

struct UiThemeWidgets {
  UiThemeButtonStyle button{};
  UiThemeInputStyle input{};
  UiThemePanelStyle panel{};
};

struct UiTheme {
  std::string name{"Default"};
  std::string description{"Built-in Arvane theme."};
  UiThemeColors colors{};
  UiThemeLayout layout{};
  UiThemeTypography typography{};
  UiThemeWidgets widgets{};
};

struct UiStylePreferences {
  f32 scale{1.0F};
  f32 font_scale{1.0F};
  f32 animation_scale{1.0F};
};

struct UiStyleConfig {
  std::string theme{"default"};
  UiStylePreferences ui{};
};

struct UiThemeLoadResult {
  UiStyleConfig config{};
  UiTheme theme{};
  std::string loaded_theme{"built-in"};
  bool used_theme_fallback{};
  bool used_builtin_theme{};
  std::vector<std::string> warnings{};
};

[[nodiscard]] const UiTheme& default_ui_theme() noexcept;
[[nodiscard]] UiTheme make_default_ui_theme();
[[nodiscard]] UiStyleConfig make_default_ui_style_config();
[[nodiscard]] bool valid_theme_name(std::string_view name) noexcept;
[[nodiscard]] UiThemeLoadResult load_ui_theme(const std::filesystem::path& arvane_home);
[[nodiscard]] UiThemeLoadResult load_ui_theme(
    const asset::PackManager& packs,
    const std::filesystem::path& style_file);
[[nodiscard]] UiThemeLoadResult load_ui_theme_from_paths(
    const std::filesystem::path& style_file,
    const std::filesystem::path& themes_root);

[[nodiscard]] const UiTheme& theme_or_default(const UiTheme* theme) noexcept;

} // namespace arv::ui
