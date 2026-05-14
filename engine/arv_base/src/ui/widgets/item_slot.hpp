#pragma once

#include "arv/base/ui/widgets/items.hpp"

#include "widget_common.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace arv::ui::widgets_detail {

[[nodiscard]] inline UiImageData with_custom_points(
    UiImageData image,
    std::array<Vec2f, 4> points,
    Vec4f tint) {
  image.custom_points = true;
  image.points = points;
  image.tint = tint;
  return image;
}

[[nodiscard]] inline UiImageData with_upright_uv_bounds(UiImageData image) {
  if (!image.valid) {
    return image;
  }

  Vec2f uv_min = image.uvs.front();
  Vec2f uv_max = image.uvs.front();
  for (const Vec2f uv : image.uvs) {
    uv_min.x = std::min(uv_min.x, uv.x);
    uv_min.y = std::min(uv_min.y, uv.y);
    uv_max.x = std::max(uv_max.x, uv.x);
    uv_max.y = std::max(uv_max.y, uv.y);
  }
  image.uvs = {{
      uv_min,
      {uv_max.x, uv_min.y},
      uv_max,
      {uv_min.x, uv_max.y},
  }};
  return image;
}

inline void add_texture_item_icon(
    UiTree& tree,
    UiNodeID slot_id,
    std::string_view stable_prefix,
    const UiImageData& texture_icon) {
  if (!texture_icon.valid) {
    return;
  }
  tree.add_child(
      slot_id,
      image_element(std::string{stable_prefix} + ".icon", texture_icon, 30.0F, 30.0F));
}

inline void add_block_item_icon(
    UiTree& tree,
    UiNodeID slot_id,
    std::string_view stable_prefix,
    const UiItemStackView& stack) {
  const UiImageData& top = stack.block_faces[voxel::face_index(voxel::BlockFace::PositiveY)];
  const UiImageData& right = stack.block_faces[voxel::face_index(voxel::BlockFace::PositiveX)];
  const UiImageData& front = stack.block_faces[voxel::face_index(voxel::BlockFace::PositiveZ)];
  if (!top.valid || !right.valid || !front.valid) {
    add_texture_item_icon(tree, slot_id, stable_prefix, stack.texture_icon);
    return;
  }

  constexpr f32 width = 34.0F;
  constexpr f32 height = 30.0F;
  UiElement left_face = image_element(
      std::string{stable_prefix} + ".block.front",
      with_custom_points(
          with_upright_uv_bounds(front),
          {{{3.0F, 9.0F}, {17.0F, 16.0F}, {17.0F, 30.0F}, {3.0F, 23.0F}}},
          {0.70F, 0.70F, 0.70F, 1.0F}),
      width,
      height);
  UiElement right_face = image_element(
      std::string{stable_prefix} + ".block.right",
      with_custom_points(
          with_upright_uv_bounds(right),
          {{{17.0F, 16.0F}, {31.0F, 9.0F}, {31.0F, 23.0F}, {17.0F, 30.0F}}},
          {0.82F, 0.82F, 0.82F, 1.0F}),
      width,
      height);
  UiElement top_face = image_element(
      std::string{stable_prefix} + ".block.top",
      with_custom_points(
          with_upright_uv_bounds(top),
          {{{17.0F, 2.0F}, {31.0F, 9.0F}, {17.0F, 16.0F}, {3.0F, 9.0F}}},
          {1.0F, 1.0F, 1.0F, 1.0F}),
      width,
      height);
  tree.add_child(slot_id, std::move(left_face));
  tree.add_child(slot_id, std::move(right_face));
  tree.add_child(slot_id, std::move(top_face));
}

inline void add_block_model_item_icon(
    UiTree& tree,
    UiNodeID slot_id,
    std::string_view stable_prefix,
    const UiItemStackView& stack) {
  if (stack.block_model_quads.empty()) {
    add_block_item_icon(tree, slot_id, stable_prefix, stack);
    return;
  }

  constexpr f32 width = 34.0F;
  constexpr f32 height = 30.0F;
  for (usize index = 0; index < stack.block_model_quads.size(); ++index) {
    tree.add_child(
        slot_id,
        image_element(
            std::string{stable_prefix} + ".block_model." + std::to_string(index),
            stack.block_model_quads[index].image,
            width,
            height));
  }
}

