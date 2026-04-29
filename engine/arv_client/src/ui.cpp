#include "arv/client/ui.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace arv::client {
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
    std::unordered_set<std::string>& ids,
    UiDocumentValidation& validation) {
  ++count;
  if (count > limits.max_elements) {
    validation.errors.push_back("UI document exceeds max element count");
    return;
  }
  if (depth > limits.max_depth) {
    validation.errors.push_back("UI document exceeds max tree depth");
  }
  if (!known_document_type(element.type)) {
    validation.errors.push_back("unknown UI element type: " + element.type);
  }
  if (!element.id.empty() && !ids.insert(element.id).second) {
    validation.errors.push_back("duplicate UI element id: " + element.id);
  }
  text_total += static_cast<u32>(element.text.size());
  if (element.text.size() > limits.max_text_length || text_total > limits.max_text_length) {
    validation.errors.push_back("UI document exceeds text length limit");
  }
  if (element.children.size() > limits.max_children_per_element) {
    validation.errors.push_back("UI element exceeds max child count");
  }
  if (!element.children.empty() && !may_have_children(element.type)) {
    validation.errors.push_back("UI element type cannot have children: " + element.type);
  }
  for (const UiDocumentElement& child : element.children) {
    validate_document_element(child, limits, depth + 1U, count, text_total, ids, validation);
  }
}

void append_rect(render::FramePacket& frame, UiRect rect, Vec4f color) {
  if (rect.width <= 0.0F || rect.height <= 0.0F || color.w <= 0.0F) {
    return;
  }
  frame.ui_rects.push_back({{rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height}, color});
}

void append_image(render::FramePacket& frame, UiRect rect, const UiImageData& image) {
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
  frame.ui_textured_quads.push_back(quad);
}

} // namespace

