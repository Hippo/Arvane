#include "arv/ui/core/ui.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace arv::ui {
namespace {

[[nodiscard]] bool contains(UiRect rect, Vec2f point) noexcept {
  return point.x >= rect.x && point.y >= rect.y &&
         point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}

[[nodiscard]] UiRect inset(UiRect rect, UiPadding padding) noexcept {
  const f32 x = rect.x + padding.left;
  const f32 y = rect.y + padding.top;
  const f32 width = std::max(0.0F, rect.width - padding.left - padding.right);
  const f32 height = std::max(0.0F, rect.height - padding.top - padding.bottom);
  return {x, y, width, height};
}

[[nodiscard]] UiRect intersect(UiRect lhs, UiRect rhs) noexcept {
  const f32 x_min = std::max(lhs.x, rhs.x);
  const f32 y_min = std::max(lhs.y, rhs.y);
  const f32 x_max = std::min(lhs.x + lhs.width, rhs.x + rhs.width);
  const f32 y_max = std::min(lhs.y + lhs.height, rhs.y + rhs.height);
  return {
      x_min,
      y_min,
      std::max(0.0F, x_max - x_min),
      std::max(0.0F, y_max - y_min)};
}

[[nodiscard]] Vec2f apply_min(Vec2f size, UiLayout layout) noexcept {
  return {std::max(size.x, layout.min_width), std::max(size.y, layout.min_height)};
}

[[nodiscard]] std::string_view element_type_name(UiElementType type) noexcept {
  switch (type) {
    case UiElementType::Root:
      return "root";
    case UiElementType::Panel:
      return "panel";
    case UiElementType::Text:
      return "text";
    case UiElementType::Button:
      return "button";
    case UiElementType::VerticalStack:
      return "stack_vertical";
    case UiElementType::HorizontalStack:
      return "stack_horizontal";
    case UiElementType::Grid:
      return "grid";
    case UiElementType::Spacer:
      return "spacer";
    case UiElementType::ItemSlot:
      return "item_slot";
    case UiElementType::Image:
      return "image";
  }
  return "unknown";
}

[[nodiscard]] bool known_document_type(std::string_view type) noexcept {
  return type == "root" || type == "panel" || type == "stack_vertical" ||
         type == "stack_horizontal" || type == "grid" || type == "scroll_area" ||
         type == "overlay" || type == "text" || type == "image" ||
         type == "button" || type == "item_slot" || type == "spacer" ||
         type == "separator" || type == "tooltip" || type == "checkbox" ||
         type == "slider" || type == "text_input" || type == "progress_bar";
}

[[nodiscard]] bool may_have_children(std::string_view type) noexcept {
  return type == "root" || type == "panel" || type == "stack_vertical" ||
         type == "stack_horizontal" || type == "grid" || type == "scroll_area" ||
         type == "overlay" || type == "button" || type == "tooltip";
}

void validate_document_element(
    const UiDocumentElement& element,
    UiDocumentLimits limits,
    u32 depth,
    u32& count,
    u32& text_total,
    HashSet<std::string>& ids,
    UiDocumentValidation& validation) {
  ++count;
  if (count > limits.max_elements) {
    validation.errors.emplace_back("UI document exceeds max element count");
    return;
  }
  if (depth > limits.max_depth) {
    validation.errors.emplace_back("UI document exceeds max tree depth");
  }
  if (!known_document_type(element.type)) {
    validation.errors.emplace_back("unknown UI element type: " + element.type);
  }
  if (!element.id.empty() && !ids.insert(element.id).second) {
    validation.errors.emplace_back("duplicate UI element id: " + element.id);
  }
  text_total += static_cast<u32>(element.text.size());
  if (element.text.size() > limits.max_text_length || text_total > limits.max_text_length) {
    validation.errors.emplace_back("UI document exceeds text length limit");
  }
  if (element.children.size() > limits.max_children_per_element) {
    validation.errors.emplace_back("UI element exceeds max child count");
  }
  if (!element.children.empty() && !may_have_children(element.type)) {
    validation.errors.emplace_back("UI element type cannot have children: " + element.type);
  }
  for (const UiDocumentElement& child : element.children) {
    validate_document_element(child, limits, depth + 1U, count, text_total, ids, validation);
  }
}

void append_rect(render::FramePacket& frame, UiRect rect, Vec4f color, std::optional<UiRect> clip) {
  if (rect.width <= 0.0F || rect.height <= 0.0F || color.w <= 0.0F) {
    return;
  }
  render::UIRectDrawItem item(
      Vec2f{rect.x, rect.y},
      Vec2f{rect.x + rect.width, rect.y + rect.height},
      color);
  if (clip.has_value()) {
    item.clip = true;
    item.clip_min = {clip->x, clip->y};
    item.clip_max = {clip->x + clip->width, clip->y + clip->height};
  }
  frame.ui_rects.emplace_back(item);
}

void append_image(
    render::FramePacket& frame,
    UiRect rect,
    const UiImageData& image,
    std::optional<UiRect> clip) {
  if (!image.valid || rect.width <= 0.0F || rect.height <= 0.0F || image.tint.w <= 0.0F) {
    return;
  }

  render::UITexturedQuadDrawItem quad;
  if (image.custom_points) {
    for (usize index = 0; index < image.points.size(); ++index) {
      quad.positions[index] = {rect.x + image.points[index].x, rect.y + image.points[index].y};
    }
  } else {
    quad.positions = {{
        {rect.x, rect.y},
        {rect.x + rect.width, rect.y},
        {rect.x + rect.width, rect.y + rect.height},
        {rect.x, rect.y + rect.height},
    }};
  }
  quad.uvs = image.uvs;
  quad.color = image.tint;
  quad.source = image.source;
  quad.page = image.page;
  if (clip.has_value()) {
    quad.clip = true;
    quad.clip_min = {clip->x, clip->y};
    quad.clip_max = {clip->x + clip->width, clip->y + clip->height};
  }
  frame.ui_textured_quads.emplace_back(std::move(quad));
}

} // namespace

