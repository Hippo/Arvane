#include "arv/base/ui/screens/account.hpp"

#include "menu_common.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arv::ui {
namespace {

using screens_detail::AccentColor;
using screens_detail::ButtonColor;
using screens_detail::ButtonDisabledColor;
using screens_detail::ButtonSelectedColor;
using screens_detail::ErrorTextColor;
using screens_detail::MutedTextColor;
using screens_detail::PanelColor;
using screens_detail::TextColor;

inline constexpr f32 AccountDropdownWidth = 414.0F;
inline constexpr f32 AccountRowWidth = 370.0F;
inline constexpr f32 AccountRowHeight = 70.0F;
inline constexpr f32 AccountRowSpacing = 8.0F;
inline constexpr f32 AccountViewportMaxHeight = 302.0F;
inline constexpr f32 AccountButtonSize = 70.0F;
inline constexpr f32 AccountTextX = 76.0F;
inline constexpr f32 AccountTextWidth = 226.0F;
inline constexpr f32 SkinDialogWidth = 620.0F;
inline constexpr f32 SkinDialogHeight = 534.0F;
inline constexpr f32 SkinDialogContentWidth = 568.0F;
inline constexpr f32 SkinRowWidth = 568.0F;
inline constexpr f32 SkinRowHeight = 68.0F;
inline constexpr f32 SkinRowSpacing = 8.0F;
inline constexpr f32 SkinListViewportHeight = 318.0F;
inline constexpr f32 SkinDialogFooterY = SkinDialogHeight - 88.0F;

struct SkinManagerState {
  bool open{};
  f32 scroll_y{};
  std::vector<std::string> snapshot_order{};
  std::optional<usize> dragging_index{};
};

[[nodiscard]] bool parse_account_profile_index(std::string_view stable_id, usize& index) noexcept {
  return screens_detail::parse_index_after_prefix(
      stable_id,
      "menu.account.profile.",
      index);
}

[[nodiscard]] bool parse_index_after_prefix(
    std::string_view stable_id,
    std::string_view prefix,
    usize& index) noexcept {
  return screens_detail::parse_index_after_prefix(stable_id, prefix, index);
}

[[nodiscard]] std::vector<LocalProfileEntry> profiles_from(const UIContext& ctx) {
  return ctx.data_source == nullptr ? std::vector<LocalProfileEntry>{}
                                    : ctx.data_source->local_profiles();
}

[[nodiscard]] PlayerSkinTextureView default_skin_from(const UIContext& ctx) {
  return ctx.data_source == nullptr ? PlayerSkinTextureView{}
                                    : ctx.data_source->default_player_skin_texture();
}

[[nodiscard]] std::optional<LocalProfileEntry> profile_by_id(
    const UIContext& ctx,
    std::string_view profile_id) {
  for (LocalProfileEntry& profile : profiles_from(ctx)) {
    if (profile.profile_id == profile_id) {
      return profile;
    }
  }
  return std::nullopt;
}

[[nodiscard]] const LocalProfileEntry* active_profile(
    const std::vector<LocalProfileEntry>& profiles) noexcept {
  for (const LocalProfileEntry& profile : profiles) {
    if (profile.active) {
      return &profile;
    }
  }
  return profiles.empty() ? nullptr : &profiles.front();
}

[[nodiscard]] PlayerSkinTextureView texture_or_default(
    const LocalProfileEntry* profile,
    const UIContext& ctx) {
  if (profile != nullptr && profile->skin_texture.valid) {
    return profile->skin_texture;
  }
  return default_skin_from(ctx);
}

[[nodiscard]] std::vector<LocalProfileSkinEntry> edit_skin_entries(
    const LocalProfileEntry& profile,
    const UIContext& ctx) {
  if (ctx.data_source != nullptr &&
      ctx.data_source->has_pending_edit_profile_skins(profile.profile_id)) {
    return ctx.data_source->pending_edit_profile_skins(profile.profile_id);
  }
  if (!profile.skins.empty()) {
    return profile.skins;
  }
  if (profile.selected_skin_hash.empty()) {
    return {};
  }
  LocalProfileSkinEntry entry;
  entry.skin_hash = profile.selected_skin_hash;
  entry.skin_texture = profile.skin_texture;
  return {std::move(entry)};
}

[[nodiscard]] std::string edit_skin_hash(
    const LocalProfileEntry& profile,
    const UIContext& ctx) {
  const std::vector<LocalProfileSkinEntry> skins = edit_skin_entries(profile, ctx);
  return skins.empty() ? std::string{} : skins.front().skin_hash;
}

[[nodiscard]] bool edit_skin_slim_model(
    const LocalProfileEntry& profile,
    const UIContext& ctx) {
  const std::vector<LocalProfileSkinEntry> skins = edit_skin_entries(profile, ctx);
  return !skins.empty() && skins.front().slim_model;
}

[[nodiscard]] std::vector<LocalProfileSkinEntry> create_skin_entries(const UIContext& ctx) {
  return ctx.data_source == nullptr ? std::vector<LocalProfileSkinEntry>{}
                                    : ctx.data_source->pending_create_profile_skins();
}

[[nodiscard]] bool create_skin_slim_model(const UIContext& ctx) {
  const std::vector<LocalProfileSkinEntry> skins = create_skin_entries(ctx);
  return !skins.empty() && skins.front().slim_model;
}

[[nodiscard]] std::string short_profile_id(std::string_view id) {
  if (id.size() <= 18U) {
    return std::string{id};
  }
  return std::string{id.substr(0U, 9U)} + "..." +
         std::string{id.substr(id.size() - 6U)};
}

[[nodiscard]] usize utf8_prefix_bytes(std::string_view value, usize max_codepoints) noexcept {
  usize offset = 0;
  usize codepoints = 0;
  while (offset < value.size() && codepoints < max_codepoints) {
    const unsigned char lead = static_cast<unsigned char>(value[offset]);
    usize length = 1;
    if ((lead & 0xE0U) == 0xC0U) {
      length = 2;
    } else if ((lead & 0xF0U) == 0xE0U) {
      length = 3;
    } else if ((lead & 0xF8U) == 0xF0U) {
      length = 4;
    }
    if (offset + length > value.size()) {
      break;
    }
    offset += length;
    ++codepoints;
  }
  return offset;
}

[[nodiscard]] bool has_more_than_codepoints(
    std::string_view value,
    usize max_codepoints) noexcept {
  return utf8_prefix_bytes(value, max_codepoints) < value.size();
}

[[nodiscard]] std::string truncate_end(std::string_view value, usize max_codepoints) {
  if (!has_more_than_codepoints(value, max_codepoints)) {
    return std::string{value};
  }
  const usize prefix_bytes =
      utf8_prefix_bytes(value, max_codepoints > 3U ? max_codepoints - 3U : 0U);
  return std::string{value.substr(0U, prefix_bytes)} + "...";
}

[[nodiscard]] f32 account_content_height(usize profile_count) noexcept {
  if (profile_count == 0U) {
    return 34.0F;
  }
  return static_cast<f32>(profile_count) * AccountRowHeight +
         static_cast<f32>(profile_count - 1U) * AccountRowSpacing;
}

[[nodiscard]] f32 account_max_scroll(usize profile_count) noexcept {
  const f32 content_height = account_content_height(profile_count);
  const f32 viewport_height = std::min(content_height, AccountViewportMaxHeight);
  return std::max(0.0F, content_height - viewport_height);
}

[[nodiscard]] f32 skin_list_content_height(usize skin_count) noexcept {
  if (skin_count == 0U) {
    return 58.0F;
  }
  return static_cast<f32>(skin_count) * SkinRowHeight +
         static_cast<f32>(skin_count - 1U) * SkinRowSpacing;
}

[[nodiscard]] f32 skin_list_max_scroll(usize skin_count) noexcept {
  return std::max(0.0F, skin_list_content_height(skin_count) - SkinListViewportHeight);
}

[[nodiscard]] std::string short_skin_hash(std::string_view hash) {
  if (hash.size() <= 22U) {
    return std::string{hash};
  }
  return std::string{hash.substr(0U, 12U)} + "..." +
         std::string{hash.substr(hash.size() - 7U)};
}

[[nodiscard]] std::vector<std::string> skin_hash_order(
    const std::vector<LocalProfileSkinEntry>& skins) {
  std::vector<std::string> order;
  order.reserve(skins.size());
  for (const LocalProfileSkinEntry& skin : skins) {
    order.push_back(skin.skin_hash);
  }
  return order;
}

void submit_skin_order(
    UIContext& ctx,
    std::optional<std::string> profile_id,
    std::vector<std::string> skin_hashes) {
  screens_detail::submit_command(
      ctx,
      SetLocalProfileSkinOrderCommand{std::move(profile_id), std::move(skin_hashes)});
}

[[nodiscard]] std::vector<std::string> order_after_move(
    const std::vector<LocalProfileSkinEntry>& skins,
    usize from,
    usize to) {
  std::vector<std::string> order = skin_hash_order(skins);
  if (from >= order.size() || to >= order.size() || from == to) {
    return order;
  }
  std::string moved = std::move(order[from]);
  order.erase(order.begin() + static_cast<std::vector<std::string>::difference_type>(from));
  order.insert(
      order.begin() + static_cast<std::vector<std::string>::difference_type>(to),
      std::move(moved));
  return order;
}

[[nodiscard]] std::vector<std::string> order_without(
    const std::vector<LocalProfileSkinEntry>& skins,
    usize index) {
  std::vector<std::string> order = skin_hash_order(skins);
  if (index < order.size()) {
    order.erase(order.begin() + static_cast<std::vector<std::string>::difference_type>(index));
  }
  return order;
}

[[nodiscard]] UiImageData skin_patch(
    PlayerSkinTextureView texture,
    f32 logical_x,
    f32 logical_y,
    f32 logical_width,
    f32 logical_height,
    Vec4f tint = {1.0F, 1.0F, 1.0F, 1.0F}) {
  if (!texture.valid || texture.atlas_width == 0U || texture.atlas_height == 0U ||
      texture.image_width == 0U || texture.image_height == 0U) {
    return {};
  }

  const f32 scale_x = static_cast<f32>(texture.image_width) / 64.0F;
  const f32 scale_y = static_cast<f32>(texture.image_height) / 64.0F;
  const f32 px = logical_x * scale_x;
  const f32 py = logical_y * scale_y;
  const f32 pw = logical_width * scale_x;
  const f32 ph = logical_height * scale_y;
  if (px < 0.0F || py < 0.0F ||
      px + pw > static_cast<f32>(texture.image_width) ||
      py + ph > static_cast<f32>(texture.image_height)) {
    return {};
  }

  const f32 atlas_w = static_cast<f32>(texture.atlas_width);
  const f32 atlas_h = static_cast<f32>(texture.atlas_height);
  const f32 u0 = (static_cast<f32>(texture.x) + px) / atlas_w;
  const f32 v0 = (static_cast<f32>(texture.y) + py) / atlas_h;
  const f32 u1 = (static_cast<f32>(texture.x) + px + pw) / atlas_w;
  const f32 v1 = (static_cast<f32>(texture.y) + py + ph) / atlas_h;

  UiImageData image;
  image.valid = true;
  image.source = render::UiTextureSource::PlayerSkinAtlas;
  image.page = texture.page;
  image.uvs = {{{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}}};
  image.tint = tint;
  return image;
}

UiNodeID add_image(
    UiTree& tree,
    UiNodeID parent,
    std::string stable_id,
    UiImageData image,
    Vec2f offset,
    Vec2f size) {
  UiElement element;
  element.stable_id = std::move(stable_id);
  element.type = UiElementType::Image;
  element.layout.width_mode = UiSizeMode::Fixed;
  element.layout.height_mode = UiSizeMode::Fixed;
  element.layout.width = size.x;
  element.layout.height = size.y;
  element.layout.offset = offset;
  element.hit_test = false;
  element.image = std::move(image);
  return tree.add_child(parent, std::move(element));
}

void add_text_at(
    UiTree& tree,
    UiNodeID parent,
    std::string stable_id,
    std::string text,
    Vec2f offset,
    Vec4f color) {
  UiElement element =
      screens_detail::make_text(std::move(stable_id), std::move(text), color);
  element.layout.offset = offset;
  tree.add_child(parent, std::move(element));
}

UiNodeID add_clip_panel(
    UiTree& tree,
    UiNodeID parent,
    std::string stable_id,
    Vec2f offset,
    Vec2f size) {
  UiElement panel = screens_detail::make_panel(
      std::move(stable_id),
      size.x,
      size.y,
      {0.0F, 0.0F, 0.0F, 0.0F});
  panel.layout.offset = offset;
  panel.hit_test = false;
  panel.clip_children = true;
  return tree.add_child(parent, std::move(panel));
}

void add_clipped_text_at(
    UiTree& tree,
    UiNodeID parent,
    std::string stable_id,
    std::string text,
    Vec2f offset,
    Vec2f clip_size,
    Vec4f color) {
  const UiNodeID clip_id =
      add_clip_panel(tree, parent, stable_id + ".clip", offset, clip_size);
  add_text_at(
      tree,
      clip_id,
      std::move(stable_id),
      std::move(text),
      {0.0F, 0.0F},
      color);
}

void add_skin_portrait(
    UIContext& ctx,
    UiNodeID parent,
    std::string_view id,
    PlayerSkinTextureView texture,
    Vec2f origin,
    f32 size) {
  UiTree& tree = ctx.tree;
  if (!texture.valid) {
    add_text_at(
        tree,
        parent,
        std::string{id} + ".fallback",
        screens_detail::tr(ctx, "ui.common.unknown"),
        {origin.x + size * 0.42F, origin.y + size * 0.34F},
        MutedTextColor);
    return;
  }

  add_image(
      tree,
      parent,
      std::string{id} + ".head",
      skin_patch(texture, 8.0F, 8.0F, 8.0F, 8.0F),
      origin,
      {size, size});
  add_image(
      tree,
      parent,
      std::string{id} + ".head_overlay",
      skin_patch(texture, 40.0F, 8.0F, 8.0F, 8.0F),
      origin,
      {size, size});
}

UiNodeID add_account_toggle_button(
    UIContext& ctx,
    PlayerSkinTextureView texture,
    bool open) {
  const UiTheme& theme = ctx.tree.theme();
  UiElement button;
  button.stable_id = "menu.account.toggle";
  button.type = UiElementType::Button;
  button.layout.width_mode = UiSizeMode::Fixed;
  button.layout.height_mode = UiSizeMode::Fixed;
  button.layout.width = AccountButtonSize;
  button.layout.height = AccountButtonSize;
  button.layout.anchor = UiAnchor::TopRight;
  button.layout.offset = {-22.0F, 22.0F};
  screens_detail::apply_control_style(button, theme, UiControlRole::Neutral, true, open);
  button.focusable = true;
  button.action = "account.toggle";
  const UiNodeID id = ctx.tree.add_child(ctx.tree.root_id(), std::move(button));
  add_skin_portrait(ctx, id, "menu.account.toggle.skin", texture, {11.0F, 11.0F}, 48.0F);
  return id;
}

UiNodeID add_account_row(
    UIContext& ctx,
    UiNodeID parent,
    const LocalProfileEntry& profile,
    PlayerSkinTextureView default_texture,
    usize index) {
  UiTree& tree = ctx.tree;
  const UiTheme& theme = tree.theme();
  const std::string prefix = "menu.account.profile." + std::to_string(index);
  UiElement row;
  row.stable_id = prefix;
  row.type = UiElementType::Button;
  row.layout.width_mode = UiSizeMode::Fixed;
  row.layout.height_mode = UiSizeMode::Fixed;
  row.layout.width = AccountRowWidth;
  row.layout.height = AccountRowHeight;
  screens_detail::apply_control_style(row, theme, UiControlRole::Neutral, true, profile.active);
  row.focusable = true;
  row.selected = profile.active;
  row.action = "account.select";
  const UiNodeID row_id = tree.add_child(parent, std::move(row));

  const PlayerSkinTextureView texture =
      profile.skin_texture.valid ? profile.skin_texture : default_texture;
  add_skin_portrait(ctx, row_id, prefix + ".skin", texture, {12.0F, 11.0F}, 48.0F);
  add_clipped_text_at(
      tree,
      row_id,
      prefix + ".name",
      truncate_end(profile.display_name, 24U),
      {AccountTextX, 10.0F},
      {AccountTextWidth, 26.0F},
      theme.colors.text);
  add_clipped_text_at(
      tree,
      row_id,
      prefix + ".id",
      short_profile_id(profile.profile_id),
      {AccountTextX, 36.0F},
      {AccountTextWidth, 24.0F},
      theme.colors.text_muted);

  UiElement edit;
  edit.stable_id = prefix + ".edit";
  edit.type = UiElementType::Button;
  edit.layout.width_mode = UiSizeMode::Fixed;
  edit.layout.height_mode = UiSizeMode::Fixed;
  edit.layout.width = 46.0F;
  edit.layout.height = 34.0F;
  edit.layout.anchor = UiAnchor::TopRight;
  edit.layout.offset = {-10.0F, 18.0F};
  edit.layout.padding = {8.0F, 8.0F, 8.0F, 8.0F};
  screens_detail::apply_control_style(edit, theme, UiControlRole::Neutral, true, false);
  edit.focusable = true;
  edit.action = "account.edit";
  const UiNodeID edit_id = tree.add_child(row_id, std::move(edit));
  UiElement edit_label =
      screens_detail::make_text(
          prefix + ".edit.label",
          screens_detail::tr(ctx, "ui.menu.account.edit"),
          theme.colors.text);
  edit_label.layout.anchor = UiAnchor::Center;
  tree.add_child(edit_id, std::move(edit_label));
  return row_id;
}

void add_account_dropdown(
    UIContext& ctx,
    const std::vector<LocalProfileEntry>& profiles,
    PlayerSkinTextureView default_texture,
    f32 scroll_y) {
  const UiTheme& theme = ctx.tree.theme();
  const f32 content_height = account_content_height(profiles.size());
  const f32 viewport_height = std::min(content_height, AccountViewportMaxHeight);
  const f32 panel_height = 22.0F + viewport_height + 12.0F + 44.0F + 22.0F;

  UiElement panel = screens_detail::make_panel(
      "menu.account.dropdown",
      AccountDropdownWidth,
      panel_height,
      theme.colors.surface);
  panel.layout.anchor = UiAnchor::TopRight;
  panel.layout.offset = {-22.0F, 100.0F};
  panel.layout.padding = {
      theme.widgets.panel.padding,
      theme.widgets.panel.padding,
      theme.widgets.panel.padding,
      theme.widgets.panel.padding};
  screens_detail::apply_panel_style(panel, theme, true);
  const UiNodeID panel_id = ctx.tree.add_child(ctx.tree.root_id(), std::move(panel));

  UiElement viewport = screens_detail::make_panel(
      "menu.account.dropdown.viewport",
      AccountRowWidth,
      viewport_height,
      {0.0F, 0.0F, 0.0F, 0.0F});
  viewport.clip_children = true;
  viewport.hit_test = true;
  const UiNodeID viewport_id = ctx.tree.add_child(panel_id, std::move(viewport));

  UiElement stack = screens_detail::make_stack("menu.account.dropdown.rows", AccountRowSpacing);
  stack.layout.offset = {0.0F, -std::clamp(scroll_y, 0.0F, account_max_scroll(profiles.size()))};
  const UiNodeID stack_id = ctx.tree.add_child(viewport_id, std::move(stack));

  if (profiles.empty()) {
    ctx.tree.add_child(
        stack_id,
        screens_detail::make_text(
            "menu.account.empty",
            screens_detail::tr(ctx, "ui.menu.account.empty"),
            theme.colors.text_muted,
            UiTextRole::Small));
  } else {
    for (usize index = 0; index < profiles.size(); ++index) {
      add_account_row(ctx, stack_id, profiles[index], default_texture, index);
    }
  }

  UiElement add_button;
  add_button.stable_id = "menu.account.create";
  add_button.type = UiElementType::Button;
  add_button.layout.width_mode = UiSizeMode::Fixed;
  add_button.layout.height_mode = UiSizeMode::Fixed;
  add_button.layout.width = AccountRowWidth;
  add_button.layout.height = 44.0F;
  add_button.layout.offset = {0.0F, viewport_height + 12.0F};
  add_button.layout.padding = {12.0F, 12.0F, 11.0F, 9.0F};
  screens_detail::apply_control_style(add_button, theme, UiControlRole::Primary, true, false);
  add_button.focusable = true;
  add_button.action = "account.create";
  const UiNodeID add_id = ctx.tree.add_child(panel_id, std::move(add_button));
  UiElement add_label = screens_detail::make_text(
      "menu.account.create.label",
      screens_detail::tr(ctx, "ui.menu.account.add_profile"),
      theme.colors.text);
  add_label.layout.anchor = UiAnchor::Center;
  ctx.tree.add_child(add_id, std::move(add_label));
}

UiNodeID add_fixed_action_button(
    UiTree& tree,
    UiNodeID parent,
    std::string stable_id,
    std::string label,
    std::string action,
    UiAnchor anchor,
    Vec2f offset,
    f32 width,
    Vec4f background = ButtonColor) {
  const UiControlRole role =
      background == ButtonSelectedColor ? UiControlRole::Primary :
      background.x > 0.25F && background.y < 0.12F ? UiControlRole::Danger :
      UiControlRole::Neutral;
  const UiNodeID button_id =
      screens_detail::add_button(
          tree,
          parent,
          stable_id,
          std::move(label),
          std::move(action),
          true,
          false,
          role);
  if (UiElement* button = tree.element(button_id); button != nullptr) {
    button->layout.width = width;
    button->layout.anchor = anchor;
    button->layout.offset = offset;
    if (background != ButtonColor && background != ButtonSelectedColor) {
      button->style.background = background;
    }
  }
  return button_id;
}

UiNodeID add_dialog_button(
    UiTree& tree,
    UiNodeID parent,
    std::string stable_id,
    std::string label,
    std::string action,
    Vec2f offset,
    f32 width,
    f32 height,
    Vec4f background = ButtonColor,
    bool enabled = true,
    bool selected = false) {
  const UiControlRole role =
      background == ButtonSelectedColor ? UiControlRole::Primary :
      background.x > 0.25F && background.y < 0.12F ? UiControlRole::Danger :
      UiControlRole::Neutral;
  const UiNodeID button_id =
      screens_detail::add_button(
          tree,
          parent,
          stable_id,
          std::move(label),
          std::move(action),
          enabled,
          selected,
          role);
  if (UiElement* button = tree.element(button_id); button != nullptr) {
    button->layout.width = width;
    button->layout.height = height;
    button->layout.anchor = UiAnchor::TopLeft;
    button->layout.offset = offset;
    button->layout.padding = {10.0F, 10.0F, 8.0F, 8.0F};
    if (background != ButtonColor && background != ButtonSelectedColor) {
      button->style.background = enabled ? background : ButtonDisabledColor;
    }
  }
  return button_id;
}

void add_skin_manager_row(
    UIContext& ctx,
    UiNodeID parent,
    std::string_view base_id,
    const LocalProfileSkinEntry& skin,
    usize index,
    usize count,
    PlayerSkinTextureView default_texture) {
  UiTree& tree = ctx.tree;
  const UiTheme& theme = tree.theme();
  const std::string prefix =
      std::string{base_id} + ".row." + std::to_string(index);
  UiElement row;
  row.stable_id = prefix;
  row.type = UiElementType::Button;
  row.layout.width_mode = UiSizeMode::Fixed;
  row.layout.height_mode = UiSizeMode::Fixed;
  row.layout.width = SkinRowWidth;
  row.layout.height = SkinRowHeight;
  screens_detail::apply_control_style(row, theme, UiControlRole::Neutral, true, index == 0U);
  row.focusable = true;
  row.action = "account.skin_drag";
  const UiNodeID row_id = tree.add_child(parent, std::move(row));

  const PlayerSkinTextureView texture =
      skin.skin_texture.valid ? skin.skin_texture : default_texture;
  add_skin_portrait(ctx, row_id, prefix + ".skin", texture, {14.0F, 10.0F}, 48.0F);
  add_text_at(
      tree,
      row_id,
      prefix + ".rank",
      "#" + std::to_string(index + 1U),
      {72.0F, 10.0F},
      index == 0U ? theme.colors.accent : theme.colors.text_muted);
  add_clipped_text_at(
      tree,
      row_id,
      prefix + ".hash",
      short_skin_hash(skin.skin_hash),
      {112.0F, 9.0F},
      {178.0F, 25.0F},
      theme.colors.text);
  add_clipped_text_at(
      tree,
      row_id,
      prefix + ".details",
      screens_detail::tr(
          ctx,
          "ui.menu.account.skin_dimensions",
          {{"width", std::to_string(skin.width)}, {"height", std::to_string(skin.height)}}),
      {112.0F, 36.0F},
      {178.0F, 24.0F},
      theme.colors.text_muted);

  add_dialog_button(
      tree,
      row_id,
      prefix + ".slim",
      skin.slim_model ? screens_detail::tr(ctx, "ui.menu.account.skin_slim")
                      : screens_detail::tr(ctx, "ui.menu.account.skin_classic"),
      "account.skin_toggle_slim",
      {292.0F, 17.0F},
      76.0F,
      34.0F,
      ButtonSelectedColor,
      true,
      skin.slim_model);
  add_dialog_button(
      tree,
      row_id,
      prefix + ".up",
      screens_detail::tr(ctx, "ui.menu.account.up"),
      "account.skin_up",
      {376.0F, 17.0F},
      44.0F,
      34.0F,
      {0.122F, 0.138F, 0.154F, 0.98F},
      index > 0U);
  add_dialog_button(
      tree,
      row_id,
      prefix + ".down",
      screens_detail::tr(ctx, "ui.menu.account.down"),
      "account.skin_down",
      {426.0F, 17.0F},
      58.0F,
      34.0F,
      {0.122F, 0.138F, 0.154F, 0.98F},
      index + 1U < count);
  add_dialog_button(
      tree,
      row_id,
      prefix + ".remove",
      screens_detail::tr(ctx, "ui.menu.common.remove"),
      "account.skin_remove",
      {492.0F, 17.0F},
      76.0F,
      34.0F,
      {0.300F, 0.070F, 0.065F, 0.96F});
}

void build_skin_manager_dialog(
    UIContext& ctx,
    std::string_view base_id,
    const std::vector<LocalProfileSkinEntry>& skins,
    f32 scroll_y) {
  const UiTheme& theme = ctx.tree.theme();
  screens_detail::add_backdrop(
      ctx.tree,
      std::string{base_id} + ".backdrop",
      {0.0F, 0.0F, 0.0F, 0.58F});

  UiElement panel = screens_detail::make_panel(
      std::string{base_id} + ".panel",
      SkinDialogWidth,
      SkinDialogHeight,
      theme.colors.surface);
  panel.layout.anchor = UiAnchor::Center;
  panel.layout.padding = {
      theme.widgets.panel.padding,
      theme.widgets.panel.padding,
      theme.widgets.panel.padding,
      theme.widgets.panel.padding};
  screens_detail::apply_panel_style(panel, theme, true);
  const UiNodeID panel_id = ctx.tree.add_child(ctx.tree.root_id(), std::move(panel));

  add_text_at(
      ctx.tree,
      panel_id,
      std::string{base_id} + ".title",
      screens_detail::tr(ctx, "ui.menu.account.manage_skins"),
      {0.0F, 0.0F},
      theme.colors.text);

  UiElement viewport = screens_detail::make_panel(
      std::string{base_id} + ".viewport",
      SkinRowWidth,
      SkinListViewportHeight,
      theme.colors.surface_alt);
  viewport.layout.offset = {0.0F, 58.0F};
  viewport.style.radius = theme.layout.radius_md;
  viewport.style.border = theme.colors.border;
  viewport.style.border_width = theme.layout.border_width;
  viewport.clip_children = true;
  viewport.hit_test = true;
  const UiNodeID viewport_id = ctx.tree.add_child(panel_id, std::move(viewport));

  UiElement stack = screens_detail::make_stack(std::string{base_id} + ".rows", SkinRowSpacing);
  stack.layout.offset = {0.0F, -std::clamp(scroll_y, 0.0F, skin_list_max_scroll(skins.size()))};
  const UiNodeID stack_id = ctx.tree.add_child(viewport_id, std::move(stack));

  if (skins.empty()) {
    add_text_at(
        ctx.tree,
        stack_id,
        std::string{base_id} + ".empty",
        screens_detail::tr(ctx, "ui.menu.account.no_skins"),
        {20.0F, 20.0F},
        MutedTextColor);
  } else {
    const PlayerSkinTextureView default_texture = default_skin_from(ctx);
    for (usize index = 0; index < skins.size(); ++index) {
      add_skin_manager_row(
          ctx,
          stack_id,
          base_id,
          skins[index],
          index,
          skins.size(),
          default_texture);
    }
  }

  add_dialog_button(
      ctx.tree,
      panel_id,
      std::string{base_id} + ".add",
      screens_detail::tr(ctx, "ui.menu.account.add_skin"),
      "account.skin_add",
      {0.0F, SkinDialogFooterY},
      132.0F,
      40.0F,
      ButtonSelectedColor,
      true,
      false);
  add_dialog_button(
      ctx.tree,
      panel_id,
      std::string{base_id} + ".cancel",
      screens_detail::tr(ctx, "ui.menu.common.cancel"),
      "account.skin_cancel",
      {SkinDialogContentWidth - 244.0F, SkinDialogFooterY},
      112.0F,
      40.0F);
  add_dialog_button(
      ctx.tree,
      panel_id,
      std::string{base_id} + ".done",
      screens_detail::tr(ctx, "ui.menu.common.done"),
      "account.skin_done",
      {SkinDialogContentWidth - 120.0F, SkinDialogFooterY},
      120.0F,
      40.0F,
      ButtonSelectedColor,
      true,
      false);
}

[[nodiscard]] std::string skin_row_prefix(std::string_view base_id) {
  return std::string{base_id} + ".row.";
}

void open_skin_manager(
    SkinManagerState& state,
    const std::vector<LocalProfileSkinEntry>& skins) {
  state.open = true;
  state.scroll_y = std::clamp(state.scroll_y, 0.0F, skin_list_max_scroll(skins.size()));
  state.snapshot_order = skin_hash_order(skins);
  state.dragging_index.reset();
}

void close_skin_manager(SkinManagerState& state) {
  state.open = false;
  state.scroll_y = 0.0F;
  state.snapshot_order.clear();
  state.dragging_index.reset();
}

bool cancel_skin_manager(
    UIContext& ctx,
    SkinManagerState& state,
    std::optional<std::string> profile_id) {
  submit_skin_order(ctx, std::move(profile_id), state.snapshot_order);
  close_skin_manager(state);
  return true;
}

[[nodiscard]] bool skin_index_at_cursor(
    const UiTree& tree,
    std::string_view base_id,
    usize skin_count,
    Vec2f cursor,
    usize& index) {
  const std::string prefix = skin_row_prefix(base_id);
  for (usize candidate = 0; candidate < skin_count; ++candidate) {
    const UiElement* row =
        tree.find_by_stable_id(prefix + std::to_string(candidate));
    if (row != nullptr && contains(row->rect, cursor)) {
      index = candidate;
      return true;
    }
  }
  return false;
}

bool handle_skin_manager_action(
    const UiAction& action,
    UIContext& ctx,
    SkinManagerState& state,
    std::string_view base_id,
    std::optional<std::string> profile_id,
    const std::vector<LocalProfileSkinEntry>& skins) {
  if (!state.open) {
    return false;
  }
  if (action.action_id == "account.skin_add") {
    screens_detail::submit_command(ctx, PickLocalProfileSkinCommand{std::move(profile_id)});
    return true;
  }
  if (action.action_id == "account.skin_done") {
    close_skin_manager(state);
    return true;
  }
  if (action.action_id == "account.skin_cancel") {
    return cancel_skin_manager(ctx, state, std::move(profile_id));
  }

  usize index = 0;
  if (!parse_index_after_prefix(action.element_id, skin_row_prefix(base_id), index) ||
      index >= skins.size()) {
    return false;
  }
  if (action.action_id == "account.skin_drag") {
    state.dragging_index = index;
    return true;
  }
  if (action.action_id == "account.skin_toggle_slim") {
    screens_detail::submit_command(
        ctx,
        SetLocalProfileSkinSlimCommand{
            std::move(profile_id),
            skins[index].skin_hash,
            !skins[index].slim_model});
    return true;
  }
  if (action.action_id == "account.skin_up" && index > 0U) {
    submit_skin_order(
        ctx,
        std::move(profile_id),
        order_after_move(skins, index, index - 1U));
    return true;
  }
  if (action.action_id == "account.skin_down" && index + 1U < skins.size()) {
    submit_skin_order(
        ctx,
        std::move(profile_id),
        order_after_move(skins, index, index + 1U));
    return true;
  }
  if (action.action_id == "account.skin_remove") {
    submit_skin_order(ctx, std::move(profile_id), order_without(skins, index));
    return true;
  }
  return false;
}

bool handle_skin_manager_scroll(
    Vec2f cursor,
    f32 wheel_y,
    UIContext& ctx,
    SkinManagerState& state,
    std::string_view base_id,
    usize skin_count) {
  if (!state.open || wheel_y == 0.0F) {
    return false;
  }
  const UiElement* viewport =
      ctx.tree.find_by_stable_id(std::string{base_id} + ".viewport");
  if (viewport == nullptr || !contains(viewport->rect, cursor)) {
    return false;
  }
  const f32 max_scroll = skin_list_max_scroll(skin_count);
  if (max_scroll <= 0.0F) {
    state.scroll_y = 0.0F;
    return true;
  }
  state.scroll_y = std::clamp(state.scroll_y - wheel_y * 36.0F, 0.0F, max_scroll);
  return true;
}

bool handle_skin_manager_pointer(
    Vec2f cursor,
    bool left_released,
    bool left_held,
    UIContext& ctx,
    SkinManagerState& state,
    std::string_view base_id,
    std::optional<std::string> profile_id,
    const std::vector<LocalProfileSkinEntry>& skins) {
  if (!state.open) {
    return false;
  }
  if (left_released || !left_held) {
    state.dragging_index.reset();
    return false;
  }
  if (!state.dragging_index.has_value() || skins.size() < 2U) {
    return false;
  }

  usize target = 0;
  if (!skin_index_at_cursor(ctx.tree, base_id, skins.size(), cursor, target)) {
    return true;
  }
  if (target != state.dragging_index.value()) {
    submit_skin_order(
        ctx,
        std::move(profile_id),
        order_after_move(skins, state.dragging_index.value(), target));
    state.dragging_index = target;
  }
  return true;
}

void add_fullscreen_account_base(UIContext& ctx, std::string_view id, std::string title) {
  const UiTheme& theme = ctx.tree.theme();
  screens_detail::add_backdrop(
      ctx.tree,
      std::string{id} + ".backdrop",
      theme.colors.backdrop);

  UiElement left = screens_detail::make_panel(
      std::string{id} + ".left",
      390.0F,
      ctx.viewport.y,
      theme.colors.surface);
  left.layout.anchor = UiAnchor::TopLeft;
  left.layout.padding = {
      theme.layout.spacing_xl,
      theme.layout.spacing_xl,
      theme.layout.spacing_xl,
      theme.layout.spacing_xl};
  screens_detail::apply_panel_style(left, theme, true);
  const UiNodeID left_id = ctx.tree.add_child(ctx.tree.root_id(), std::move(left));

  UiElement stack = screens_detail::make_stack(std::string{id} + ".controls", theme.layout.spacing_md);
  const UiNodeID stack_id = ctx.tree.add_child(left_id, std::move(stack));
  ctx.tree.add_child(
      stack_id,
      screens_detail::make_text(
          std::string{id} + ".title",
          std::move(title),
          theme.colors.text,
          UiTextRole::Title));
}

class AccountEditScreen final : public UIScreen {
public:
  explicit AccountEditScreen(std::string profile_id) : profile_id_(std::move(profile_id)) {
  }

  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::AccountEdit;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "account_edit";
  }