FontUiTextMeasurer::FontUiTextMeasurer(const asset::FontSystem& fonts, asset::FontStack stack)
    : fonts_(&fonts), stack_(std::move(stack)) {
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
  elements_.push_back(std::move(root));
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
  parent_element->children.push_back(id);
  if (!element_to_add.stable_id.empty()) {
    stable_ids_[element_to_add.stable_id] = id;
  }
  elements_.push_back(std::move(element_to_add));
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
  (void)text_measurer;
  UiElement* current = element(id);
  if (current == nullptr || !current->visible) {
    return;
  }
  current->rect = rect;
  current->clip = rect;
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
      arrange_element(child, child_rect, text_measurer);
    }
  } else if (current->type == UiElementType::VerticalStack) {
    f32 y = content.y;
    for (const UiNodeID child : current->children) {
      UiElement* child_element = element(child);
      if (child_element == nullptr) {
        continue;
      }
      arrange_element(child, {content.x, y, child_element->rect.width, child_element->rect.height}, text_measurer);
      y += child_element->rect.height + current->layout.spacing;
    }
  } else if (current->type == UiElementType::HorizontalStack) {
    f32 x = content.x;
    for (const UiNodeID child : current->children) {
      UiElement* child_element = element(child);
      if (child_element == nullptr) {
        continue;
      }
      arrange_element(child, {x, content.y, child_element->rect.width, child_element->rect.height}, text_measurer);
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
      arrange_element(child, {x, y, child_element->rect.width, child_element->rect.height}, text_measurer);
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
  if (current == nullptr || !current->visible || !current->enabled || !contains(current->clip, point)) {
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
    validation.errors.push_back("unsupported UI document version");
  }
  if (document.screen_id.empty()) {
    validation.errors.push_back("UI document screen_id is required");
  }
  if (document.owner.empty()) {
    validation.errors.push_back("UI document owner is required");
  }
  if (document.root.type != "root") {
    validation.errors.push_back("UI document root element must have type root");
  }
  u32 count = 0;
  u32 text_total = 0;
  std::unordered_set<std::string> ids;
  validate_document_element(document.root, limits, 1, count, text_total, ids, validation);
  validation.accepted = validation.errors.empty();
  return validation;
}

namespace {

[[nodiscard]] UiElement text_element(
    std::string stable_id,
    std::string text,
    Vec4f color = {0.92F, 0.94F, 0.96F, 1.0F}) {
  UiElement element;
  element.stable_id = std::move(stable_id);
  element.type = UiElementType::Text;
  element.text = std::move(text);
  element.style.foreground = color;
  element.hit_test = false;
  return element;
}

[[nodiscard]] UiElement fixed_panel(
    std::string stable_id,
    f32 width,
    f32 height,
    Vec4f background) {
  UiElement panel;
  panel.stable_id = std::move(stable_id);
  panel.type = UiElementType::Panel;
  panel.layout.width_mode = UiSizeMode::Fixed;
  panel.layout.height_mode = UiSizeMode::Fixed;
  panel.layout.width = width;
  panel.layout.height = height;
  panel.style.background = background;
  return panel;
}

[[nodiscard]] UiElement image_element(
    std::string stable_id,
    UiImageData image,
    f32 width,
    f32 height) {
  UiElement element;
  element.stable_id = std::move(stable_id);
  element.type = UiElementType::Image;
  element.layout.width_mode = UiSizeMode::Fixed;
  element.layout.height_mode = UiSizeMode::Fixed;
  element.layout.width = width;
  element.layout.height = height;
  element.layout.anchor = UiAnchor::Center;
  element.image = image;
  element.hit_test = false;
  return element;
}

[[nodiscard]] UiImageData with_custom_points(
    UiImageData image,
    std::array<Vec2f, 4> points,
    Vec4f tint) {
  image.custom_points = true;
  image.points = points;
  image.tint = tint;
  return image;
}

void add_texture_item_icon(
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

void add_block_item_icon(
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
          front,
          {{{3.0F, 9.0F}, {17.0F, 16.0F}, {17.0F, 30.0F}, {3.0F, 23.0F}}},
          {0.70F, 0.70F, 0.70F, 1.0F}),
      width,
      height);
  UiElement right_face = image_element(
      std::string{stable_prefix} + ".block.right",
      with_custom_points(
          right,
          {{{17.0F, 16.0F}, {31.0F, 9.0F}, {31.0F, 23.0F}, {17.0F, 30.0F}}},
          {0.82F, 0.82F, 0.82F, 1.0F}),
      width,
      height);
  UiElement top_face = image_element(
      std::string{stable_prefix} + ".block.top",
      with_custom_points(
          top,
          {{{17.0F, 2.0F}, {31.0F, 9.0F}, {17.0F, 16.0F}, {3.0F, 9.0F}}},
          {1.0F, 1.0F, 1.0F, 1.0F}),
      width,
      height);
  tree.add_child(slot_id, std::move(left_face));
  tree.add_child(slot_id, std::move(right_face));
  tree.add_child(slot_id, std::move(top_face));
}

void add_item_slot_children(
    UiTree& tree,
    UiNodeID slot_id,
    std::string_view stable_prefix,
    u8 slot,
    const UiItemStackView* stack,
    bool show_number) {
  if (stack != nullptr && !stack->empty()) {
    if (stack->render_type == player::ItemRenderType::Block) {
      add_block_item_icon(tree, slot_id, stable_prefix, *stack);
    } else {
      add_texture_item_icon(tree, slot_id, stable_prefix, stack->texture_icon);
    }

    if (stack->count > 1U) {
      UiElement count = text_element(
          std::string{stable_prefix} + ".count",
          std::to_string(stack->count),
          {0.96F, 0.96F, 0.91F, 1.0F});
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
      bar.style.background = {0.24F, 0.74F, 0.42F, 0.95F};
      bar.hit_test = false;
      tree.add_child(slot_id, std::move(bar));
    }
  }

  if (show_number) {
    UiElement label = text_element(
        std::string{stable_prefix} + ".label",
        std::to_string(static_cast<u32>(slot + 1U)),
        {0.86F, 0.88F, 0.90F, 0.92F});
    label.layout.anchor = UiAnchor::BottomLeft;
    label.layout.offset = {4.0F, -4.0F};
    tree.add_child(slot_id, std::move(label));
  }
}

void append_slot(
    UiTree& tree,
    UiNodeID parent,
    std::string stable_id,
    u8 slot,
    const UiItemStackView* stack,
    bool selected,
    bool show_number,
    f32 size) {
  UiElement item;
  item.stable_id = stable_id;
  item.type = UiElementType::ItemSlot;
  item.layout.width_mode = UiSizeMode::Fixed;
  item.layout.height_mode = UiSizeMode::Fixed;
  item.layout.width = size;
  item.layout.height = size;
  item.style.background = selected ? Vec4f{0.18F, 0.17F, 0.12F, 0.94F}
                                   : Vec4f{0.12F, 0.12F, 0.13F, 0.88F};
  item.style.accent = {0.95F, 0.86F, 0.44F, 1.0F};
  item.selected = selected;
  item.hit_test = true;
  item.focusable = true;
  item.action = "select_hotbar_slot";
  const UiNodeID slot_id = tree.add_child(parent, std::move(item));

  add_item_slot_children(tree, slot_id, stable_id, slot, stack, show_number);
}

void append_hotbar_hud(UiTree& tree, HotbarHudModel model) {
  const u8 slot_count = static_cast<u8>(
      std::max<u32>(1U, std::min<u32>(model.slot_count, player::DefaultHotbarSlotCount)));
  const bool selected_slot_is_valid = model.selected_slot < slot_count;

  UiElement hotbar;
  hotbar.stable_id = "hud.hotbar";
  hotbar.type = UiElementType::HorizontalStack;
  hotbar.layout.width_mode = UiSizeMode::Content;
  hotbar.layout.height_mode = UiSizeMode::Content;
  hotbar.layout.spacing = 4.0F;
  hotbar.layout.padding = {6.0F, 6.0F, 6.0F, 6.0F};
  hotbar.layout.anchor = UiAnchor::BottomCenter;
  hotbar.layout.offset = {0.0F, -24.0F};
  hotbar.style.background = {0.03F, 0.035F, 0.04F, 0.78F};
  hotbar.hit_test = false;
  const UiNodeID hotbar_id = tree.add_child(tree.root_id(), std::move(hotbar));

  for (u8 slot = 0; slot < slot_count; ++slot) {
    const UiItemStackView* stack =
        slot < model.slots.size() ? &model.slots[slot] : nullptr;
    append_slot(
        tree,
        hotbar_id,
        "hud.hotbar.slot." + std::to_string(slot),
        slot,
        stack,
        selected_slot_is_valid && slot == model.selected_slot,
        false,
        42.0F);
  }

  (void)model.viewport;
}

void append_inventory_ui(UiTree& tree, InventoryUiModel model) {
  if (!model.visible) {
    return;
  }

  UiElement panel = fixed_panel(
      "inventory.panel",
      426.0F,
      252.0F,
      {0.035F, 0.04F, 0.047F, 0.94F});
  panel.layout.anchor = UiAnchor::Center;
  panel.layout.padding = {14.0F, 14.0F, 12.0F, 12.0F};
  panel.hit_test = true;
  const UiNodeID panel_id = tree.add_child(tree.root_id(), std::move(panel));

  UiElement stack;
  stack.stable_id = "inventory.stack";
  stack.type = UiElementType::VerticalStack;
  stack.layout.spacing = 10.0F;
  stack.hit_test = false;
  const UiNodeID stack_id = tree.add_child(panel_id, std::move(stack));

  tree.add_child(
      stack_id,
      text_element("inventory.title", model.title, {0.94F, 0.95F, 0.96F, 1.0F}));

  UiElement grid;
  grid.stable_id = "inventory.grid";
  grid.type = UiElementType::Grid;
  grid.layout.grid_columns = 9;
  grid.layout.spacing = 4.0F;
  grid.hit_test = false;
  const UiNodeID grid_id = tree.add_child(stack_id, std::move(grid));

  const usize slot_total = std::max<usize>(model.slots.size(), player::DefaultPlayerInventorySlotCount);
  for (usize slot_index = 0; slot_index < slot_total; ++slot_index) {
    const UiItemStackView* stack_view =
        slot_index < model.slots.size() ? &model.slots[slot_index] : nullptr;
    const bool in_hotbar = slot_index < model.hotbar_slot_count;
    append_slot(
        tree,
        grid_id,
        "inventory.slot." + std::to_string(slot_index),
        static_cast<u8>(slot_index),
        stack_view,
        in_hotbar && slot_index == model.selected_hotbar_slot,
        false,
        40.0F);
  }

  (void)model.viewport;
}

void append_chat_ui(UiTree& tree, ChatUiModel model) {
  if (!model.visible && !model.input_active) {
    return;
  }

  UiElement stack;
  stack.stable_id = "chat.stack";
  stack.type = UiElementType::VerticalStack;
  stack.layout.width_mode = UiSizeMode::Fixed;
  stack.layout.width = 420.0F;
  stack.layout.spacing = 4.0F;
  stack.layout.padding = {8.0F, 8.0F, 8.0F, 8.0F};
  stack.layout.anchor = UiAnchor::BottomLeft;
  stack.layout.offset = {20.0F, -96.0F};
  stack.style.background = model.input_active ? Vec4f{0.02F, 0.024F, 0.03F, 0.52F}
                                              : Vec4f{0.0F, 0.0F, 0.0F, 0.0F};
  stack.hit_test = model.input_active;
  const UiNodeID stack_id = tree.add_child(tree.root_id(), std::move(stack));

  const usize start = model.messages.size() > 8U ? model.messages.size() - 8U : 0U;
  for (usize index = start; index < model.messages.size(); ++index) {
    const ChatLineView& line = model.messages[index];
    tree.add_child(
        stack_id,
        text_element(
            "chat.line." + std::to_string(index),
            line.text,
            {0.92F, 0.94F, 0.96F, std::clamp(line.opacity, 0.0F, 1.0F)}));
  }

  if (model.input_active) {
    UiElement input = fixed_panel(
        "chat.input",
        404.0F,
        30.0F,
        {0.04F, 0.045F, 0.052F, 0.92F});
    input.layout.padding = {8.0F, 8.0F, 6.0F, 6.0F};
    input.focusable = true;
    input.action = "chat_input";
    const UiNodeID input_id = tree.add_child(stack_id, std::move(input));
    tree.add_child(
        input_id,
        text_element("chat.input.text", "> " + model.draft, {0.96F, 0.97F, 0.98F, 1.0F}));
  }
}

void append_tab_menu_ui(UiTree& tree, TabMenuUiModel model) {
  if (!model.visible) {
    return;
  }

  const f32 rows = static_cast<f32>(std::max<usize>(1U, model.players.size()));
  UiElement panel = fixed_panel(
      "tab.panel",
      360.0F,
      60.0F + rows * 24.0F + (model.footer.empty() ? 0.0F : 24.0F),
      {0.03F, 0.034F, 0.04F, 0.88F});
  panel.layout.anchor = UiAnchor::TopCenter;
  panel.layout.offset = {0.0F, 24.0F};
  panel.layout.padding = {12.0F, 12.0F, 10.0F, 10.0F};
  panel.hit_test = false;
  const UiNodeID panel_id = tree.add_child(tree.root_id(), std::move(panel));

  UiElement stack;
  stack.stable_id = "tab.stack";
  stack.type = UiElementType::VerticalStack;
  stack.layout.spacing = 6.0F;
  stack.hit_test = false;
  const UiNodeID stack_id = tree.add_child(panel_id, std::move(stack));

  if (!model.header.empty()) {
    tree.add_child(stack_id, text_element("tab.header", model.header));
  }
  for (usize index = 0; index < model.players.size(); ++index) {
    const PlayerListEntryView& player = model.players[index];
    std::string text = player.name;
    if (player.latency_ms.has_value()) {
      text += "  " + std::to_string(player.latency_ms.value()) + "ms";
    }
    tree.add_child(
        stack_id,
        text_element("tab.player." + std::to_string(index), std::move(text)));
  }
  if (!model.footer.empty()) {
    tree.add_child(stack_id, text_element("tab.footer", model.footer, {0.78F, 0.82F, 0.86F, 1.0F}));
  }
}

[[nodiscard]] std::string interpolate_scoreboard_line(
    std::string_view line,
    const ScoreboardVariableMap* variables) {
  std::string output;
  output.reserve(line.size());
  for (usize index = 0; index < line.size();) {
    if (line[index] != '{') {
      output.push_back(line[index++]);
      continue;
    }

    const usize close = line.find('}', index + 1U);
    if (close == std::string_view::npos) {
      output.push_back(line[index++]);
      continue;
    }

    const std::string key{line.substr(index + 1U, close - index - 1U)};
    if (variables != nullptr) {
      const auto found = variables->find(key);
      if (found != variables->end()) {
        output += found->second;
      } else {
        output += "{" + key + "}";
      }
    } else {
      output += "{" + key + "}";
    }
    index = close + 1U;
  }
  return output;
}

void append_scoreboard_ui(UiTree& tree, ScoreboardUiModel model) {
  if (!model.visible || (model.lines.empty() && model.title.empty())) {
    return;
  }

  const f32 rows = static_cast<f32>(model.lines.size() + (model.title.empty() ? 0U : 1U));
  UiElement panel = fixed_panel(
      "scoreboard.panel",
      260.0F,
      22.0F + rows * 22.0F,
      {0.035F, 0.04F, 0.045F, 0.76F});
  panel.layout.anchor = UiAnchor::TopRight;
  panel.layout.offset = {-20.0F, 72.0F};
  panel.layout.padding = {10.0F, 10.0F, 8.0F, 8.0F};
  panel.hit_test = false;
  const UiNodeID panel_id = tree.add_child(tree.root_id(), std::move(panel));

  UiElement stack;
  stack.stable_id = "scoreboard.stack";
  stack.type = UiElementType::VerticalStack;
  stack.layout.spacing = 4.0F;
  stack.hit_test = false;
  const UiNodeID stack_id = tree.add_child(panel_id, std::move(stack));

  if (!model.title.empty()) {
    tree.add_child(stack_id, text_element("scoreboard.title", model.title));
  }
  for (usize index = 0; index < model.lines.size(); ++index) {
    tree.add_child(
        stack_id,
        text_element(
            "scoreboard.line." + std::to_string(index),
            interpolate_scoreboard_line(model.lines[index], model.variables),
            {0.86F, 0.89F, 0.92F, 1.0F}));
  }
}

void append_debug_hud(UiTree& tree, DebugHudModel model) {
  if (!model.show_fps) {
    return;
  }

  UiElement fps = text_element(
      "hud.debug.fps",
      "FPS: " + std::to_string(model.fps),
      {0.86F, 0.94F, 0.78F, 1.0F});
  fps.layout.anchor = UiAnchor::TopLeft;
  fps.layout.offset = {8.0F, 8.0F};
  tree.add_child(tree.root_id(), std::move(fps));
}

} // namespace