FontUiTextMeasurer::FontUiTextMeasurer(const asset::FontSystem& fonts, asset::FontStack stack) : fonts_(&fonts), stack_(std::move(stack)) {
}

Vec2f FontUiTextMeasurer::measure(std::string_view text) const {
  if (fonts_ == nullptr) {
    return {};
  }
  auto measured = fonts_->measure_text(text, stack_);
  if (!measured) {
    return {};
  }
  return {measured.value().width, measured.value().height};
}

f32 FontUiTextMeasurer::line_height() const {
  if (fonts_ == nullptr || stack_.faces.empty()) {
    return 0.0F;
  }
  return fonts_->metrics(stack_.faces.front()).line_height;
}

UiTree::UiTree() {
  clear();
}

void UiTree::clear() {
  elements_.clear();
  stable_ids_.clear();
  hovered_ = {};
  active_ = {};
  keyboard_focus_ = {};
  mouse_capture_ = {};
  next_id_ = UiNodeID{2};

  UiElement root;
  root.node_id = root_id_;
  root.stable_id = "root";
  root.type = UiElementType::Root;
  root.layout.width_mode = UiSizeMode::Fill;
  root.layout.height_mode = UiSizeMode::Fill;
  root.hit_test = false;
  elements_.emplace_back(std::move(root));
  stable_ids_["root"] = root_id_;
}

UiElement* UiTree::element(UiNodeID id) noexcept {
  if (id.value == 0 || id.value > elements_.size()) {
    return nullptr;
  }
  return &elements_[id.value - 1U];
}

const UiElement* UiTree::element(UiNodeID id) const noexcept {
  if (id.value == 0 || id.value > elements_.size()) {
    return nullptr;
  }
  return &elements_[id.value - 1U];
}

