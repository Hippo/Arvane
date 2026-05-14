#pragma once

#include "arv/api.h"
#include "arv/buffer.hpp"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <string_view>
#include <utility>

namespace arv {

enum class TextClickAction {
  OpenUrl = ARV_TEXT_CLICK_OPEN_URL,
  SuggestCommand = ARV_TEXT_CLICK_SUGGEST_COMMAND,
  RunCommand = ARV_TEXT_CLICK_RUN_COMMAND,
  CopyToClipboard = ARV_TEXT_CLICK_COPY_TO_CLIPBOARD,
};

using TextColor = ArvTextColor;
using TextStylePatch = ArvTextStylePatch;
using TextLimits = ArvTextLimits;

class TextComponent {
public:
  TextComponent() noexcept;
  TextComponent(const ArvAPI* api, ArvTextHandle handle) noexcept;

  TextComponent(const TextComponent&) = delete;
  TextComponent& operator=(const TextComponent&) = delete;

  TextComponent(TextComponent&& other) noexcept;

  TextComponent& operator=(TextComponent&& other) noexcept;

  ~TextComponent();

  void reset() noexcept;

  [[nodiscard]] bool valid() const noexcept;

  [[nodiscard]] explicit operator bool() const noexcept;

  [[nodiscard]] ArvTextHandle abi() const noexcept;

private:
  const ArvAPI* api_{};
  ArvTextHandle handle_{};
};

struct TextResult {
  Result result{};
  TextComponent text{};

  [[nodiscard]] explicit operator bool() const noexcept;
};

class Text {
public:
  explicit Text(const ArvAPI* api) noexcept;

  [[nodiscard]] Result unsupported() const;

  [[nodiscard]] TextResult literal(std::string_view text) const;

  [[nodiscard]] TextResult translatable(std::string_view translation_key) const;

  [[nodiscard]] TextResult append(
      const TextComponent& parent,
      const TextComponent& child) const;

  [[nodiscard]] TextResult style(
      const TextComponent& text,
      TextStylePatch style) const;

  [[nodiscard]] TextResult color(
      const TextComponent& text,
      TextColor color) const;

  [[nodiscard]] TextResult hover_text(
      const TextComponent& text,
      const TextComponent& hover_text) const;

  [[nodiscard]] TextResult click(
      const TextComponent& text,
      TextClickAction action,
      std::string_view value) const;

  [[nodiscard]] BufferResult plain_text(const TextComponent& text) const;

  [[nodiscard]] BufferResult serialize(const TextComponent& text) const;

  [[nodiscard]] Result validate(
      const TextComponent& text,
      const TextLimits* limits = nullptr) const;

private:
  const ArvAPI* api_{};
};

} // namespace arv
