#include "arv/base/ui/screens/settings.hpp"

#include "menu_common.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arv::ui {
namespace {

using namespace screens_detail;

inline constexpr f32 SettingsPanelWidth = 800.0F;
inline constexpr f32 SettingsPanelHeight = 560.0F;
inline constexpr f32 SettingsContentWidth = 736.0F;
inline constexpr f32 SettingsViewportHeight = 402.0F;
inline constexpr f32 SettingsRowHeight = 48.0F;
inline constexpr f32 SettingsRowSpacing = 8.0F;
inline constexpr f32 SettingsRowInsetX = 18.0F;
inline constexpr f32 KeyBindRowHeight = 58.0F;
inline constexpr i32 MaxConfigurableChunkDistance = 32;

struct CaptureTarget {
  std::string action_name{};
  u8 slot{};
};

struct SliderSpec {
  f32 min{};
  f32 max{};
  f32 step{};
};

struct SwitchAnimation {
  bool initialized{};
  bool target{};
  f32 start{};
  f32 current{};
  std::chrono::steady_clock::time_point start_time{};
};

[[nodiscard]] std::string fixed_decimal(f32 value, int precision) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(precision) << value;
  return output.str();
}

[[nodiscard]] std::string percent_text(f32 value) {
  return fixed_decimal(value, value < 100.0F ? 1 : 0) + "%";
}

[[nodiscard]] RenderSettingsView render_settings_from(const UIContext& ctx) {
  return ctx.data_source == nullptr ? RenderSettingsView{} : ctx.data_source->render_settings();
}

[[nodiscard]] ControlsSettingsView controls_settings_from(const UIContext& ctx) {
  return ctx.data_source == nullptr ? ControlsSettingsView{} : ctx.data_source->controls_settings();
}

[[nodiscard]] InterfaceSettingsView interface_settings_from(const UIContext& ctx) {
  return ctx.data_source == nullptr ? InterfaceSettingsView{} : ctx.data_source->interface_settings();
}

[[nodiscard]] std::vector<KeyBindEntryView> key_bind_entries_from(const UIContext& ctx) {
  return ctx.data_source == nullptr ? std::vector<KeyBindEntryView>{}
                                    : ctx.data_source->key_bind_entries();
}

[[nodiscard]] f32 settings_content_height(usize row_count, f32 row_height) noexcept {
  if (row_count == 0U) {
    return 0.0F;
  }
  return static_cast<f32>(row_count) * row_height +
         static_cast<f32>(row_count - 1U) * SettingsRowSpacing;
}

[[nodiscard]] f32 settings_max_scroll(usize row_count, f32 row_height) noexcept {
  return std::max(0.0F, settings_content_height(row_count, row_height) - SettingsViewportHeight);
}

[[nodiscard]] f32 ease_out_quart(f32 t) noexcept {
  const f32 inverse = 1.0F - std::clamp(t, 0.0F, 1.0F);
  return 1.0F - inverse * inverse * inverse * inverse;
}

[[nodiscard]] Vec4f mix_color(Vec4f from, Vec4f to, f32 t) noexcept {
  const f32 amount = std::clamp(t, 0.0F, 1.0F);
  return {
      from.x + (to.x - from.x) * amount,
      from.y + (to.y - from.y) * amount,
      from.z + (to.z - from.z) * amount,
      from.w + (to.w - from.w) * amount};
}

[[nodiscard]] f32 animated_switch_progress(
    bool target,
    SwitchAnimation& state,
    UIContext& ctx) {
  constexpr auto Duration = std::chrono::milliseconds{180};
  const auto now = std::chrono::steady_clock::now();
  const f32 target_value = target ? 1.0F : 0.0F;
  if (!state.initialized) {
    state.initialized = true;
    state.target = target;
    state.start = target_value;
    state.current = target_value;
    state.start_time = now;
    return state.current;
  }
  if (state.target != target) {
    state.target = target;
    state.start = state.current;
    state.start_time = now;
  }

  const auto elapsed = now - state.start_time;
  const f32 fraction =
      std::clamp(
          std::chrono::duration<f32>(elapsed).count() /
              std::chrono::duration<f32>(Duration).count(),
          0.0F,
          1.0F);
  state.current = state.start + (target_value - state.start) * ease_out_quart(fraction);
  if (fraction < 1.0F && ctx.screens != nullptr) {
    ctx.screens->mark_dirty();
  }
  return state.current;
}

[[nodiscard]] f32 slider_fraction(f32 value, SliderSpec spec) noexcept {
  if (spec.max <= spec.min) {
    return 0.0F;
  }
  return std::clamp((value - spec.min) / (spec.max - spec.min), 0.0F, 1.0F);
}

[[nodiscard]] f32 snap_slider_value(f32 value, SliderSpec spec) noexcept {
  if (spec.step <= 0.0F) {
    return std::clamp(value, spec.min, spec.max);
  }
  const f32 stepped =
      spec.min + std::round((value - spec.min) / spec.step) * spec.step;
  return std::clamp(stepped, spec.min, spec.max);
}

[[nodiscard]] std::optional<f32> slider_value_from_cursor(
    const UIContext& ctx,
    std::string_view stable_id,
    Vec2f cursor,
    SliderSpec spec) {
  const UiElement* slider = ctx.tree.find_by_stable_id(stable_id);
  if (slider == nullptr || slider->rect.width <= 0.0F) {
    return std::nullopt;
  }
  const f32 fraction =
      std::clamp((cursor.x - slider->rect.x) / slider->rect.width, 0.0F, 1.0F);
  return snap_slider_value(spec.min + fraction * (spec.max - spec.min), spec);
}

[[nodiscard]] f32 settings_row_width(const UiTree& tree, UiNodeID parent) noexcept {
  const UiElement* parent_element = tree.element(parent);
  if (parent_element != nullptr && parent_element->layout.width > 0.0F) {
    return std::min(SettingsContentWidth, parent_element->layout.width);
  }
  return SettingsContentWidth;
}

[[nodiscard]] f32 settings_row_content_width(f32 row_width) noexcept {
  return std::max(0.0F, row_width - SettingsRowInsetX * 2.0F);
}