UiElement* UiTree::find_by_stable_id(std::string_view stable_id) noexcept {
  const auto found = stable_ids_.find(std::string{stable_id});
  return found == stable_ids_.end() ? nullptr : element(found->second);
}

const UiElement* UiTree::find_by_stable_id(std::string_view stable_id) const noexcept {
  const auto found = stable_ids_.find(std::string{stable_id});
  return found == stable_ids_.end() ? nullptr : element(found->second);
}

UiNodeID UiTree::add_child(UiNodeID parent, UiElement element_to_add) {
  UiElement* parent_element = element(parent);
  if (parent_element == nullptr) {
    return {};
  }
  element_to_add.node_id = allocate_node();
  const UiNodeID id = element_to_add.node_id;
  parent_element->children.emplace_back(id);
  if (!element_to_add.stable_id.empty()) {
    stable_ids_[element_to_add.stable_id] = id;
  }
  elements_.emplace_back(std::move(element_to_add));
  return id;
}

void UiTree::layout(Vec2f viewport, const IUiTextMeasurer* text_measurer) {
  (void)measure_element(root_id_, viewport, text_measurer);
  arrange_element(root_id_, {0.0F, 0.0F, viewport.x, viewport.y}, text_measurer);
}

std::optional<UiNodeID> UiTree::hit_test(Vec2f point) const {
  return hit_test_recursive(root_id_, point);
}

std::optional<UiAction> UiTree::route_mouse_move(Vec2f point) {
  hovered_ = hit_test(point).value_or(UiNodeID{});
  if (const UiElement* hover = element(hovered_); hover != nullptr && !hover->action.empty()) {
    return UiAction{hover->stable_id, hover->action, "hover"};
  }
  return std::nullopt;
}

std::optional<UiAction> UiTree::route_mouse_down(Vec2f point, UiMouseButton button) {
  const std::optional<UiNodeID> hit = hit_test(point);
  if (!hit.has_value()) {
    active_ = {};
    return std::nullopt;
  }

  active_ = hit.value();
  mouse_capture_ = hit.value();
  UiElement* target = element(hit.value());
  if (target != nullptr && target->focusable) {
    keyboard_focus_ = hit.value();
  }
  if (target != nullptr && !target->action.empty()) {
    return UiAction{
        target->stable_id,
        target->action,
        button == UiMouseButton::Right ? "right_click" : "click"};
  }
  return std::nullopt;
}

std::optional<UiAction> UiTree::route_mouse_up(Vec2f point, UiMouseButton button) {
  (void)button;
  const UiNodeID released = mouse_capture_.value != 0 ? mouse_capture_ : hit_test(point).value_or(UiNodeID{});
  active_ = {};
  mouse_capture_ = {};
  if (const UiElement* target = element(released); target != nullptr && !target->action.empty()) {
    return UiAction{target->stable_id, target->action, "release"};
  }
  return std::nullopt;
}

UiNodeID UiTree::allocate_node() {
  const UiNodeID id = next_id_;
  next_id_ = UiNodeID{next_id_.value + 1U};
  return id;
}