void build_hotbar_hud(UiTree& tree, HotbarHudModel model) {
  tree.clear();
  append_hotbar_hud(tree, model);
}

void build_inventory_ui(UiTree& tree, InventoryUiModel model) {
  tree.clear();
  append_inventory_ui(tree, model);
}

void build_chat_ui(UiTree& tree, ChatUiModel model) {
  tree.clear();
  append_chat_ui(tree, model);
}

void build_tab_menu_ui(UiTree& tree, TabMenuUiModel model) {
  tree.clear();
  append_tab_menu_ui(tree, model);
}

void build_scoreboard_ui(UiTree& tree, ScoreboardUiModel model) {
  tree.clear();
  append_scoreboard_ui(tree, model);
}

void build_gameplay_ui(UiTree& tree, GameplayUiModel model) {
  tree.clear();
  append_hotbar_hud(tree, model.hotbar);
  append_debug_hud(tree, model.debug);
  append_scoreboard_ui(tree, model.scoreboard);
  append_chat_ui(tree, model.chat);
  append_tab_menu_ui(tree, model.tab_menu);
  append_inventory_ui(tree, model.inventory);
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
      append_rect(frame, element.rect, element.style.background);
      if (element.selected) {
        append_rect(frame, {element.rect.x, element.rect.y, element.rect.width, 3.0F}, element.style.accent);
        append_rect(frame, {element.rect.x, element.rect.y + element.rect.height - 3.0F, element.rect.width, 3.0F}, element.style.accent);
        append_rect(frame, {element.rect.x, element.rect.y, 3.0F, element.rect.height}, element.style.accent);
        append_rect(frame, {element.rect.x + element.rect.width - 3.0F, element.rect.y, 3.0F, element.rect.height}, element.style.accent);
      }
    }
    if (element.type == UiElementType::Image) {
      append_image(frame, element.rect, element.image);
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
        frame.ui_glyphs.push_back({
            quad.min,
            quad.max,
            quad.uv_min,
            quad.uv_max,
            quad.color,
            quad.page});
      }
    }
  }
}

} // namespace arv::client