[[nodiscard]] UiNodeID add_settings_shell(
    UIContext& ctx,
    std::string_view id,
    std::string title,
    f32 scroll_y,
    usize row_count,
    f32 row_height = SettingsRowHeight) {
  const UiTheme& theme = ctx.tree.theme();
  add_backdrop(ctx.tree, std::string{id} + ".backdrop", theme.colors.pause_backdrop);

  const f32 safe_width =
      std::min(SettingsPanelWidth, std::max(320.0F, ctx.viewport.x - theme.layout.spacing_xl * 2.0F));
  const f32 safe_height =
      std::min(SettingsPanelHeight, std::max(220.0F, ctx.viewport.y - theme.layout.spacing_xl * 2.0F));
  UiElement panel = make_panel(std::string{id} + ".panel", safe_width, safe_height);
  panel.layout.anchor = UiAnchor::Center;
  panel.layout.padding = {
      theme.widgets.panel.padding,
      theme.widgets.panel.padding,
      theme.widgets.panel.padding,
      theme.widgets.panel.padding};
  apply_panel_style(panel, theme, true);
  const UiNodeID panel_id = ctx.tree.add_child(ctx.tree.root_id(), std::move(panel));

  UiElement title_element =
      make_text(std::string{id} + ".title", std::move(title), theme.colors.text, UiTextRole::Title);
  title_element.layout.anchor = UiAnchor::TopCenter;
  title_element.layout.offset = {0.0F, 0.0F};
  ctx.tree.add_child(panel_id, std::move(title_element));

  const f32 content_width =
      std::min(SettingsContentWidth, std::max(0.0F, safe_width - theme.widgets.panel.padding * 2.0F));
  const f32 viewport_height =
      std::min(SettingsViewportHeight, std::max(80.0F, safe_height - 138.0F));
  UiElement viewport = make_panel(
      std::string{id} + ".viewport",
      content_width,
      viewport_height,
      {0.0F, 0.0F, 0.0F, 0.0F});
  viewport.layout.anchor = UiAnchor::TopCenter;
  viewport.layout.offset = {0.0F, 52.0F};
  viewport.clip_children = true;
  viewport.hit_test = true;
  const UiNodeID viewport_id = ctx.tree.add_child(panel_id, std::move(viewport));

  UiElement stack = make_stack(std::string{id} + ".rows", SettingsRowSpacing);
  stack.layout.width_mode = UiSizeMode::Fixed;
  stack.layout.width = content_width;
  stack.layout.offset = {
      0.0F,
      -std::clamp(scroll_y, 0.0F, settings_max_scroll(row_count, row_height))};
  return ctx.tree.add_child(viewport_id, std::move(stack));
}

[[nodiscard]] UiNodeID add_settings_row(
    UIContext& ctx,
    UiNodeID parent,
    std::string stable_id,
    std::string label,
    f32 row_height = SettingsRowHeight) {
  const UiTheme& theme = ctx.tree.theme();
  const f32 row_width = settings_row_width(ctx.tree, parent);
  UiElement row = make_panel(std::move(stable_id), row_width, row_height);
  row.style.background = theme.colors.surface_alt;
  row.style.border = theme.colors.border;
  row.style.border_width = theme.layout.border_width;
  row.style.radius = theme.layout.radius_sm;
  row.layout.padding = {SettingsRowInsetX, SettingsRowInsetX, 0.0F, 0.0F};
  const UiNodeID row_id = ctx.tree.add_child(parent, std::move(row));

  UiElement label_element = make_text(
      ctx.tree.element(row_id)->stable_id + ".label",
      std::move(label),
      theme.colors.text);
  label_element.layout.offset = {0.0F, 13.0F};
  ctx.tree.add_child(row_id, std::move(label_element));
  return row_id;
}

UiNodeID add_small_button(
    UiTree& tree,
    UiNodeID parent,
    std::string stable_id,
    std::string label,
    std::string action,
    Vec2f offset,
    f32 width,
    bool enabled = true,
    bool selected = false,
    UiControlRole role = UiControlRole::Neutral) {
  const UiNodeID button_id =
      add_button(
          tree,
          parent,
          std::move(stable_id),
          std::move(label),
          std::move(action),
          enabled,
          selected,
          role);
  if (UiElement* button = tree.element(button_id); button != nullptr) {
    button->layout.anchor = UiAnchor::TopLeft;
    button->layout.offset = offset;
    button->layout.width = width;
    button->layout.height = 34.0F;
    button->layout.padding = {8.0F, 8.0F, 7.0F, 7.0F};
  }
  return button_id;
}

void add_value_text(
    UIContext& ctx,
    UiNodeID parent,
    std::string stable_id,
    std::string value,
    Vec2f offset,
    f32 width = 118.0F) {
  const UiTheme& theme = ctx.tree.theme();
  UiElement box = make_panel(std::move(stable_id), width, 34.0F, theme.colors.surface);
  box.layout.offset = offset;
  box.style.border = theme.colors.border;
  box.style.border_width = theme.layout.border_width;
  box.style.radius = theme.layout.radius_sm;
  const UiNodeID box_id = ctx.tree.add_child(parent, std::move(box));
  UiElement text = make_text(
      ctx.tree.element(box_id)->stable_id + ".value",
      std::move(value),
      theme.colors.text,
      UiTextRole::Small);
  text.layout.anchor = UiAnchor::Center;
  ctx.tree.add_child(box_id, std::move(text));
}