  void build(UIContext& ctx) override {
    const std::optional<LocalProfileEntry> profile = profile_by_id(ctx, profile_id_);
    if (!profile.has_value()) {
      if (ctx.screens != nullptr) {
        ctx.screens->pop();
      }
      return;
    }
    if (!initialized_) {
      display_name_ = profile->display_name;
      initialized_ = true;
    }

    add_fullscreen_account_base(ctx, screen_id(), screens_detail::tr(ctx, "ui.menu.account.edit_profile"));
    const UiNodeID stack = ctx.tree.find_by_stable_id("account_edit.controls")->node_id;
    screens_detail::add_text_box(
        ctx.tree,
        stack,
        "menu.account_edit.name",
        screens_detail::tr(ctx, "ui.menu.account.display_name_label"),
        display_name_,
        screens_detail::tr(ctx, "ui.menu.account.display_name_placeholder"),
        "account.focus_name",
        true);
    screens_detail::add_button(
        ctx.tree,
        stack,
        "menu.account_edit.skin",
        screens_detail::tr(ctx, "ui.menu.account.manage_skins"),
        "account.manage_skins");
    const std::string status =
        ctx.data_source == nullptr ? std::string{} : ctx.data_source->local_profile_status();
    screens_detail::add_error_text(ctx.tree, stack, "menu.account_edit.status", status);

    UiElement preview = screens_detail::make_panel(
        "menu.account_edit.preview",
        360.0F,
        512.0F,
        {0.0F, 0.0F, 0.0F, 0.0F});
    preview.layout.anchor = UiAnchor::Center;
    preview.layout.offset = {210.0F, 0.0F};
    preview.hit_test = true;
    (void)ctx.tree.add_child(ctx.tree.root_id(), std::move(preview));

    screens_detail::add_button(
        ctx.tree,
        ctx.tree.root_id(),
        "menu.account_edit.delete",
        screens_detail::tr(ctx, "ui.menu.account.delete_profile"),
        "account.delete");
    if (UiElement* del = ctx.tree.find_by_stable_id("menu.account_edit.delete");
        del != nullptr) {
      del->layout.anchor = UiAnchor::BottomLeft;
      del->layout.offset = {28.0F, -28.0F};
      screens_detail::apply_control_style(
          *del,
          ctx.tree.theme(),
          UiControlRole::Danger,
          true,
          false);
    }

    add_fixed_action_button(
        ctx.tree,
        ctx.tree.root_id(),
        "menu.account_edit.cancel",
        screens_detail::tr(ctx, "ui.menu.common.cancel"),
        "account.cancel",
        UiAnchor::BottomRight,
        {-182.0F, -28.0F},
        142.0F);
    add_fixed_action_button(
        ctx.tree,
        ctx.tree.root_id(),
        "menu.account_edit.save",
        screens_detail::tr(ctx, "ui.menu.common.save"),
        "account.save",
        UiAnchor::BottomRight,
        {-28.0F, -28.0F},
        142.0F,
        ButtonSelectedColor);

    if (confirm_delete_) {
      build_delete_confirmation(ctx, profile.value());
    } else if (skin_manager_.open) {
      build_skin_manager_dialog(
          ctx,
          skin_dialog_id(),
          current_skins(ctx),
          skin_manager_.scroll_y);
    }
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (skin_manager_.open) {
      return handle_skin_manager_action(
          action,
          ctx,
          skin_manager_,
          skin_dialog_id(),
          profile_id_,
          current_skins(ctx));
    }
    if (action.action_id == "account.save") {
      return save(ctx);
    }
    if (action.action_id == "account.cancel") {
      return cancel(ctx);
    }
    if (action.action_id == "account.manage_skins") {
      open_skin_manager(skin_manager_, current_skins(ctx));
      return true;
    }
    if (action.action_id == "account.delete") {
      confirm_delete_ = true;
      return true;
    }
    if (action.action_id == "account.confirm_delete") {
      screens_detail::submit_command(ctx, DeleteLocalProfileCommand{profile_id_});
      if (ctx.screens != nullptr) {
        ctx.screens->pop();
      }
      return true;
    }
    if (action.action_id == "account.cancel_delete") {
      confirm_delete_ = false;
      return true;
    }
    return action.action_id == "account.focus_name";
  }