inline void add_item_slot_children(
    UiTree& tree,
    UiNodeID slot_id,
    std::string_view stable_prefix,
    u8 slot,
    const UiItemStackView* stack,
    bool show_number) {
  if (stack != nullptr && !stack->empty()) {
    if (stack->render_type == player::ItemRenderType::Block) {
      add_block_model_item_icon(tree, slot_id, stable_prefix, *stack);
    } else {
      add_texture_item_icon(tree, slot_id, stable_prefix, stack->texture_icon);
    }

    if (stack->count > 1U) {
      UiElement count = text_element(
          std::string{stable_prefix} + ".count",
          std::to_string(stack->count),
          tree.theme().colors.text,
          UiTextRole::Small);
      count.layout.anchor = UiAnchor::BottomRight;
      count.layout.offset = {-4.0F, -3.0F};
      tree.add_child(slot_id, std::move(count));
    }

    if (stack->durability > 0U && stack->max_durability > 0U) {
      const f32 fraction = std::clamp(
          static_cast<f32>(stack->durability) / static_cast<f32>(stack->max_durability),
          0.0F,
          1.0F);
      UiElement bar;
      bar.stable_id = std::string{stable_prefix} + ".durability";
      bar.type = UiElementType::Panel;
      bar.layout.width_mode = UiSizeMode::Fixed;
      bar.layout.height_mode = UiSizeMode::Fixed;
      bar.layout.width = 32.0F * fraction;
      bar.layout.height = 3.0F;
      bar.layout.anchor = UiAnchor::BottomLeft;
      bar.layout.offset = {5.0F, -5.0F};
      bar.style.background = tree.theme().colors.success;
      bar.hit_test = false;
      tree.add_child(slot_id, std::move(bar));
    }
  }

  if (show_number) {
    UiElement label = text_element(
        std::string{stable_prefix} + ".label",
        std::to_string(static_cast<u32>(slot + 1U)),
        tree.theme().colors.text_muted,
        UiTextRole::Small);
    label.layout.anchor = UiAnchor::BottomLeft;
    label.layout.offset = {4.0F, -4.0F};
    tree.add_child(slot_id, std::move(label));
  }
}

inline void append_slot(
    UiTree& tree,
    UiNodeID parent,
    std::string stable_id,
    u8 slot,
    const UiItemStackView* stack,
    bool selected,
    bool show_number,
    f32 size) {
  const UiTheme& theme = tree.theme();
  UiElement item;
  item.stable_id = stable_id;
  item.type = UiElementType::Slot;
  item.layout.width_mode = UiSizeMode::Fixed;
  item.layout.height_mode = UiSizeMode::Fixed;
  item.layout.width = size;
  item.layout.height = size;
  item.style.background = selected ? theme.colors.surface_selected : theme.colors.surface_alt;
  item.style.background.w = std::min(item.style.background.w, 0.90F);
  item.style.background_hover = theme.colors.surface_hover;
  item.style.background_pressed = theme.colors.surface_pressed;
  item.style.background_selected = theme.colors.surface_selected;
  item.style.accent = theme.colors.warning;
  item.style.border = theme.colors.border;
  item.style.border_focus = theme.colors.border_focus;
  item.style.border_width = theme.layout.border_width;
  item.style.focus_border_width = std::max(theme.layout.border_width * 2.0F, 2.0F);
  item.style.radius = theme.layout.radius_sm;
  item.selected = selected;
  item.hit_test = true;
  item.focusable = true;
  item.action = "inventory_slot";
  const UiNodeID slot_id = tree.add_child(parent, std::move(item));

  add_item_slot_children(tree, slot_id, stable_id, slot, stack, show_number);
}

} // namespace arv::ui::widgets_detail