void add_slider_row(
    UIContext& ctx,
    UiNodeID parent,
    std::string stable_id,
    std::string label,
    std::string value,
    std::string action_prefix,
    f32 slider_value,
    SliderSpec spec,
    bool enabled = true) {
  const UiNodeID row = add_settings_row(ctx, parent, stable_id, std::move(label));
  const UiTheme& theme = ctx.tree.theme();
  const f32 row_width = settings_row_width(ctx.tree, parent);
  const f32 content_width = settings_row_content_width(row_width);
  const bool compact = row_width < 520.0F;
  const f32 gap = compact ? 8.0F : 12.0F;
  const f32 value_width = compact ? 64.0F : 84.0F;
  const f32 slider_width =
      std::clamp(content_width * (compact ? 0.38F : 0.40F), 112.0F, 228.0F);
  const f32 controls_width = slider_width + gap + value_width;
  const f32 controls_x = std::max(0.0F, content_width - controls_width);
  const std::string slider_id = action_prefix + ".slider";
  const f32 fraction = slider_fraction(slider_value, spec);

  UiElement slider;
  slider.stable_id = slider_id;
  slider.type = UiElementType::Button;
  slider.layout.width_mode = UiSizeMode::Fixed;
  slider.layout.height_mode = UiSizeMode::Fixed;
  slider.layout.width = slider_width;
  slider.layout.height = 30.0F;
  slider.layout.anchor = UiAnchor::TopLeft;
  slider.layout.offset = {controls_x, 9.0F};
  slider.layout.padding = {};
  slider.style.background = {0.0F, 0.0F, 0.0F, 0.0F};
  slider.style.background_hover = {0.0F, 0.0F, 0.0F, 0.0F};
  slider.style.background_pressed = {0.0F, 0.0F, 0.0F, 0.0F};
  slider.enabled = enabled;
  slider.focusable = enabled;
  slider.hit_test = enabled;
  slider.action = enabled ? slider_id : std::string{};
  const UiNodeID slider_node = ctx.tree.add_child(row, std::move(slider));

  UiElement track =
      make_panel(slider_id + ".track", slider_width, 6.0F, theme.colors.surface);
  track.layout.offset = {0.0F, 12.0F};
  track.style.radius = 3.0F;
  track.style.border = enabled ? theme.colors.border : theme.colors.surface;
  track.style.border_width = theme.layout.border_width;
  track.hit_test = false;
  ctx.tree.add_child(slider_node, std::move(track));

  UiElement fill = make_panel(
      slider_id + ".fill",
      std::max(0.0F, slider_width * fraction),
      6.0F,
      enabled ? theme.colors.accent : theme.colors.text_disabled);
  fill.layout.offset = {0.0F, 12.0F};
  fill.style.radius = 3.0F;
  fill.hit_test = false;
  ctx.tree.add_child(slider_node, std::move(fill));

  UiElement knob = make_panel(
      slider_id + ".knob",
      18.0F,
      18.0F,
      enabled ? theme.colors.accent_text : theme.colors.text_disabled);
  knob.layout.offset = {std::clamp(slider_width * fraction - 9.0F, 0.0F, slider_width - 18.0F), 6.0F};
  knob.style.radius = 9.0F;
  knob.style.border = enabled ? theme.colors.accent : theme.colors.border;
  knob.style.border_width = theme.layout.border_width;
  knob.hit_test = false;
  ctx.tree.add_child(slider_node, std::move(knob));

  add_value_text(
      ctx,
      row,
      action_prefix + ".value",
      std::move(value),
      {controls_x + slider_width + gap, 7.0F},
      value_width);
}

void add_toggle_row(
    UIContext& ctx,
    UiNodeID parent,
    std::string stable_id,
    std::string label,
    bool value,
    std::string action,
    f32 progress = -1.0F) {
  const UiNodeID row = add_settings_row(ctx, parent, std::move(stable_id), std::move(label));
  const UiTheme& theme = ctx.tree.theme();
  const f32 row_width = settings_row_width(ctx.tree, parent);
  const f32 content_width = settings_row_content_width(row_width);
  const f32 switch_width = 58.0F;
  const f32 switch_height = 30.0F;
  const f32 amount = progress < 0.0F ? (value ? 1.0F : 0.0F) : std::clamp(progress, 0.0F, 1.0F);
  UiElement toggle;
  toggle.stable_id = action;
  toggle.type = UiElementType::Button;
  toggle.layout.width_mode = UiSizeMode::Fixed;
  toggle.layout.height_mode = UiSizeMode::Fixed;
  toggle.layout.width = switch_width;
  toggle.layout.height = switch_height;
  toggle.layout.anchor = UiAnchor::TopLeft;
  toggle.layout.offset = {std::max(0.0F, content_width - switch_width), 9.0F};
  toggle.layout.padding = {};
  toggle.style.background = mix_color(theme.colors.surface, theme.colors.accent, amount);
  toggle.style.background_hover = mix_color(theme.colors.surface_hover, theme.colors.accent_hover, amount);
  toggle.style.background_pressed = mix_color(theme.colors.surface_pressed, theme.colors.accent_pressed, amount);
  toggle.style.border = theme.colors.border;
  toggle.style.border_width = theme.layout.border_width;
  toggle.style.radius = switch_height * 0.5F;
  toggle.enabled = true;
  toggle.focusable = true;
  toggle.hit_test = true;
  toggle.action = action;
  const UiNodeID toggle_id = ctx.tree.add_child(row, std::move(toggle));

  UiElement knob = make_panel(action + ".knob", 24.0F, 24.0F, theme.colors.accent_text);
  knob.layout.offset = {3.0F + amount * (switch_width - 30.0F), 3.0F};
  knob.style.radius = 12.0F;
  knob.style.border = mix_color(theme.colors.border, theme.colors.accent, amount);
  knob.style.border_width = theme.layout.border_width;
  knob.hit_test = false;
  ctx.tree.add_child(toggle_id, std::move(knob));
}

void add_frame_sync_row(UIContext& ctx, UiNodeID parent, SettingsFrameSyncMode mode) {
  const UiNodeID row = add_settings_row(
      ctx,
      parent,
      "render.frame_sync",
      tr(ctx, "ui.menu.settings.render_frame_sync"));
  const f32 row_width = settings_row_width(ctx.tree, parent);
  const f32 content_width = settings_row_content_width(row_width);
  const bool compact = row_width < 560.0F;
  const f32 gap = compact ? 6.0F : 10.0F;
  const f32 off_width = compact ? 64.0F : 82.0F;
  const f32 vsync_width = compact ? 64.0F : 82.0F;
  const f32 cap_width = compact ? 76.0F : 84.0F;
  const f32 controls_width = off_width + vsync_width + cap_width + gap * 2.0F;
  const f32 controls_x = std::max(0.0F, content_width - controls_width);
  add_small_button(
      ctx.tree,
      row,
      "render.frame_sync.off",
      tr(ctx, "ui.menu.settings.render_frame_sync_off"),
      "render.frame_sync.off",
      {controls_x, 7.0F},
      off_width,
      true,
      mode == SettingsFrameSyncMode::Off,
      mode == SettingsFrameSyncMode::Off ? UiControlRole::Primary : UiControlRole::Neutral);
  add_small_button(
      ctx.tree,
      row,
      "render.frame_sync.vsync",
      tr(ctx, "ui.menu.settings.render_frame_sync_vsync"),
      "render.frame_sync.vsync",
      {controls_x + off_width + gap, 7.0F},
      vsync_width,
      true,
      mode == SettingsFrameSyncMode::VSync,
      mode == SettingsFrameSyncMode::VSync ? UiControlRole::Primary : UiControlRole::Neutral);
  add_small_button(
      ctx.tree,
      row,
      "render.frame_sync.fps_cap",
      tr(ctx, "ui.menu.settings.render_frame_sync_fps_cap"),
      "render.frame_sync.fps_cap",
      {controls_x + off_width + gap + vsync_width + gap, 7.0F},
      cap_width,
      true,
      mode == SettingsFrameSyncMode::FpsCap,
      mode == SettingsFrameSyncMode::FpsCap ? UiControlRole::Primary : UiControlRole::Neutral);
}