  bool handle_text_input(std::string_view text, UIContext& ctx) override {
    (void)ctx;
    if (confirm_delete_ || skin_manager_.open) {
      return false;
    }
    display_name_.append(text.substr(0U, std::min<usize>(text.size(), 32U)));
    if (display_name_.size() > 32U) {
      display_name_.resize(32U);
    }
    return true;
  }

  bool handle_backspace(UIContext& ctx) override {
    (void)ctx;
    if (confirm_delete_ || skin_manager_.open || display_name_.empty()) {
      return false;
    }
    screens_detail::erase_last_utf8_codepoint(display_name_);
    return true;
  }

  bool handle_submit(UIContext& ctx) override {
    if (skin_manager_.open) {
      close_skin_manager(skin_manager_);
      return true;
    }
    return save(ctx);
  }

  bool handle_cancel(UIContext& ctx) override {
    if (confirm_delete_) {
      confirm_delete_ = false;
      return true;
    }
    if (skin_manager_.open) {
      return cancel_skin_manager(ctx, skin_manager_, profile_id_);
    }
    return save(ctx);
  }

  bool handle_scroll(Vec2f cursor, f32 wheel_y, UIContext& ctx) override {
    return handle_skin_manager_scroll(
        cursor,
        wheel_y,
        ctx,
        skin_manager_,
        skin_dialog_id(),
        current_skins(ctx).size());
  }