Vec2f UiTree::measure_element(
    UiNodeID id,
    Vec2f available,
    const IUiTextMeasurer* text_measurer) {
  UiElement* current = element(id);
  if (current == nullptr || !current->visible) {
    return {};
  }

  Vec2f measured{};
  if (current->type == UiElementType::Text) {
    measured = text_measurer == nullptr ? Vec2f{} : text_measurer->measure(current->text);
  } else if (current->type == UiElementType::Spacer || current->type == UiElementType::ItemSlot ||
             current->type == UiElementType::Image) {
    measured = {current->layout.width, current->layout.height};
    for (const UiNodeID child : current->children) {
      (void)measure_element(child, measured, text_measurer);
    }
  } else if (current->type == UiElementType::VerticalStack) {
    f32 width = 0.0F;
    f32 height = 0.0F;
    bool first = true;
    const UiRect content = inset({0.0F, 0.0F, available.x, available.y}, current->layout.padding);
    for (const UiNodeID child : current->children) {
      const Vec2f child_size = measure_element(child, {content.width, content.height}, text_measurer);
      width = std::max(width, child_size.x);
      height += child_size.y + (first ? 0.0F : current->layout.spacing);
      first = false;
    }
    measured = {
        width + current->layout.padding.left + current->layout.padding.right,
        height + current->layout.padding.top + current->layout.padding.bottom};
  } else if (current->type == UiElementType::HorizontalStack) {
    f32 width = 0.0F;
    f32 height = 0.0F;
    bool first = true;
    const UiRect content = inset({0.0F, 0.0F, available.x, available.y}, current->layout.padding);
    for (const UiNodeID child : current->children) {
      const Vec2f child_size = measure_element(child, {content.width, content.height}, text_measurer);
      width += child_size.x + (first ? 0.0F : current->layout.spacing);
      height = std::max(height, child_size.y);
      first = false;
    }
    measured = {
        width + current->layout.padding.left + current->layout.padding.right,
        height + current->layout.padding.top + current->layout.padding.bottom};
  } else if (current->type == UiElementType::Grid) {
    const u32 columns = std::max(1U, current->layout.grid_columns);
    f32 cell_width = 0.0F;
    f32 cell_height = 0.0F;
    u32 child_count = 0;
    const UiRect content = inset({0.0F, 0.0F, available.x, available.y}, current->layout.padding);
    for (const UiNodeID child : current->children) {
      const Vec2f child_size = measure_element(child, {content.width, content.height}, text_measurer);
      cell_width = std::max(cell_width, child_size.x);
      cell_height = std::max(cell_height, child_size.y);
      ++child_count;
    }
    const u32 rows = child_count == 0U ? 0U : ((child_count + columns - 1U) / columns);
    measured = {
        static_cast<f32>(std::min(columns, child_count)) * cell_width +
            static_cast<f32>(std::min(columns, child_count) > 0U ? std::min(columns, child_count) - 1U : 0U) *
                current->layout.spacing +
            current->layout.padding.left + current->layout.padding.right,
        static_cast<f32>(rows) * cell_height +
            static_cast<f32>(rows > 0U ? rows - 1U : 0U) * current->layout.spacing +
            current->layout.padding.top + current->layout.padding.bottom};
  } else {
    f32 width = 0.0F;
    f32 height = 0.0F;
    for (const UiNodeID child : current->children) {
      const Vec2f child_size = measure_element(child, available, text_measurer);
      width = std::max(width, child_size.x);
      height = std::max(height, child_size.y);
    }
    measured = {
        width + current->layout.padding.left + current->layout.padding.right,
        height + current->layout.padding.top + current->layout.padding.bottom};
  }

  if (current->layout.width_mode == UiSizeMode::Fixed) {
    measured.x = current->layout.width;
  } else if (current->layout.width_mode == UiSizeMode::Fill) {
    measured.x = available.x;
  }
  if (current->layout.height_mode == UiSizeMode::Fixed) {
    measured.y = current->layout.height;
  } else if (current->layout.height_mode == UiSizeMode::Fill) {
    measured.y = available.y;
  }
  current->rect.width = apply_min(measured, current->layout).x;
  current->rect.height = apply_min(measured, current->layout).y;
  return {current->rect.width, current->rect.height};
}

void UiTree::arrange_element(
    UiNodeID id,
    UiRect rect,
    const IUiTextMeasurer* text_measurer) {
  arrange_element(id, rect, text_measurer, std::nullopt);
}

