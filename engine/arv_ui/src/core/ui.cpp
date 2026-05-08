#include "arv/ui/core/ui.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace arv::ui {
namespace {

constexpr usize MaxTextMeasureCacheEntries = 512;

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

[[nodiscard]] usize max_line_text_units(std::string_view text) noexcept {
  usize max_units = 0;
  usize line_units = 0;
  for (const unsigned char byte : text) {
    if (byte == '\n') {
      max_units = std::max(max_units, line_units);
      line_units = 0;
      continue;
    }
    if ((byte & 0xC0U) != 0x80U) {
      ++line_units;
    }
  }
  return std::max(max_units, line_units);
}

[[nodiscard]] f32 anchored_child_x(const UiRect& content, const UiElement& child) noexcept {
  switch (child.layout.anchor) {
    case UiAnchor::TopCenter:
    case UiAnchor::BottomCenter:
    case UiAnchor::Center:
      return content.x + (content.width - child.rect.width) * 0.5F + child.layout.offset.x;
    case UiAnchor::TopRight:
    case UiAnchor::BottomRight:
      return content.x + content.width - child.rect.width + child.layout.offset.x;
    case UiAnchor::TopLeft:
    case UiAnchor::BottomLeft:
      return content.x + child.layout.offset.x;
  }
  return content.x + child.layout.offset.x;
}

[[nodiscard]] f32 anchored_child_y(const UiRect& content, const UiElement& child) noexcept {
  switch (child.layout.anchor) {
    case UiAnchor::BottomLeft:
    case UiAnchor::BottomCenter:
    case UiAnchor::BottomRight:
      return content.y + content.height - child.rect.height + child.layout.offset.y;
    case UiAnchor::Center:
      return content.y + (content.height - child.rect.height) * 0.5F + child.layout.offset.y;
    case UiAnchor::TopLeft:
    case UiAnchor::TopCenter:
    case UiAnchor::TopRight:
      return content.y + child.layout.offset.y;
  }
  return content.y + child.layout.offset.y;
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

void append_rect(
    render::FramePacket& frame,
    UiRect rect,
    Vec4f color,
    Vec4f border,
    f32 border_width,
    f32 radius,
    std::optional<UiRect> clip) {
  if (rect.width <= 0.0F || rect.height <= 0.0F ||
      (color.w <= 0.0F && (border.w <= 0.0F || border_width <= 0.0F))) {
    return;
  }
  render::UIRectDrawItem item(
      Vec2f{rect.x, rect.y},
      Vec2f{rect.x + rect.width, rect.y + rect.height},
      color);
  item.border_color = border;
  item.border_width = border_width;
  item.radius = radius;
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

FontUiTextMeasurer::FontUiTextMeasurer(const asset::FontSystem& fonts, asset::FontStack stack)
    : fonts_(&fonts) {
  stacks_.small = stack;
  stacks_.body = stack;
  stacks_.heading = stack;
  stacks_.title = stack;
  stacks_.mono = std::move(stack);
}

FontUiTextMeasurer::FontUiTextMeasurer(const asset::FontSystem& fonts, UiFontStacks stacks)
    : fonts_(&fonts),
      stacks_(std::move(stacks)) {
}

const asset::FontStack& FontUiTextMeasurer::stack_for(UiTextRole role) const noexcept {
  switch (role) {
    case UiTextRole::Small:
      return stacks_.small.faces.empty() ? stacks_.body : stacks_.small;
    case UiTextRole::Body:
      return stacks_.body;
    case UiTextRole::Heading:
      return stacks_.heading.faces.empty() ? stacks_.body : stacks_.heading;
    case UiTextRole::Title:
      return stacks_.title.faces.empty() ? stacks_.body : stacks_.title;
    case UiTextRole::Mono:
      return stacks_.mono.faces.empty() ? stacks_.body : stacks_.mono;
  }
  return stacks_.body;
}

std::string FontUiTextMeasurer::measure_cache_key(std::string_view text, UiTextRole role) const {
  std::string key;
  key.reserve(text.size() + 2U);
  key.push_back(static_cast<char>(role));
  key.push_back('\0');
  key.append(text.data(), text.size());
  return key;
}

Vec2f FontUiTextMeasurer::measure(std::string_view text, UiTextRole role) const {
  if (fonts_ == nullptr) {
    return {};
  }
  std::string key = measure_cache_key(text, role);
  if (const auto found = measure_cache_.find(key); found != measure_cache_.end()) {
    return found->second;
  }
  auto measured = fonts_->measure_text(text, stack_for(role));
  if (!measured) {
    return {};
  }
  Vec2f size{measured.value().width, measured.value().height};
  if (measure_cache_.size() >= MaxTextMeasureCacheEntries) {
    measure_cache_.clear();
  }
  measure_cache_.emplace(std::move(key), size);
  return size;
}

f32 FontUiTextMeasurer::line_height(UiTextRole role) const {
  const asset::FontStack& stack = stack_for(role);
  if (fonts_ == nullptr || stack.faces.empty()) {
    return 0.0F;
  }
  return fonts_->metrics(stack.faces.front()).line_height;
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
    measured = text_measurer == nullptr ? Vec2f{} : text_measurer->measure(current->text, current->style.text_role);
    if (current->style.letter_spacing > 0.0F) {
      const usize text_units = max_line_text_units(current->text);
      if (text_units > 1U) {
        measured.x += current->style.letter_spacing * static_cast<f32>(text_units - 1U);
      }
    }
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
          anchored_child_x(content, *child_element),
          anchored_child_y(content, *child_element),
          child_element->rect.width,
          child_element->rect.height};
      arrange_element(child, child_rect, text_measurer, child_clip);
    }
  } else if (current->type == UiElementType::VerticalStack) {
    f32 y = content.y;
    for (const UiNodeID child : current->children) {
      UiElement* child_element = element(child);
      if (child_element == nullptr) {
        continue;
      }
      arrange_element(
          child,
          {
              anchored_child_x(content, *child_element),
              y + child_element->layout.offset.y,
              child_element->rect.width,
              child_element->rect.height},
          text_measurer,
          child_clip);
      y += child_element->rect.height + current->layout.spacing;
    }
  } else if (current->type == UiElementType::HorizontalStack) {
    f32 x = content.x;
    for (const UiNodeID child : current->children) {
      UiElement* child_element = element(child);
      if (child_element == nullptr) {
        continue;
      }
      arrange_element(
          child,
          {
              x + child_element->layout.offset.x,
              anchored_child_y(content, *child_element),
              child_element->rect.width,
              child_element->rect.height},
          text_measurer,
          child_clip);
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

[[nodiscard]] const asset::FontStack& font_stack_for_role(
    const UiFontStacks& stacks,
    UiTextRole role) noexcept {
  switch (role) {
    case UiTextRole::Small:
      return stacks.small.faces.empty() ? stacks.body : stacks.small;
    case UiTextRole::Body:
      return stacks.body;
    case UiTextRole::Heading:
      return stacks.heading.faces.empty() ? stacks.body : stacks.heading;
    case UiTextRole::Title:
      return stacks.title.faces.empty() ? stacks.body : stacks.title;
    case UiTextRole::Mono:
      return stacks.mono.faces.empty() ? stacks.body : stacks.mono;
  }
  return stacks.body;
}

[[nodiscard]] Vec4f interactive_background(const UiTree& tree, const UiElement& element) noexcept {
  Vec4f background = element.style.background;
  if (element.selected && element.style.background_selected.w > 0.0F) {
    background = element.style.background_selected;
  }
  if (tree.hovered().value == element.node_id.value && element.style.background_hover.w > 0.0F) {
    background = element.style.background_hover;
  }
  if (tree.active().value == element.node_id.value && element.style.background_pressed.w > 0.0F) {
    background = element.style.background_pressed;
  }
  return background;
}

[[nodiscard]] f32 snap_pixel(f32 value) noexcept {
  return static_cast<f32>(std::round(value));
}

void append_ui_draw_commands(
    const UiTree& tree,
    asset::FontSystem& fonts,
    const UiFontStacks& font_stacks,
    render::FramePacket& frame) {
  const usize element_count = tree.elements().size();
  frame.ui_rects.reserve(frame.ui_rects.size() + element_count);
  frame.ui_textured_quads.reserve(frame.ui_textured_quads.size() + element_count);
  frame.ui_glyphs.reserve(frame.ui_glyphs.size() + element_count * 8U);
  for (const UiElement& element : tree.elements()) {
    if (!element.visible) {
      continue;
    }
    if (element.type == UiElementType::Panel || element.type == UiElementType::HorizontalStack ||
        element.type == UiElementType::VerticalStack || element.type == UiElementType::Button ||
        element.type == UiElementType::ItemSlot) {
      const std::optional<UiRect> clip =
          element.clip_active ? std::optional<UiRect>{element.clip} : std::nullopt;
      Vec4f border = element.style.border;
      f32 border_width = element.style.border_width;
      if (element.selected) {
        border = element.style.accent;
        border_width = std::max(border_width, 2.0F);
      }
      if (tree.keyboard_focus().value == element.node_id.value &&
          element.style.border_focus.w > 0.0F) {
        border = element.style.border_focus;
        border_width = std::max(border_width, element.style.focus_border_width);
      }
      append_rect(
          frame,
          element.rect,
          interactive_background(tree, element),
          border,
          border_width,
          element.style.radius,
          clip);
    }
    if (element.type == UiElementType::Image) {
      append_image(
          frame,
          element.rect,
          element.image,
          element.clip_active ? std::optional<UiRect>{element.clip} : std::nullopt);
    }
    if (element.type == UiElementType::Text && !element.text.empty()) {
      const Vec2f text_origin{snap_pixel(element.rect.x), snap_pixel(element.rect.y)};
      auto layout = fonts.layout_text(
          element.text,
          font_stack_for_role(font_stacks, element.style.text_role),
          text_origin,
          element.style.foreground);
      if (!layout) {
        continue;
      }
      usize glyph_index_in_line = 0;
      f32 previous_glyph_x = 0.0F;
      for (const asset::GlyphQuad& quad : layout.value().quads) {
        if (glyph_index_in_line > 0U && quad.min.x < previous_glyph_x - 0.5F) {
          glyph_index_in_line = 0U;
        }
        const f32 tracking =
            element.style.letter_spacing * static_cast<f32>(glyph_index_in_line);
        const Vec2f glyph_min{snap_pixel(quad.min.x + tracking), snap_pixel(quad.min.y)};
        const Vec2f glyph_size{
            std::max(0.0F, snap_pixel(quad.max.x - quad.min.x)),
            std::max(0.0F, snap_pixel(quad.max.y - quad.min.y))};
        render::UIGlyphDrawItem glyph(
            glyph_min,
            {glyph_min.x + glyph_size.x, glyph_min.y + glyph_size.y},
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
        previous_glyph_x = quad.min.x;
        ++glyph_index_in_line;
      }
    }
  }
}

void append_ui_draw_commands(
    const UiTree& tree,
    asset::FontSystem& fonts,
    const asset::FontStack& font_stack,
    render::FramePacket& frame) {
  UiFontStacks stacks;
  stacks.small = font_stack;
  stacks.body = font_stack;
  stacks.heading = font_stack;
  stacks.title = font_stack;
  stacks.mono = font_stack;
  append_ui_draw_commands(tree, fonts, stacks, frame);
}

} // namespace arv::ui