  bool handle_pointer(
      Vec2f cursor,
      Vec2f delta,
      bool left_pressed,
      bool left_released,
      bool left_held,
      UIContext& ctx) override {
    if (skin_manager_.open) {
      (void)handle_skin_manager_pointer(
          cursor,
          left_released,
          left_held,
          ctx,
          skin_manager_,
          skin_dialog_id(),
          profile_id_,
          current_skins(ctx));
      return true;
    }
    const UiElement* preview = ctx.tree.find_by_stable_id("menu.account_edit.preview");
    const bool over_preview = preview != nullptr && contains(preview->rect, cursor);
    if (left_pressed && over_preview) {
      dragging_preview_ = true;
      last_drag_cursor_ = cursor;
    }
    if (left_released || !left_held) {
      dragging_preview_ = false;
      last_drag_cursor_.reset();
    }
    if (dragging_preview_ && left_held) {
      const f32 drag_delta =
          last_drag_cursor_.has_value() ? cursor.x - last_drag_cursor_->x : delta.x;
      last_drag_cursor_ = cursor;
      preview_yaw_ += drag_delta * 0.014F;
      return true;
    }
    return false;
  }

  [[nodiscard]] std::optional<PlayerSkinPreviewRequest> player_skin_preview(
      const UIContext& ctx) const override {
    if (confirm_delete_ || skin_manager_.open) {
      return std::nullopt;
    }
    const std::optional<LocalProfileEntry> profile = profile_by_id(ctx, profile_id_);
    if (!profile.has_value()) {
      return std::nullopt;
    }
    return PlayerSkinPreviewRequest{
        "menu.account_edit.preview",
        edit_skin_hash(profile.value(), ctx),
        preview_yaw_,
        edit_skin_slim_model(profile.value(), ctx)};
  }

private:
  [[nodiscard]] static std::string_view skin_dialog_id() noexcept {
    return "menu.account_edit.skin_dialog";
  }