void add_back_button(UIContext& ctx, std::string_view id) {
  const UiElement* panel = ctx.tree.find_by_stable_id(std::string{id} + ".panel");
  if (panel == nullptr) {
    return;
  }
  const UiNodeID panel_id = panel->node_id;
  const UiNodeID button_id = add_small_button(
      ctx.tree,
      panel_id,
      std::string{id} + ".back",
      tr(ctx, "ui.menu.common.back"),
      "menu.back",
      {0.0F, std::max(128.0F, panel->layout.height - 76.0F)},
      156.0F);
  if (UiElement* button = ctx.tree.element(button_id); button != nullptr) {
    button->layout.anchor = UiAnchor::TopCenter;
  }
}

[[nodiscard]] std::optional<SliderSpec> render_slider_spec(
    std::string_view action_id,
    const RenderSettingsView& settings) noexcept {
  if (action_id == "render.fps_cap.slider") {
    return SliderSpec{30.0F, 240.0F, 5.0F};
  }
  if (action_id == "render.load_distance.slider") {
    return SliderSpec{1.0F, static_cast<f32>(MaxConfigurableChunkDistance), 1.0F};
  }
  if (action_id == "render.simulation_distance.slider") {
    return SliderSpec{0.0F, static_cast<f32>(std::max(0, settings.load_distance)), 1.0F};
  }
  if (action_id == "render.view_distance.slider") {
    return SliderSpec{1.0F, static_cast<f32>(std::max(1, settings.load_distance)), 1.0F};
  }
  if (action_id == "render.stream_distance.slider") {
    return SliderSpec{
        static_cast<f32>(std::max(1, settings.view_distance)),
        static_cast<f32>(std::max(1, settings.load_distance)),
        1.0F};
  }
  if (action_id == "render.mesh_upload_budget.slider") {
    return SliderSpec{1.0F, 32.0F, 1.0F};
  }
  if (action_id == "render.star_intensity.slider" ||
      action_id == "render.cloud_intensity.slider") {
    return SliderSpec{0.0F, 3.0F, 0.05F};
  }
  if (action_id == "render.cirrus.slider" ||
      action_id == "render.cumulus.slider" ||
      action_id == "render.outline_red.slider" ||
      action_id == "render.outline_green.slider" ||
      action_id == "render.outline_blue.slider") {
    return SliderSpec{0.0F, 1.0F, 0.05F};
  }
  if (action_id == "render.outline_thickness.slider") {
    return SliderSpec{0.1F, 2.0F, 0.05F};
  }
  return std::nullopt;
}

void normalize_render_chunk_distances(RenderSettingsView& settings) noexcept {
  settings.load_distance =
      std::clamp(settings.load_distance, 1, MaxConfigurableChunkDistance);
  settings.simulation_distance =
      std::clamp(settings.simulation_distance, 0, settings.load_distance);
  settings.stream_distance =
      std::clamp(settings.stream_distance, 1, settings.load_distance);
  settings.view_distance =
      std::clamp(settings.view_distance, 1, settings.stream_distance);
}

[[nodiscard]] bool apply_render_slider_value(
    RenderSettingsView& settings,
    std::string_view action_id,
    f32 value) noexcept {
  if (action_id == "render.fps_cap.slider") {
    const u32 next = static_cast<u32>(std::lround(value));
    if (settings.fps_cap == next) {
      return false;
    }
    settings.fps_cap = next;
    return true;
  }
  if (action_id == "render.load_distance.slider") {
    const i32 next = static_cast<i32>(std::lround(value));
    if (settings.load_distance == next) {
      return false;
    }
    settings.load_distance = next;
    normalize_render_chunk_distances(settings);
    return true;
  }
  if (action_id == "render.simulation_distance.slider") {
    const i32 next = static_cast<i32>(std::lround(value));
    if (settings.simulation_distance == next) {
      return false;
    }
    settings.simulation_distance = next;
    normalize_render_chunk_distances(settings);
    return true;
  }
  if (action_id == "render.view_distance.slider") {
    const i32 next = static_cast<i32>(std::lround(value));
    if (settings.view_distance == next) {
      return false;
    }
    settings.view_distance = next;
    settings.stream_distance = std::max(settings.stream_distance, next);
    normalize_render_chunk_distances(settings);
    return true;
  }
  if (action_id == "render.stream_distance.slider") {
    const i32 next = static_cast<i32>(std::lround(value));
    if (settings.stream_distance == next) {
      return false;
    }
    settings.stream_distance = next;
    settings.view_distance = std::min(settings.view_distance, next);
    normalize_render_chunk_distances(settings);
    return true;
  }
  if (action_id == "render.mesh_upload_budget.slider") {
    const u32 next = static_cast<u32>(std::lround(value));
    if (settings.mesh_upload_budget == next) {
      return false;
    }
    settings.mesh_upload_budget = next;
    return true;
  }
  auto changed_float = [value](f32& target) {
    if (std::abs(target - value) <= 0.0001F) {
      return false;
    }
    target = value;
    return true;
  };
  if (action_id == "render.star_intensity.slider") {
    return changed_float(settings.star_intensity);
  }
  if (action_id == "render.cloud_intensity.slider") {
    return changed_float(settings.cloud_intensity);
  }
  if (action_id == "render.cirrus.slider") {
    return changed_float(settings.cirrus_amount);
  }
  if (action_id == "render.cumulus.slider") {
    return changed_float(settings.cumulus_amount);
  }
  if (action_id == "render.outline_red.slider") {
    return changed_float(settings.outline_red);
  }
  if (action_id == "render.outline_green.slider") {
    return changed_float(settings.outline_green);
  }
  if (action_id == "render.outline_blue.slider") {
    return changed_float(settings.outline_blue);
  }
  if (action_id == "render.outline_thickness.slider") {
    return changed_float(settings.outline_thickness);
  }
  return false;
}

[[nodiscard]] bool is_render_slider_action(
    std::string_view action_id,
    const RenderSettingsView& settings) noexcept {
  return render_slider_spec(action_id, settings).has_value();
}

[[nodiscard]] bool apply_controls_slider_value(
    ControlsSettingsView& settings,
    std::string_view action_id,
    f32 value) noexcept {
  if (action_id != "controls.mouse_sensitivity.slider") {
    return false;
  }
  if (std::abs(settings.mouse_sensitivity_percent - value) <= 0.0001F) {
    return false;
  }
  settings.mouse_sensitivity_percent = value;
  return true;
}

