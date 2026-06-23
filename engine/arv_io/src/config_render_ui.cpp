#include "config_internal.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace arv::io::detail {
namespace {

[[nodiscard]] std::optional<u8> hex_digit(char value) noexcept {
  if (value >= '0' && value <= '9') {
    return static_cast<u8>(value - '0');
  }
  if (value >= 'a' && value <= 'f') {
    return static_cast<u8>(value - 'a' + 10);
  }
  if (value >= 'A' && value <= 'F') {
    return static_cast<u8>(value - 'A' + 10);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<u8> hex_byte(std::string_view text, usize offset) noexcept {
  const std::optional<u8> high = hex_digit(text[offset]);
  const std::optional<u8> low = hex_digit(text[offset + 1U]);
  if (!high.has_value() || !low.has_value()) {
    return std::nullopt;
  }
  return static_cast<u8>((high.value() << 4U) | low.value());
}

[[nodiscard]] bool parse_hex_rgb(
    std::string_view text,
    f32& out_r,
    f32& out_g,
    f32& out_b) noexcept {
  if (!text.empty() && text.front() == '#') {
    text.remove_prefix(1U);
  }
  if (text.size() != 6U) {
    return false;
  }
  const std::optional<u8> r = hex_byte(text, 0U);
  const std::optional<u8> g = hex_byte(text, 2U);
  const std::optional<u8> b = hex_byte(text, 4U);
  if (!r.has_value() || !g.has_value() || !b.has_value()) {
    return false;
  }
  out_r = static_cast<f32>(r.value()) / 255.0F;
  out_g = static_cast<f32>(g.value()) / 255.0F;
  out_b = static_cast<f32>(b.value()) / 255.0F;
  return true;
}

[[nodiscard]] char hex_char(u8 value) noexcept {
  constexpr char Digits[] = "0123456789ABCDEF";
  return Digits[value & 0x0FU];
}

[[nodiscard]] u8 color_byte(f32 value) noexcept {
  const f32 clamped = std::clamp(value, 0.0F, 1.0F);
  return static_cast<u8>(std::lround(clamped * 255.0F));
}

[[nodiscard]] std::string hex_rgb(f32 r, f32 g, f32 b) {
  const u8 red = color_byte(r);
  const u8 green = color_byte(g);
  const u8 blue = color_byte(b);
  std::string text{"#000000"};
  text[1] = hex_char(static_cast<u8>(red >> 4U));
  text[2] = hex_char(red);
  text[3] = hex_char(static_cast<u8>(green >> 4U));
  text[4] = hex_char(green);
  text[5] = hex_char(static_cast<u8>(blue >> 4U));
  text[6] = hex_char(blue);
  return text;
}

} // namespace

void read_block_outline_config(
    const DataValue& render,
    RenderConfig::BlockOutlineConfig& config) {
  const DataValue* outline = render.get("block_outline");
  if (outline == nullptr) {
    return;
  }
  const DataValue* color = outline->get("color");
  if (color != nullptr && color->kind() == DataKind::String) {
    (void)parse_hex_rgb(
        color->as_string_or({}),
        config.color_r,
        config.color_g,
        config.color_b);
  }
  read_scalar(*outline, "thickness", config.thickness);
}

void read_render_sky_config(
    const DataValue& render,
    RenderConfig::SkyConfig& config) {
  const DataValue* sky = render.get("sky");
  if (sky == nullptr) {
    return;
  }
  read_scalar(*sky, "star_intensity", config.star_intensity);
  read_scalar(*sky, "cloud_intensity", config.cloud_intensity);
  read_scalar(*sky, "cirrus_amount", config.cirrus_amount);
  read_scalar(*sky, "cumulus_amount", config.cumulus_amount);
}

void read_render_shadows_config(
    const DataValue& render,
    RenderConfig::ShadowsConfig& config) {
  const DataValue* shadows = render.get("shadows");
  if (shadows == nullptr) {
    return;
  }
  read_scalar(*shadows, "enabled", config.enabled);
}

void read_render_atlases_config(
    const DataValue& render,
    RenderConfig::AtlasesConfig& config) {
  const DataValue* atlases = render.get("atlases");
  if (atlases == nullptr) {
    return;
  }
  const auto read_static_atlas = [&](const char* key, RenderConfig::StaticAtlasConfig& atlas_config) {
    const DataValue* atlas = atlases->get(key);
    if (atlas == nullptr) {
      return;
    }
    read_scalar(*atlas, "page_size", atlas_config.page_size);
    read_scalar(*atlas, "max_pages", atlas_config.max_pages);
  };
  read_static_atlas("blocks", config.blocks);
  read_static_atlas("entities", config.entities);
  read_static_atlas("ui", config.ui);
  read_static_atlas("fonts", config.fonts);
  read_static_atlas("particles", config.particles);

  const DataValue* player_skins = atlases->get("player_skins");
  if (player_skins == nullptr) {
    return;
  }
  read_scalar(*player_skins, "page_size", config.player_skins.page_size);
  read_scalar(*player_skins, "max_pages", config.player_skins.max_pages);

  const DataValue* cleanup = player_skins->get("cleanup");
  if (cleanup != nullptr) {
    read_scalar(*cleanup, "interval_seconds", config.player_skins.cleanup.interval_seconds);
    read_scalar(
        *cleanup,
        "evict_after_unused_seconds",
        config.player_skins.cleanup.evict_after_unused_seconds);
  }

  const DataValue* pressure = player_skins->get("pressure_cleanup");
  if (pressure != nullptr) {
    read_scalar(*pressure, "enabled", config.player_skins.pressure_cleanup.enabled);
    read_scalar(*pressure, "usage_threshold", config.player_skins.pressure_cleanup.usage_threshold);
    read_scalar(*pressure, "target_usage", config.player_skins.pressure_cleanup.target_usage);
  }
}

void read_ui_config(const DataValue& ui, UiConfig& config) {
  const DataValue* chat = ui.get("chat");
  if (chat != nullptr) {
    read_scalar(*chat, "max_history_messages", config.chat.max_history_messages);
    read_scalar(*chat, "passive_visible_seconds", config.chat.passive_visible_seconds);
    read_scalar(*chat, "passive_fade_seconds", config.chat.passive_fade_seconds);
    read_scalar(*chat, "passive_max_lines", config.chat.passive_max_lines);
    read_scalar(*chat, "open_max_lines", config.chat.open_max_lines);
  }
}

void put_render_sky_config(DataValue& root, const RenderConfig::SkyConfig& sky) {
  put(root, "render.sky.star_intensity", sky.star_intensity);
  put(root, "render.sky.cloud_intensity", sky.cloud_intensity);
  put(root, "render.sky.cirrus_amount", sky.cirrus_amount);
  put(root, "render.sky.cumulus_amount", sky.cumulus_amount);
}

void put_render_shadows_config(DataValue& root, const RenderConfig::ShadowsConfig& shadows) {
  put(root, "render.shadows.enabled", shadows.enabled);
}

void put_render_atlases_config(DataValue& root, const RenderConfig::AtlasesConfig& atlases) {
  const auto put_static_atlas = [&](std::string_view name, const RenderConfig::StaticAtlasConfig& atlas) {
    const std::string prefix = "render.atlases." + std::string{name};
    put(root, prefix + ".page_size", atlas.page_size);
    put(root, prefix + ".max_pages", atlas.max_pages);
  };
  put_static_atlas("blocks", atlases.blocks);
  put_static_atlas("entities", atlases.entities);
  put_static_atlas("ui", atlases.ui);
  put_static_atlas("fonts", atlases.fonts);
  put(root, "render.atlases.player_skins.page_size", atlases.player_skins.page_size);
  put(root, "render.atlases.player_skins.max_pages", atlases.player_skins.max_pages);
  put(root, "render.atlases.player_skins.cleanup.interval_seconds", atlases.player_skins.cleanup.interval_seconds);
  put(
      root,
      "render.atlases.player_skins.cleanup.evict_after_unused_seconds",
      atlases.player_skins.cleanup.evict_after_unused_seconds);
  put(root, "render.atlases.player_skins.pressure_cleanup.enabled", atlases.player_skins.pressure_cleanup.enabled);
  put(
      root,
      "render.atlases.player_skins.pressure_cleanup.usage_threshold",
      atlases.player_skins.pressure_cleanup.usage_threshold);
  put(
      root,
      "render.atlases.player_skins.pressure_cleanup.target_usage",
      atlases.player_skins.pressure_cleanup.target_usage);
  put_static_atlas("particles", atlases.particles);
}

void put_render_config(DataValue& root, const RenderConfig& render) {
  put(root, "render.max_chunk_mesh_uploads_per_frame", render.max_chunk_mesh_uploads_per_frame);
  put(root, "render.frame_pacing_enabled", render.frame_pacing_enabled);
  put(root, "render.frame_pacing_target_fps", render.frame_pacing_target_fps);
  put(root, "render.frame_pacing_spin_us", render.frame_pacing_spin_us);
  put(root, "render.show_local_player_body", render.show_local_player_body);

  put(
      root,
      "render.block_outline.color",
      hex_rgb(
          render.block_outline.color_r,
          render.block_outline.color_g,
          render.block_outline.color_b));
  put(root, "render.block_outline.thickness", render.block_outline.thickness);
  put_render_sky_config(root, render.sky);
  put_render_shadows_config(root, render.shadows);
  put_render_atlases_config(root, render.atlases);
}

void put_ui_config(DataValue& root, const UiConfig& ui) {
  put(root, "ui.chat.max_history_messages", ui.chat.max_history_messages);
  put(root, "ui.chat.passive_visible_seconds", ui.chat.passive_visible_seconds);
  put(root, "ui.chat.passive_fade_seconds", ui.chat.passive_fade_seconds);
  put(root, "ui.chat.passive_max_lines", ui.chat.passive_max_lines);
  put(root, "ui.chat.open_max_lines", ui.chat.open_max_lines);
}

void read_render_ui_sections(const DataValue& root, EngineConfig& config) {
  const DataValue* render = root.get("render");
  if (render != nullptr) {
    read_scalar(
        *render,
        "max_chunk_mesh_uploads_per_frame",
        config.render.max_chunk_mesh_uploads_per_frame);
    read_scalar(*render, "frame_pacing_enabled", config.render.frame_pacing_enabled);
    read_scalar(*render, "frame_pacing_target_fps", config.render.frame_pacing_target_fps);
    read_scalar(*render, "frame_pacing_spin_us", config.render.frame_pacing_spin_us);
    read_scalar(*render, "show_local_player_body", config.render.show_local_player_body);
    read_block_outline_config(*render, config.render.block_outline);
    read_render_sky_config(*render, config.render.sky);
    read_render_shadows_config(*render, config.render.shadows);
    read_render_atlases_config(*render, config.render.atlases);
  }

  const DataValue* ui = root.get("ui");
  if (ui != nullptr) {
    read_ui_config(*ui, config.ui);
  }
}

void put_render_ui_full(DataValue& root, const EngineConfig& config) {
  put_render_config(root, config.render);
  put_ui_config(root, config.ui);
}

void put_render_ui_client(DataValue& root, const EngineConfig& config) {
  put_render_config(root, config.render);
  put_ui_config(root, config.ui);
}

} // namespace arv::io::detail