void UiTree::arrange_element(
    UiNodeID id,
    UiRect rect,
    const IUiTextMeasurer* text_measurer,
    std::optional<UiRect> inherited_clip) {
  (void)text_measurer;
  UiElement* current = element(id);
  if (current == nullptr || !current->visible) {
    return;
  }
  current->rect = rect;
  current->clip_active = inherited_clip.has_value();
  current->clip = inherited_clip.has_value() ? intersect(rect, *inherited_clip) : rect;
  const std::optional<UiRect> child_clip =
      current->clip_children ? std::optional<UiRect>{current->clip} : inherited_clip;
  const UiRect content = inset(rect, current->layout.padding);

  if (current->type == UiElementType::Root || current->type == UiElementType::Panel ||
      current->type == UiElementType::Button || current->type == UiElementType::ItemSlot) {
    for (const UiNodeID child : current->children) {
      UiElement* child_element = element(child);
      if (child_element == nullptr) {
        continue;
      }
      UiRect child_rect{
          content.x + child_element->layout.offset.x,
          content.y + child_element->layout.offset.y,
          child_element->rect.width,
          child_element->rect.height};
      if (child_element->layout.anchor == UiAnchor::TopCenter) {
        child_rect.x = content.x + (content.width - child_rect.width) * 0.5F + child_element->layout.offset.x;
        child_rect.y = content.y + child_element->layout.offset.y;
      } else if (child_element->layout.anchor == UiAnchor::TopRight) {
        child_rect.x = content.x + content.width - child_rect.width + child_element->layout.offset.x;
        child_rect.y = content.y + child_element->layout.offset.y;
      } else if (child_element->layout.anchor == UiAnchor::BottomLeft) {
        child_rect.x = content.x + child_element->layout.offset.x;
        child_rect.y = content.y + content.height - child_rect.height + child_element->layout.offset.y;
      } else if (child_element->layout.anchor == UiAnchor::BottomCenter) {
        child_rect.x = content.x + (content.width - child_rect.width) * 0.5F + child_element->layout.offset.x;
        child_rect.y = content.y + content.height - child_rect.height + child_element->layout.offset.y;
      } else if (child_element->layout.anchor == UiAnchor::BottomRight) {
        child_rect.x = content.x + content.width - child_rect.width + child_element->layout.offset.x;
        child_rect.y = content.y + content.height - child_rect.height + child_element->layout.offset.y;
      } else if (child_element->layout.anchor == UiAnchor::Center) {
        child_rect.x = content.x + (content.width - child_rect.width) * 0.5F + child_element->layout.offset.x;
        child_rect.y = content.y + (content.height - child_rect.height) * 0.5F + child_element->layout.offset.y;
      }
      arrange_element(child, child_rect, text_measurer, child_clip);
    }
  } else if (current->type == UiElementType::VerticalStack) {
    f32 y = content.y;
    for (const UiNodeID child : current->children) {
      UiElement* child_element = element(child);
      if (child_element == nullptr) {
        continue;
      }
      arrange_element(child, {content.x, y, child_element->rect.width, child_element->rect.height}, text_measurer, child_clip);
      y += child_element->rect.height + current->layout.spacing;
    }
  } else if (current->type == UiElementType::HorizontalStack) {
    f32 x = content.x;
    for (const UiNodeID child : current->children) {
      UiElement* child_element = element(child);
      if (child_element == nullptr) {
        continue;
      }
      arrange_element(child, {x, content.y, child_element->rect.width, child_element->rect.height}, text_measurer, child_clip);
      x += child_element->rect.width + current->layout.spacing;
    }
  } else if (current->type == UiElementType::Grid) {
    const u32 columns = std::max(1U, current->layout.grid_columns);
    f32 x = content.x;
    f32 y = content.y;
    u32 column = 0;
    for (const UiNodeID child : current->children) {
      UiElement* child_element = element(child);
      if (child_element == nullptr) {
        continue;
      }
      arrange_element(child, {x, y, child_element->rect.width, child_element->rect.height}, text_measurer, child_clip);
      ++column;
      if (column >= columns) {
        column = 0;
        x = content.x;
        y += child_element->rect.height + current->layout.spacing;
      } else {
        x += child_element->rect.width + current->layout.spacing;
      }
    }
  }
}