  [[nodiscard]] std::vector<LocalProfileSkinEntry> current_skins(
      const UIContext& ctx) const {
    const std::optional<LocalProfileEntry> profile = profile_by_id(ctx, profile_id_);
    return profile.has_value() ? edit_skin_entries(profile.value(), ctx)
                               : std::vector<LocalProfileSkinEntry>{};
  }

  bool save(UIContext& ctx) {
    screens_detail::submit_command(
        ctx,
        UpdateLocalProfileCommand{
            profile_id_,
            screens_detail::trim_copy(display_name_).empty() ? "local_player" : display_name_});
    if (ctx.screens != nullptr) {
      ctx.screens->pop();
    }
    return true;
  }

  bool cancel(UIContext& ctx) {
    screens_detail::submit_command(ctx, DiscardLocalProfileEditCommand{profile_id_});
    if (ctx.screens != nullptr) {
      ctx.screens->pop();
    }
    return true;
  }

  void build_delete_confirmation(UIContext& ctx, const LocalProfileEntry& profile) {
    const UiTheme& theme = ctx.tree.theme();
    screens_detail::add_backdrop(
        ctx.tree,
        "menu.account_edit.confirm.backdrop",
        {0.0F, 0.0F, 0.0F, 0.56F});
    UiElement panel = screens_detail::make_panel(
        "menu.account_edit.confirm.panel",
        430.0F,
        206.0F,
        theme.colors.surface);
    panel.layout.anchor = UiAnchor::Center;
    panel.layout.padding = {
        theme.widgets.panel.padding,
        theme.widgets.panel.padding,
        theme.widgets.panel.padding,
        theme.widgets.panel.padding};
    screens_detail::apply_panel_style(panel, theme, true);
    const UiNodeID panel_id = ctx.tree.add_child(ctx.tree.root_id(), std::move(panel));
    UiElement stack = screens_detail::make_stack("menu.account_edit.confirm.stack", theme.layout.spacing_md);
    const UiNodeID stack_id = ctx.tree.add_child(panel_id, std::move(stack));
    ctx.tree.add_child(
        stack_id,
        screens_detail::make_text(
            "menu.account_edit.confirm.text",
            screens_detail::tr(
                ctx,
                "ui.menu.account.confirm_delete",
                {{"name", profile.display_name}}),
            theme.colors.danger,
            UiTextRole::Heading));
    screens_detail::add_button(
        ctx.tree,
        stack_id,
        "menu.account_edit.confirm.delete",
        screens_detail::tr(ctx, "ui.menu.account.confirm_delete_button"),
        "account.confirm_delete",
        true,
        false,
        UiControlRole::Danger);
    screens_detail::add_button(
        ctx.tree,
        stack_id,
        "menu.account_edit.confirm.cancel",
        screens_detail::tr(ctx, "ui.menu.common.cancel"),
        "account.cancel_delete");
  }