[[nodiscard]] std::optional<SliderSpec> interface_slider_spec(std::string_view action_id) noexcept {
  if (action_id == "interface.chat_history.slider") {
    return SliderSpec{100.0F, 5000.0F, 100.0F};
  }
  if (action_id == "interface.open_lines.slider") {
    return SliderSpec{1.0F, 16.0F, 1.0F};
  }
  if (action_id == "interface.passive_lines.slider") {
    return SliderSpec{0.0F, 16.0F, 1.0F};
  }
  if (action_id == "interface.visible_seconds.slider" ||
      action_id == "interface.fade_seconds.slider") {
    return SliderSpec{0.0F, 30.0F, 0.5F};
  }
  return std::nullopt;
}

[[nodiscard]] bool apply_interface_slider_value(
    InterfaceSettingsView& settings,
    std::string_view action_id,
    f32 value) noexcept {
  if (action_id == "interface.chat_history.slider") {
    const u32 next = static_cast<u32>(std::lround(value));
    if (settings.chat_history_max == next) {
      return false;
    }
    settings.chat_history_max = next;
    return true;
  }
  if (action_id == "interface.open_lines.slider") {
    const u32 next = static_cast<u32>(std::lround(value));
    if (settings.chat_open_lines == next) {
      return false;
    }
    settings.chat_open_lines = next;
    return true;
  }
  if (action_id == "interface.passive_lines.slider") {
    const u32 next = static_cast<u32>(std::lround(value));
    if (settings.chat_passive_lines == next) {
      return false;
    }
    settings.chat_passive_lines = next;
    return true;
  }
  if (action_id == "interface.visible_seconds.slider") {
    if (std::abs(settings.chat_passive_visible_seconds - value) <= 0.0001F) {
      return false;
    }
    settings.chat_passive_visible_seconds = value;
    return true;
  }
  if (action_id == "interface.fade_seconds.slider") {
    if (std::abs(settings.chat_passive_fade_seconds - value) <= 0.0001F) {
      return false;
    }
    settings.chat_passive_fade_seconds = value;
    return true;
  }
  return false;
}

class SettingsMenuScreen final : public UIScreen {
public:
  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::SettingsMenu;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "settings_menu";
  }

  void build(UIContext& ctx) override {
    const UiTheme& theme = ctx.tree.theme();
    add_backdrop(ctx.tree, "settings_menu.backdrop", theme.colors.pause_backdrop);
    UiElement stack_element = make_stack("settings_menu.stack", theme.layout.spacing_md);
    stack_element.layout.width_mode = UiSizeMode::Fixed;
    stack_element.layout.width = menu_button_width(theme);
    stack_element.layout.anchor = UiAnchor::Center;
    const UiNodeID stack = ctx.tree.add_child(ctx.tree.root_id(), std::move(stack_element));

    UiElement title =
        make_text("settings_menu.title", tr(ctx, "ui.menu.settings.title"), theme.colors.text, UiTextRole::Heading);
    title.layout.anchor = UiAnchor::TopCenter;
    title.layout.offset = {0.0F, -theme.layout.spacing_sm};
    ctx.tree.add_child(stack, std::move(title));

    add_button(
        ctx.tree,
        stack,
        "menu.settings.render",
        tr(ctx, "ui.menu.settings.render"),
        "settings.render",
        true,
        false,
        UiControlRole::Primary);
    add_button(ctx.tree, stack, "menu.settings.key_binds", tr(ctx, "ui.menu.settings.key_binds"), "settings.key_binds");
    add_button(ctx.tree, stack, "menu.settings.controls", tr(ctx, "ui.menu.settings.controls"), "settings.controls");
    add_button(ctx.tree, stack, "menu.settings.interface", tr(ctx, "ui.menu.settings.interface"), "settings.interface");
    add_button(ctx.tree, stack, "menu.settings.back", tr(ctx, "ui.menu.common.back"), "menu.back");
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (ctx.screens == nullptr) {
      return false;
    }
    if (action.action_id == "settings.render") {
      ctx.screens->push(make_render_settings_screen());
      return true;
    }
    if (action.action_id == "settings.key_binds") {
      ctx.screens->push(make_key_binds_screen());
      return true;
    }
    if (action.action_id == "settings.controls") {
      ctx.screens->push(make_controls_settings_screen());
      return true;
    }
    if (action.action_id == "settings.interface") {
      ctx.screens->push(make_interface_settings_screen());
      return true;
    }
    if (action.action_id == "menu.back") {
      return handle_cancel(ctx);
    }
    return false;
  }
};