std::optional<UiNodeID> UiTree::hit_test_recursive(UiNodeID id, Vec2f point) const {
  const UiElement* current = element(id);
  if (current == nullptr || !current->visible || !current->enabled) {
    return std::nullopt;
  }
  if (current->clip_active && !contains(current->clip, point)) {
    return std::nullopt;
  }
  if (current->type != UiElementType::Root && !contains(current->rect, point)) {
    return std::nullopt;
  }
  for (auto iter = current->children.rbegin(); iter != current->children.rend(); ++iter) {
    if (auto hit = hit_test_recursive(*iter, point); hit.has_value()) {
      return hit;
    }
  }
  if (current->hit_test && contains(current->rect, point)) {
    return id;
  }
  return std::nullopt;
}

UiDocumentValidation validate_ui_document(const UiDocument& document, UiDocumentLimits limits) {
  UiDocumentValidation validation;
  if (document.version != 1) {
    validation.errors.emplace_back("unsupported UI document version");
  }
  if (document.screen_id.empty()) {
    validation.errors.emplace_back("UI document screen_id is required");
  }
  if (document.owner.empty()) {
    validation.errors.emplace_back("UI document owner is required");
  }
  if (document.root.type != "root") {
    validation.errors.emplace_back("UI document root element must have type root");
  }
  u32 count = 0;
  u32 text_total = 0;
  HashSet<std::string> ids;
  validate_document_element(document.root, limits, 1, count, text_total, ids, validation);
  validation.accepted = validation.errors.empty();
  return validation;
}
void append_ui_draw_commands(
    const UiTree& tree,
    asset::FontSystem& fonts,
    const asset::FontStack& font_stack,
    render::FramePacket& frame) {
  for (const UiElement& element : tree.elements()) {
    if (!element.visible) {
      continue;
    }
    if (element.type == UiElementType::Panel || element.type == UiElementType::HorizontalStack ||
        element.type == UiElementType::VerticalStack || element.type == UiElementType::Button ||
        element.type == UiElementType::ItemSlot) {
      const std::optional<UiRect> clip =
          element.clip_active ? std::optional<UiRect>{element.clip} : std::nullopt;
      append_rect(frame, element.rect, element.style.background, clip);
      if (element.selected) {
        append_rect(frame, {element.rect.x, element.rect.y, element.rect.width, 3.0F}, element.style.accent, clip);
        append_rect(frame, {element.rect.x, element.rect.y + element.rect.height - 3.0F, element.rect.width, 3.0F}, element.style.accent, clip);
        append_rect(frame, {element.rect.x, element.rect.y, 3.0F, element.rect.height}, element.style.accent, clip);
        append_rect(frame, {element.rect.x + element.rect.width - 3.0F, element.rect.y, 3.0F, element.rect.height}, element.style.accent, clip);
      }
    }
    if (element.type == UiElementType::Image) {
      append_image(
          frame,
          element.rect,
          element.image,
          element.clip_active ? std::optional<UiRect>{element.clip} : std::nullopt);
    }
    if (element.type == UiElementType::Text && !element.text.empty()) {
      auto layout = fonts.layout_text(
          element.text,
          font_stack,
          {element.rect.x, element.rect.y},
          element.style.foreground);
      if (!layout) {
        continue;
      }
      for (const asset::GlyphQuad& quad : layout.value().quads) {
        render::UIGlyphDrawItem glyph(
            quad.min,
            quad.max,
            quad.uv_min,
            quad.uv_max,
            quad.color,
            quad.page);
        if (element.clip_active) {
          glyph.clip = true;
          glyph.clip_min = {element.clip.x, element.clip.y};
          glyph.clip_max = {element.clip.x + element.clip.width, element.clip.y + element.clip.height};
        }
        frame.ui_glyphs.emplace_back(glyph);
      }
    }
  }
}

} // namespace arv::ui