  std::string profile_id_{};
  std::string display_name_{};
  bool initialized_{};
  bool confirm_delete_{};
  SkinManagerState skin_manager_{};
  bool dragging_preview_{};
  std::optional<Vec2f> last_drag_cursor_{};
  f32 preview_yaw_{};
};

class AccountCreateScreen final : public UIScreen {
public:
  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::AccountCreate;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "account_create";
  }

  void build(UIContext& ctx) override {
    add_fullscreen_account_base(ctx, screen_id(), screens_detail::tr(ctx, "ui.menu.account.create_profile"));
    const UiNodeID stack = ctx.tree.find_by_stable_id("account_create.controls")->node_id;
    screens_detail::add_text_box(
        ctx.tree,
        stack,
        "menu.account_create.name",
        screens_detail::tr(ctx, "ui.menu.account.display_name_label"),
        display_name_,
        screens_detail::tr(ctx, "ui.menu.account.display_name_placeholder"),
        "account.focus_name",
        true);
    screens_detail::add_button(
        ctx.tree,
        stack,
        "menu.account_create.skin",
        screens_detail::tr(ctx, "ui.menu.account.manage_skins"),
        "account.manage_skins");
    const std::string status =
        ctx.data_source == nullptr ? std::string{} : ctx.data_source->local_profile_status();
    screens_detail::add_error_text(ctx.tree, stack, "menu.account_create.status", status);

    UiElement preview = screens_detail::make_panel(
        "menu.account_create.preview",
        360.0F,
        512.0F,
        {0.0F, 0.0F, 0.0F, 0.0F});
    preview.layout.anchor = UiAnchor::Center;
    preview.layout.offset = {210.0F, 0.0F};
    preview.hit_test = true;
    (void)ctx.tree.add_child(ctx.tree.root_id(), std::move(preview));

    add_fixed_action_button(
        ctx.tree,
        ctx.tree.root_id(),
        "menu.account_create.cancel",
        screens_detail::tr(ctx, "ui.menu.common.cancel"),
        "account.cancel",
        UiAnchor::BottomRight,
        {-182.0F, -28.0F},
        142.0F);
    add_fixed_action_button(
        ctx.tree,
        ctx.tree.root_id(),
        "menu.account_create.complete",
        screens_detail::tr(ctx, "ui.menu.common.complete"),
        "account.complete",
        UiAnchor::BottomRight,
        {-28.0F, -28.0F},
        142.0F,
        ButtonSelectedColor);

    if (skin_manager_.open) {
      build_skin_manager_dialog(
          ctx,
          skin_dialog_id(),
          current_skins(ctx),
          skin_manager_.scroll_y);
    }
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (skin_manager_.open) {
      return handle_skin_manager_action(
          action,
          ctx,
          skin_manager_,
          skin_dialog_id(),
          std::nullopt,
          current_skins(ctx));
    }
    if (action.action_id == "account.manage_skins") {
      open_skin_manager(skin_manager_, current_skins(ctx));
      return true;
    }
    if (action.action_id == "account.complete") {
      return complete(ctx);
    }
    if (action.action_id == "account.cancel") {
      return cancel(ctx);
    }
    return action.action_id == "account.focus_name";
  }

  bool handle_text_input(std::string_view text, UIContext&) override {
    if (skin_manager_.open) {
      return false;
    }
    display_name_.append(text.substr(0U, std::min<usize>(text.size(), 32U)));
    if (display_name_.size() > 32U) {
      display_name_.resize(32U);
    }
    return true;
  }

  bool handle_backspace(UIContext&) override {
    if (skin_manager_.open || display_name_.empty()) {
      return false;
    }
    screens_detail::erase_last_utf8_codepoint(display_name_);
    return true;
  }

  bool handle_submit(UIContext& ctx) override {
    if (skin_manager_.open) {
      close_skin_manager(skin_manager_);
      return true;
    }
    return complete(ctx);
  }

  bool handle_cancel(UIContext& ctx) override {
    if (skin_manager_.open) {
      return cancel_skin_manager(ctx, skin_manager_, std::nullopt);
    }
    return cancel(ctx);
  }

  bool handle_scroll(Vec2f cursor, f32 wheel_y, UIContext& ctx) override {
    return handle_skin_manager_scroll(
        cursor,
        wheel_y,
        ctx,
        skin_manager_,
        skin_dialog_id(),
        current_skins(ctx).size());
  }

  bool handle_pointer(
      Vec2f cursor,
      Vec2f delta,
      bool left_pressed,
      bool left_released,
      bool left_held,
      UIContext& ctx) override {
    if (skin_manager_.open) {
      (void)handle_skin_manager_pointer(
          cursor,
          left_released,
          left_held,
          ctx,
          skin_manager_,
          skin_dialog_id(),
          std::nullopt,
          current_skins(ctx));
      return true;
    }
    const UiElement* preview = ctx.tree.find_by_stable_id("menu.account_create.preview");
    const bool over_preview = preview != nullptr && contains(preview->rect, cursor);
    if (left_pressed && over_preview) {
      dragging_preview_ = true;
      last_drag_cursor_ = cursor;
    }
    if (left_released || !left_held) {
      dragging_preview_ = false;
      last_drag_cursor_.reset();
    }
    if (dragging_preview_ && left_held) {
      const f32 drag_delta =
          last_drag_cursor_.has_value() ? cursor.x - last_drag_cursor_->x : delta.x;
      last_drag_cursor_ = cursor;
      preview_yaw_ += drag_delta * 0.014F;
      return true;
    }
    return false;
  }

  [[nodiscard]] std::optional<PlayerSkinPreviewRequest> player_skin_preview(
      const UIContext& ctx) const override {
    if (skin_manager_.open) {
      return std::nullopt;
    }
    std::string skin_hash;
    if (ctx.data_source != nullptr) {
      skin_hash = ctx.data_source->pending_create_profile_skin_hash();
    }
    return PlayerSkinPreviewRequest{
        "menu.account_create.preview",
        std::move(skin_hash),
        preview_yaw_,
        create_skin_slim_model(ctx)};
  }