class RenderSettingsScreen final : public UIScreen {
public:
  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::RenderSettings;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "render_settings";
  }

  void build(UIContext& ctx) override {
    const RenderSettingsView settings = render_settings_from(ctx);
    const UiNodeID stack = add_settings_shell(
        ctx,
        screen_id(),
        tr(ctx, "ui.menu.settings.render"),
        scroll_y_,
        16U);
    add_frame_sync_row(ctx, stack, settings.frame_sync);
    add_slider_row(
        ctx,
        stack,
        "render.fps_cap",
        tr(ctx, "ui.menu.settings.render_fps_cap"),
        std::to_string(settings.fps_cap),
        "render.fps_cap",
        static_cast<f32>(settings.fps_cap),
        {30.0F, 240.0F, 5.0F},
        settings.frame_sync == SettingsFrameSyncMode::FpsCap);
    add_slider_row(
        ctx,
        stack,
        "render.load_distance",
        tr(ctx, "ui.menu.settings.render_load_distance"),
        std::to_string(settings.load_distance),
        "render.load_distance",
        static_cast<f32>(settings.load_distance),
        {1.0F, static_cast<f32>(MaxConfigurableChunkDistance), 1.0F});
    add_slider_row(
        ctx,
        stack,
        "render.simulation_distance",
        tr(ctx, "ui.menu.settings.render_simulation_distance"),
        std::to_string(settings.simulation_distance),
        "render.simulation_distance",
        static_cast<f32>(settings.simulation_distance),
        {0.0F, static_cast<f32>(std::max(0, settings.load_distance)), 1.0F});
    add_slider_row(
        ctx,
        stack,
        "render.view_distance",
        tr(ctx, "ui.menu.settings.render_view_distance"),
        std::to_string(settings.view_distance),
        "render.view_distance",
        static_cast<f32>(settings.view_distance),
        {1.0F, static_cast<f32>(std::max(1, settings.load_distance)), 1.0F});
    add_slider_row(
        ctx,
        stack,
        "render.stream_distance",
        tr(ctx, "ui.menu.settings.render_stream_distance"),
        std::to_string(settings.stream_distance),
        "render.stream_distance",
        static_cast<f32>(settings.stream_distance),
        {static_cast<f32>(std::max(1, settings.view_distance)),
         static_cast<f32>(std::max(1, settings.load_distance)),
         1.0F});
    add_slider_row(
        ctx,
        stack,
        "render.mesh_upload_budget",
        tr(ctx, "ui.menu.settings.render_mesh_upload_budget"),
        std::to_string(settings.mesh_upload_budget),
        "render.mesh_upload_budget",
        static_cast<f32>(settings.mesh_upload_budget),
        {1.0F, 32.0F, 1.0F});
    add_toggle_row(
        ctx,
        stack,
        "render.local_player_body",
        tr(ctx, "ui.menu.settings.render_local_player_body"),
        settings.show_local_player_body,
        "render.local_player_body",
        animated_switch_progress(settings.show_local_player_body, local_player_switch_, ctx));
    add_slider_row(
        ctx,
        stack,
        "render.sky.star_intensity",
        tr(ctx, "ui.menu.settings.render_star_intensity"),
        fixed_decimal(settings.star_intensity, 2),
        "render.star_intensity",
        settings.star_intensity,
        {0.0F, 3.0F, 0.05F});
    add_slider_row(
        ctx,
        stack,
        "render.sky.cloud_intensity",
        tr(ctx, "ui.menu.settings.render_cloud_intensity"),
        fixed_decimal(settings.cloud_intensity, 2),
        "render.cloud_intensity",
        settings.cloud_intensity,
        {0.0F, 3.0F, 0.05F});
    add_slider_row(
        ctx,
        stack,
        "render.sky.cirrus",
        tr(ctx, "ui.menu.settings.render_cirrus"),
        fixed_decimal(settings.cirrus_amount, 2),
        "render.cirrus",
        settings.cirrus_amount,
        {0.0F, 1.0F, 0.05F});
    add_slider_row(
        ctx,
        stack,
        "render.sky.cumulus",
        tr(ctx, "ui.menu.settings.render_cumulus"),
        fixed_decimal(settings.cumulus_amount, 2),
        "render.cumulus",
        settings.cumulus_amount,
        {0.0F, 1.0F, 0.05F});
    add_slider_row(
        ctx,
        stack,
        "render.outline.red",
        tr(ctx, "ui.menu.settings.render_outline_red"),
        fixed_decimal(settings.outline_red, 2),
        "render.outline_red",
        settings.outline_red,
        {0.0F, 1.0F, 0.05F});
    add_slider_row(
        ctx,
        stack,
        "render.outline.green",
        tr(ctx, "ui.menu.settings.render_outline_green"),
        fixed_decimal(settings.outline_green, 2),
        "render.outline_green",
        settings.outline_green,
        {0.0F, 1.0F, 0.05F});
    add_slider_row(
        ctx,
        stack,
        "render.outline.blue",
        tr(ctx, "ui.menu.settings.render_outline_blue"),
        fixed_decimal(settings.outline_blue, 2),
        "render.outline_blue",
        settings.outline_blue,
        {0.0F, 1.0F, 0.05F});
    add_slider_row(
        ctx,
        stack,
        "render.outline.thickness",
        tr(ctx, "ui.menu.settings.render_outline_thickness"),
        fixed_decimal(settings.outline_thickness, 2),
        "render.outline_thickness",
        settings.outline_thickness,
        {0.1F, 2.0F, 0.05F});
    add_back_button(ctx, screen_id());
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (action.action_id == "menu.back") {
      return handle_cancel(ctx);
    }
    RenderSettingsView settings = render_settings_from(ctx);
    if (is_render_slider_action(action.action_id, settings)) {
      active_slider_ = action.action_id;
      return true;
    }
    bool changed = true;
    if (action.action_id == "render.frame_sync.off") {
      settings.frame_sync = SettingsFrameSyncMode::Off;
    } else if (action.action_id == "render.frame_sync.vsync") {
      settings.frame_sync = SettingsFrameSyncMode::VSync;
    } else if (action.action_id == "render.frame_sync.fps_cap") {
      settings.frame_sync = SettingsFrameSyncMode::FpsCap;
    } else if (action.action_id == "render.local_player_body") {
      settings.show_local_player_body = !settings.show_local_player_body;
    } else {
      changed = false;
    }
    if (!changed) {
      return false;
    }
    submit_command(ctx, SetRenderSettingsCommand{settings});
    return true;
  }

  bool handle_scroll(Vec2f cursor, f32 wheel_y, UIContext& ctx) override {
    (void)cursor;
    (void)ctx;
    scroll_y_ =
        std::clamp(scroll_y_ - wheel_y * 38.0F, 0.0F, settings_max_scroll(16U, SettingsRowHeight));
    return true;
  }

  bool handle_pointer(
      Vec2f cursor,
      Vec2f delta,
      bool left_pressed,
      bool left_released,
      bool left_held,
      UIContext& ctx) override {
    (void)delta;
    if (!active_slider_.has_value()) {
      return false;
    }
    bool handled = left_pressed || left_released || left_held;
    if (handled) {
      RenderSettingsView settings = render_settings_from(ctx);
      const std::optional<SliderSpec> spec =
          render_slider_spec(active_slider_.value(), settings);
      if (spec.has_value()) {
        const std::optional<f32> value =
            slider_value_from_cursor(ctx, active_slider_.value(), cursor, spec.value());
        if (value.has_value() &&
            apply_render_slider_value(settings, active_slider_.value(), value.value())) {
          submit_command(ctx, SetRenderSettingsCommand{settings});
        }
      }
    }
    if (left_released || (!left_pressed && !left_held)) {
      active_slider_.reset();
      handled = true;
    }
    return handled;
  }

private:
  f32 scroll_y_{};
  std::optional<std::string> active_slider_{};
  SwitchAnimation local_player_switch_{};
};

class ControlsSettingsScreen final : public UIScreen {
public:
  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::ControlsSettings;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "controls_settings";
  }

  void build(UIContext& ctx) override {
    const ControlsSettingsView settings = controls_settings_from(ctx);
    const UiNodeID stack = add_settings_shell(
        ctx,
        screen_id(),
        tr(ctx, "ui.menu.settings.controls"),
        0.0F,
        1U);
    add_slider_row(
        ctx,
        stack,
        "controls.mouse_sensitivity",
        tr(ctx, "ui.menu.settings.controls_mouse_sensitivity"),
        percent_text(settings.mouse_sensitivity_percent),
        "controls.mouse_sensitivity",
        settings.mouse_sensitivity_percent,
        {10.0F, 300.0F, 5.0F});
    add_back_button(ctx, screen_id());
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (action.action_id == "menu.back") {
      return handle_cancel(ctx);
    }
    if (action.action_id == "controls.mouse_sensitivity.slider") {
      active_slider_ = action.action_id;
      return true;
    }
    return false;
  }

  bool handle_pointer(
      Vec2f cursor,
      Vec2f delta,
      bool left_pressed,
      bool left_released,
      bool left_held,
      UIContext& ctx) override {
    (void)delta;
    if (!active_slider_.has_value()) {
      return false;
    }
    bool handled = left_pressed || left_released || left_held;
    if (handled) {
      ControlsSettingsView settings = controls_settings_from(ctx);
      const std::optional<f32> value =
          slider_value_from_cursor(
              ctx,
              active_slider_.value(),
              cursor,
              {10.0F, 300.0F, 5.0F});
      if (value.has_value() &&
          apply_controls_slider_value(settings, active_slider_.value(), value.value())) {
        submit_command(ctx, SetControlsSettingsCommand{settings});
      }
    }
    if (left_released || (!left_pressed && !left_held)) {
      active_slider_.reset();
      handled = true;
    }
    return handled;
  }

