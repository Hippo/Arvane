#include "arv/text.hpp"

#include <utility>

namespace arv {

TextComponent::TextComponent() noexcept = default;

TextComponent::TextComponent(const ArvAPI* api, ArvTextHandle handle) noexcept
    : api_(api), handle_(handle) {
}

TextComponent::TextComponent(TextComponent&& other) noexcept
    : api_(std::exchange(other.api_, nullptr)),
      handle_(std::exchange(other.handle_, {})) {
}

TextComponent& TextComponent::operator=(TextComponent&& other) noexcept {
  if (this != &other) {
    reset();
    api_ = std::exchange(other.api_, nullptr);
    handle_ = std::exchange(other.handle_, {});
  }
  return *this;
}

TextComponent::~TextComponent() {
  reset();
}

void TextComponent::reset() noexcept {
  if (handle_.value != 0U && api_ != nullptr && api_->text != nullptr &&
      api_->text->release != nullptr) {
    api_->text->release(api_->text_ctx, handle_);
  }
  api_ = nullptr;
  handle_ = {};
}

bool TextComponent::valid() const noexcept {
  return handle_.value != 0U;
}

TextComponent::operator bool() const noexcept {
  return valid();
}

ArvTextHandle TextComponent::abi() const noexcept {
  return handle_;
}

TextResult::operator bool() const noexcept {
  return static_cast<bool>(result);
}

Text::Text(const ArvAPI* api) noexcept : api_(api) {
}

Result Text::unsupported() const {
  if (api_ == nullptr || api_->text == nullptr ||
      api_->text->unsupported == nullptr) {
    return Result::unsupported("text API is unavailable");
  }
  return Result{api_->text->unsupported(api_->text_ctx)};
}

TextResult Text::literal(std::string_view text) const {
  if (api_ == nullptr || api_->text == nullptr || api_->text->literal == nullptr) {
    return {Result::unsupported("text API is unavailable"), {}};
  }
  ArvTextHandle handle{};
  Result result{api_->text->literal(api_->text_ctx, view(text), &handle)};
  return {result, TextComponent{api_, handle}};
}

TextResult Text::translatable(std::string_view translation_key) const {
  if (api_ == nullptr || api_->text == nullptr ||
      api_->text->translatable == nullptr) {
    return {Result::unsupported("text API is unavailable"), {}};
  }
  ArvTextHandle handle{};
  Result result{api_->text->translatable(
      api_->text_ctx,
      view(translation_key),
      &handle)};
  return {result, TextComponent{api_, handle}};
}

TextResult Text::append(
    const TextComponent& parent,
    const TextComponent& child) const {
  if (api_ == nullptr || api_->text == nullptr || api_->text->append == nullptr) {
    return {Result::unsupported("text API is unavailable"), {}};
  }
  ArvTextHandle handle{};
  Result result{api_->text->append(
      api_->text_ctx,
      parent.abi(),
      child.abi(),
      &handle)};
  return {result, TextComponent{api_, handle}};
}

TextResult Text::style(
    const TextComponent& text,
    TextStylePatch style) const {
  if (api_ == nullptr || api_->text == nullptr || api_->text->style == nullptr) {
    return {Result::unsupported("text API is unavailable"), {}};
  }
  ArvTextHandle handle{};
  Result result{api_->text->style(api_->text_ctx, text.abi(), style, &handle)};
  return {result, TextComponent{api_, handle}};
}

TextResult Text::color(
    const TextComponent& text,
    TextColor color) const {
  if (api_ == nullptr || api_->text == nullptr || api_->text->color == nullptr) {
    return {Result::unsupported("text API is unavailable"), {}};
  }
  ArvTextHandle handle{};
  Result result{api_->text->color(api_->text_ctx, text.abi(), color, &handle)};
  return {result, TextComponent{api_, handle}};
}

TextResult Text::hover_text(
    const TextComponent& text,
    const TextComponent& hover_text) const {
  if (api_ == nullptr || api_->text == nullptr ||
      api_->text->hover_text == nullptr) {
    return {Result::unsupported("text API is unavailable"), {}};
  }
  ArvTextHandle handle{};
  Result result{api_->text->hover_text(
      api_->text_ctx,
      text.abi(),
      hover_text.abi(),
      &handle)};
  return {result, TextComponent{api_, handle}};
}

TextResult Text::click(
    const TextComponent& text,
    TextClickAction action,
    std::string_view value) const {
  if (api_ == nullptr || api_->text == nullptr || api_->text->click == nullptr) {
    return {Result::unsupported("text API is unavailable"), {}};
  }
  ArvTextHandle handle{};
  Result result{api_->text->click(
      api_->text_ctx,
      text.abi(),
      static_cast<ArvTextClickAction>(action),
      view(value),
      &handle)};
  return {result, TextComponent{api_, handle}};
}

BufferResult Text::plain_text(const TextComponent& text) const {
  if (api_ == nullptr || api_->text == nullptr ||
      api_->text->plain_text == nullptr) {
    return {Result::unsupported("text API is unavailable"), {}};
  }
  ArvByteBuffer buffer{};
  Result result{api_->text->plain_text(api_->text_ctx, text.abi(), &buffer)};
  return {result, Buffer{api_, buffer}};
}

BufferResult Text::serialize(const TextComponent& text) const {
  if (api_ == nullptr || api_->text == nullptr ||
      api_->text->serialize == nullptr) {
    return {Result::unsupported("text API is unavailable"), {}};
  }
  ArvByteBuffer buffer{};
  Result result{api_->text->serialize(api_->text_ctx, text.abi(), &buffer)};
  return {result, Buffer{api_, buffer}};
}

Result Text::validate(
    const TextComponent& text,
    const TextLimits* limits) const {
  if (api_ == nullptr || api_->text == nullptr || api_->text->validate == nullptr) {
    return Result::unsupported("text API is unavailable");
  }
  return Result{api_->text->validate(api_->text_ctx, text.abi(), limits)};
}

} // namespace arv