private:
  [[nodiscard]] static std::string_view skin_dialog_id() noexcept {
    return "menu.account_create.skin_dialog";
  }

  [[nodiscard]] std::vector<LocalProfileSkinEntry> current_skins(
      const UIContext& ctx) const {
    return create_skin_entries(ctx);
  }

  bool complete(UIContext& ctx) {
    const std::string name = screens_detail::trim_copy(display_name_);
    if (name.empty()) {
      display_name_ = "local_player";
    }
    screens_detail::submit_command(
        ctx,
        CreateLocalProfileCommand{
            screens_detail::trim_copy(display_name_).empty()
                ? std::string{"local_player"}
                : screens_detail::trim_copy(display_name_)});
    if (ctx.screens != nullptr) {
      ctx.screens->pop();
    }
    return true;
  }

  bool cancel(UIContext& ctx) {
    screens_detail::submit_command(ctx, DiscardLocalProfileCreateCommand{});
    if (ctx.screens != nullptr) {
      ctx.screens->pop();
    }
    return true;
  }

  std::string display_name_{"local_player"};
  SkinManagerState skin_manager_{};
  bool dragging_preview_{};
  std::optional<Vec2f> last_drag_cursor_{};
  f32 preview_yaw_{};
};

} // namespace

void build_account_manager_dropdown(UIContext& ctx, AccountManagerDropdownState& state) {
  const std::vector<LocalProfileEntry> profiles = profiles_from(ctx);
  const LocalProfileEntry* active = active_profile(profiles);
  const PlayerSkinTextureView default_texture = default_skin_from(ctx);
  state.scroll_y = std::clamp(state.scroll_y, 0.0F, account_max_scroll(profiles.size()));
  add_account_toggle_button(ctx, texture_or_default(active, ctx), state.open);
  if (state.open) {
    add_account_dropdown(ctx, profiles, default_texture, state.scroll_y);
  }
}