private:
  std::optional<std::string> active_slider_{};
};

class InterfaceSettingsScreen final : public UIScreen {
public:
  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::InterfaceSettings;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "interface_settings";
  }

  void build(UIContext& ctx) override {
    const InterfaceSettingsView settings = interface_settings_from(ctx);
    const UiNodeID stack = add_settings_shell(
        ctx,
        screen_id(),
        tr(ctx, "ui.menu.settings.interface"),
        scroll_y_,
        5U);
    add_slider_row(
        ctx,
        stack,
        "interface.chat_history",
        tr(ctx, "ui.menu.settings.interface_chat_history"),
        std::to_string(settings.chat_history_max),
        "interface.chat_history",
        static_cast<f32>(settings.chat_history_max),
        {100.0F, 5000.0F, 100.0F});
    add_slider_row(
        ctx,
        stack,
        "interface.open_lines",
        tr(ctx, "ui.menu.settings.interface_open_lines"),
        std::to_string(settings.chat_open_lines),
        "interface.open_lines",
        static_cast<f32>(settings.chat_open_lines),
        {1.0F, 16.0F, 1.0F});
    add_slider_row(
        ctx,
        stack,
        "interface.passive_lines",
        tr(ctx, "ui.menu.settings.interface_passive_lines"),
        std::to_string(settings.chat_passive_lines),
        "interface.passive_lines",
        static_cast<f32>(settings.chat_passive_lines),
        {0.0F, 16.0F, 1.0F});
    add_slider_row(
        ctx,
        stack,
        "interface.visible_seconds",
        tr(ctx, "ui.menu.settings.interface_visible_seconds"),
        fixed_decimal(settings.chat_passive_visible_seconds, 1),
        "interface.visible_seconds",
        settings.chat_passive_visible_seconds,
        {0.0F, 30.0F, 0.5F});
    add_slider_row(
        ctx,
        stack,
        "interface.fade_seconds",
        tr(ctx, "ui.menu.settings.interface_fade_seconds"),
        fixed_decimal(settings.chat_passive_fade_seconds, 1),
        "interface.fade_seconds",
        settings.chat_passive_fade_seconds,
        {0.0F, 30.0F, 0.5F});
    add_back_button(ctx, screen_id());
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (action.action_id == "menu.back") {
      return handle_cancel(ctx);
    }
    if (interface_slider_spec(action.action_id).has_value()) {
      active_slider_ = action.action_id;
      return true;
    }
    return false;
  }

  bool handle_pointer(
      Vec2f cursor,
      Vec2f delta,
      bool left_pressed,
      bool left_released,
      bool left_held,
      UIContext& ctx) override {
    (void)delta;
    if (!active_slider_.has_value()) {
      return false;
    }
    bool handled = left_pressed || left_released || left_held;
    if (handled) {
      InterfaceSettingsView settings = interface_settings_from(ctx);
      const std::optional<SliderSpec> spec = interface_slider_spec(active_slider_.value());
      if (spec.has_value()) {
        const std::optional<f32> value =
            slider_value_from_cursor(ctx, active_slider_.value(), cursor, spec.value());
        if (value.has_value() &&
            apply_interface_slider_value(settings, active_slider_.value(), value.value())) {
          submit_command(ctx, SetInterfaceSettingsCommand{settings});
        }
      }
    }
    if (left_released || (!left_pressed && !left_held)) {
      active_slider_.reset();
      handled = true;
    }
    return handled;
  }

  bool handle_scroll(Vec2f cursor, f32 wheel_y, UIContext& ctx) override {
    (void)cursor;
    (void)ctx;
    scroll_y_ =
        std::clamp(scroll_y_ - wheel_y * 38.0F, 0.0F, settings_max_scroll(5U, SettingsRowHeight));
    return true;
  }

private:
  f32 scroll_y_{};
  std::optional<std::string> active_slider_{};
};

[[nodiscard]] bool parse_keybind_index(
    std::string_view stable_id,
    std::string_view prefix,
    usize& index) noexcept {
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
  index = parsed;
  return true;
}

[[nodiscard]] std::string bind_label(const KeyBindSlotView& slot, const UIContext& ctx) {
  if (!slot.has_binding || !slot.binding.valid) {
    return tr(ctx, "ui.menu.settings.keybind_unbound");
  }
  return slot.binding.label.empty() ? slot.binding.input_code : slot.binding.label;
}

void add_keybind_row(
    UIContext& ctx,
    UiNodeID parent,
    const KeyBindEntryView& entry,
    usize index,
    const std::optional<CaptureTarget>& capture) {
  const UiTheme& theme = ctx.tree.theme();
  const std::string prefix = "key_binds.row." + std::to_string(index);
  const UiNodeID row = add_settings_row(
      ctx,
      parent,
      prefix,
      entry.display_name,
      KeyBindRowHeight);
  if (entry.primary.conflict || entry.secondary.conflict) {
    UiElement warning = make_text(
        prefix + ".warning",
        tr(ctx, "ui.menu.settings.keybind_conflict"),
        theme.colors.warning,
        UiTextRole::Small);
    warning.layout.offset = {0.0F, 34.0F};
    ctx.tree.add_child(row, std::move(warning));
  }
  const bool capturing_primary =
      capture.has_value() && capture->action_name == entry.action_name && capture->slot == 0U;
  const bool capturing_secondary =
      capture.has_value() && capture->action_name == entry.action_name && capture->slot == 1U;
  const f32 row_width = settings_row_width(ctx.tree, parent);
  const f32 content_width = settings_row_content_width(row_width);
  const bool compact = row_width < 650.0F;
  const f32 gap = compact ? 6.0F : 10.0F;
  const f32 reset_width = compact ? 58.0F : 82.0F;
  const f32 clear_width = compact ? 50.0F : 62.0F;
  const f32 slot_width =
      compact ? std::clamp(
                    (content_width - reset_width - clear_width - gap * 3.0F) / 2.0F,
                    72.0F,
                    150.0F)
              : 150.0F;
  const f32 controls_width =
      slot_width * 2.0F + clear_width + reset_width + gap * 3.0F;
  const f32 controls_x = compact ? std::max(0.0F, content_width - controls_width)
                                 : 236.0F;
  add_small_button(
      ctx.tree,
      row,
      prefix + ".primary",
      capturing_primary ? tr(ctx, "ui.menu.settings.keybind_listening") : bind_label(entry.primary, ctx),
      "key_bind.capture_primary",
      {controls_x, 12.0F},
      slot_width,
      true,
      capturing_primary,
      capturing_primary ? UiControlRole::Primary : UiControlRole::Neutral);
  add_small_button(
      ctx.tree,
      row,
      prefix + ".secondary",
      capturing_secondary ? tr(ctx, "ui.menu.settings.keybind_listening") : bind_label(entry.secondary, ctx),
      "key_bind.capture_secondary",
      {controls_x + slot_width + gap, 12.0F},
      slot_width,
      true,
      capturing_secondary,
      capturing_secondary ? UiControlRole::Primary : UiControlRole::Neutral);
  add_small_button(
      ctx.tree,
      row,
      prefix + ".clear_secondary",
      tr(ctx, "ui.menu.settings.keybind_clear"),
      "key_bind.clear_secondary",
      {controls_x + slot_width * 2.0F + gap * 2.0F, 12.0F},
      clear_width,
      entry.secondary.has_binding);
  add_small_button(
      ctx.tree,
      row,
      prefix + ".reset",
      tr(ctx, "ui.menu.settings.keybind_reset"),
      "key_bind.reset",
      {controls_x + slot_width * 2.0F + clear_width + gap * 3.0F, 12.0F},
      reset_width);
}

class KeyBindsScreen final : public UIScreen {
public:
  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::KeyBinds;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "key_binds";
  }

  void build(UIContext& ctx) override {
    const std::vector<KeyBindEntryView> entries = key_bind_entries_from(ctx);
    const UiNodeID stack = add_settings_shell(
        ctx,
        screen_id(),
        tr(ctx, "ui.menu.settings.key_binds"),
        scroll_y_,
        entries.size(),
        KeyBindRowHeight);
    for (usize index = 0; index < entries.size(); ++index) {
      add_keybind_row(ctx, stack, entries[index], index, capture_);
    }
    const UiElement* panel = ctx.tree.find_by_stable_id("key_binds.panel");
    if (panel != nullptr) {
      const UiNodeID panel_id = panel->node_id;
      if (capture_.has_value()) {
        const f32 footer_y = std::max(128.0F, panel->layout.height - 76.0F);
        const UiNodeID cancel_id = add_small_button(
            ctx.tree,
            panel_id,
            "key_binds.cancel_capture",
            tr(ctx, "ui.menu.settings.keybind_cancel"),
            "key_bind.cancel_capture",
            {-166.0F, footer_y},
            156.0F,
            true,
            false,
            UiControlRole::Danger);
        if (UiElement* button = ctx.tree.element(cancel_id); button != nullptr) {
          button->layout.anchor = UiAnchor::TopCenter;
        }
        const UiNodeID back_id = add_small_button(
            ctx.tree,
            panel_id,
            "key_binds.back",
            tr(ctx, "ui.menu.common.back"),
            "menu.back",
            {166.0F, footer_y},
            156.0F);
        if (UiElement* button = ctx.tree.element(back_id); button != nullptr) {
          button->layout.anchor = UiAnchor::TopCenter;
        }
      } else {
        add_back_button(ctx, screen_id());
      }
    }
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (action.action_id == "menu.back") {
      return handle_cancel(ctx);
    }
    if (action.action_id == "key_bind.cancel_capture") {
      capture_.reset();
      return true;
    }

    usize index = 0;
    if (!parse_keybind_index(action.element_id, "key_binds.row.", index)) {
      return false;
    }
    const std::vector<KeyBindEntryView> entries = key_bind_entries_from(ctx);
    if (index >= entries.size()) {
      return false;
    }
    const std::string action_name = entries[index].action_name;
    if (action.action_id == "key_bind.capture_primary") {
      capture_ = CaptureTarget{action_name, 0U};
      return true;
    }
    if (action.action_id == "key_bind.capture_secondary") {
      capture_ = CaptureTarget{action_name, 1U};
      return true;
    }
    if (action.action_id == "key_bind.clear_secondary") {
      submit_command(ctx, SetKeyBindCommand{action_name, 1U, std::nullopt});
      capture_.reset();
      return true;
    }
    if (action.action_id == "key_bind.reset") {
      submit_command(ctx, ResetKeyBindCommand{action_name});
      capture_.reset();
      return true;
    }
    return false;
  }

  bool handle_captured_input(const CapturedInputView& input, UIContext& ctx) override {
    if (!capture_.has_value()) {
      return false;
    }
    InputBindingView binding;
    binding.valid = true;
    binding.device = input.device;
    binding.input_code = input.input_code;
    binding.label = input.label.empty() ? input.input_code : input.label;
    binding.modifiers = input.modifiers;
    submit_command(
        ctx,
        SetKeyBindCommand{capture_->action_name, capture_->slot, std::move(binding)});
    capture_.reset();
    return true;
  }

  bool handle_scroll(Vec2f cursor, f32 wheel_y, UIContext& ctx) override {
    (void)cursor;
    const std::vector<KeyBindEntryView> entries = key_bind_entries_from(ctx);
    scroll_y_ = std::clamp(
        scroll_y_ - wheel_y * 38.0F,
        0.0F,
        settings_max_scroll(entries.size(), KeyBindRowHeight));
    return true;
  }

private:
  f32 scroll_y_{};
  std::optional<CaptureTarget> capture_{};
};

} // namespace

std::unique_ptr<UIScreen> make_settings_menu_screen() {
  return std::make_unique<SettingsMenuScreen>();
}

std::unique_ptr<UIScreen> make_render_settings_screen() {
  return std::make_unique<RenderSettingsScreen>();
}

std::unique_ptr<UIScreen> make_key_binds_screen() {
  return std::make_unique<KeyBindsScreen>();
}

std::unique_ptr<UIScreen> make_controls_settings_screen() {
  return std::make_unique<ControlsSettingsScreen>();
}

std::unique_ptr<UIScreen> make_interface_settings_screen() {
  return std::make_unique<InterfaceSettingsScreen>();
}

} // namespace arv::ui