bool handle_account_manager_action(
    const UiAction& action,
    UIContext& ctx,
    AccountManagerDropdownState& state) {
  if (ctx.screens == nullptr) {
    return false;
  }
  if (action.action_id == "account.toggle") {
    state.open = !state.open;
    return true;
  }
  if (action.action_id == "account.create") {
    state.open = false;
    ctx.screens->push(make_account_create_screen());
    return true;
  }

  usize index = 0;
  if (!parse_account_profile_index(action.element_id, index)) {
    return false;
  }
  const std::vector<LocalProfileEntry> profiles = profiles_from(ctx);
  if (index >= profiles.size()) {
    return false;
  }
  if (action.action_id == "account.select") {
    screens_detail::submit_command(ctx, SelectLocalProfileCommand{profiles[index].profile_id});
    state.open = false;
    state.scroll_y = 0.0F;
    return true;
  }
  if (action.action_id == "account.edit") {
    state.open = false;
    ctx.screens->push(make_account_edit_screen(profiles[index].profile_id));
    return true;
  }
  return false;
}

bool handle_account_manager_scroll(
    Vec2f cursor,
    f32 wheel_y,
    UIContext& ctx,
    AccountManagerDropdownState& state) {
  if (!state.open || wheel_y == 0.0F) {
    return false;
  }
  const UiElement* viewport = ctx.tree.find_by_stable_id("menu.account.dropdown.viewport");
  if (viewport == nullptr || !contains(viewport->rect, cursor)) {
    return false;
  }
  const std::vector<LocalProfileEntry> profiles = profiles_from(ctx);
  const f32 max_scroll = account_max_scroll(profiles.size());
  if (max_scroll <= 0.0F) {
    state.scroll_y = 0.0F;
    return true;
  }
  state.scroll_y = std::clamp(state.scroll_y - wheel_y * 36.0F, 0.0F, max_scroll);
  return true;
}

std::unique_ptr<UIScreen> make_account_create_screen() {
  return std::make_unique<AccountCreateScreen>();
}

std::unique_ptr<UIScreen> make_account_edit_screen(std::string profile_id) {
  return std::make_unique<AccountEditScreen>(std::move(profile_id));
}

} // namespace arv::ui
