#pragma once

#include "arv/core/types.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace arv::core {

enum class TextEditCommand : u8 {
  MoveLeft,
  MoveRight,
  MoveUp,
  MoveDown,
  DeleteForward,
  Copy,
  Paste,
  Cut,
  SelectAll,
};

struct TextEditInput {
  TextEditCommand command{TextEditCommand::MoveLeft};
  bool extend_selection{};
  std::string clipboard_text{};
};

struct TextEditResult {
  bool handled{};
  bool text_changed{};
  std::optional<std::string> clipboard_text{};
};

[[nodiscard]] inline usize clamp_utf8_boundary(std::string_view text, usize offset) noexcept {
  offset = std::min(offset, text.size());
  while (offset > 0U && offset < text.size() &&
         (static_cast<unsigned char>(text[offset]) & 0xC0U) == 0x80U) {
    --offset;
  }
  return offset;
}

[[nodiscard]] inline usize previous_utf8_codepoint(
    std::string_view text,
    usize cursor) noexcept {
  cursor = clamp_utf8_boundary(text, cursor);
  if (cursor == 0U) {
    return 0U;
  }
  --cursor;
  while (cursor > 0U &&
         (static_cast<unsigned char>(text[cursor]) & 0xC0U) == 0x80U) {
    --cursor;
  }
  return cursor;
}

[[nodiscard]] inline usize next_utf8_codepoint(
    std::string_view text,
    usize cursor) noexcept {
  cursor = clamp_utf8_boundary(text, cursor);
  if (cursor >= text.size()) {
    return text.size();
  }
  ++cursor;
  while (cursor < text.size() &&
         (static_cast<unsigned char>(text[cursor]) & 0xC0U) == 0x80U) {
    ++cursor;
  }
  return cursor;
}

struct TextEditBuffer {
  std::string value{};
  usize cursor{};
  usize selection_anchor{};

  TextEditBuffer() = default;

  explicit TextEditBuffer(std::string text) : value(std::move(text)) {
    move_cursor_to_end();
  }

  void clamp() noexcept {
    cursor = clamp_utf8_boundary(value, cursor);
    selection_anchor = clamp_utf8_boundary(value, selection_anchor);
  }

  void move_cursor_to_end() noexcept {
    cursor = value.size();
    selection_anchor = cursor;
  }

  [[nodiscard]] bool has_selection() const noexcept {
    return cursor != selection_anchor;
  }

  [[nodiscard]] usize selection_start() const noexcept {
    return std::min(cursor, selection_anchor);
  }

  [[nodiscard]] usize selection_end() const noexcept {
    return std::max(cursor, selection_anchor);
  }

  [[nodiscard]] std::string selected_text() const {
    const usize start = selection_start();
    return std::string{value.substr(start, selection_end() - start)};
  }

  void clear_selection() noexcept {
    selection_anchor = cursor;
  }

  bool delete_selection() {
    clamp();
    if (!has_selection()) {
      return false;
    }
    const usize start = selection_start();
    value.erase(start, selection_end() - start);
    cursor = start;
    selection_anchor = start;
    return true;
  }

  bool insert(std::string_view text, std::optional<usize> max_size = std::nullopt) {
    const bool removed_selection = delete_selection();
    usize count = text.size();
    if (max_size.has_value()) {
      if (value.size() >= max_size.value()) {
        count = 0U;
      } else {
        count = std::min(count, max_size.value() - value.size());
      }
    }
    count = clamp_utf8_boundary(text, count);
    if (count == 0U) {
      return removed_selection;
    }
    value.insert(cursor, text.substr(0U, count));
    cursor += count;
    selection_anchor = cursor;
    return true;
  }

  bool backspace() {
    if (delete_selection()) {
      return true;
    }
    clamp();
    if (cursor == 0U) {
      return false;
    }
    const usize previous = previous_utf8_codepoint(value, cursor);
    value.erase(previous, cursor - previous);
    cursor = previous;
    selection_anchor = previous;
    return true;
  }

  bool erase_forward() {
    if (delete_selection()) {
      return true;
    }
    clamp();
    if (cursor >= value.size()) {
      return false;
    }
    const usize next = next_utf8_codepoint(value, cursor);
    value.erase(cursor, next - cursor);
    selection_anchor = cursor;
    return true;
  }

  void move_left(bool extend_selection) {
    clamp();
    if (!extend_selection && has_selection()) {
      cursor = selection_start();
      selection_anchor = cursor;
      return;
    }
    if (!extend_selection) {
      selection_anchor = cursor;
    }
    cursor = previous_utf8_codepoint(value, cursor);
    if (!extend_selection) {
      selection_anchor = cursor;
    }
  }

  void move_right(bool extend_selection) {
    clamp();
    if (!extend_selection && has_selection()) {
      cursor = selection_end();
      selection_anchor = cursor;
      return;
    }
    if (!extend_selection) {
      selection_anchor = cursor;
    }
    cursor = next_utf8_codepoint(value, cursor);
    if (!extend_selection) {
      selection_anchor = cursor;
    }
  }

  void select_all() noexcept {
    selection_anchor = 0U;
    cursor = value.size();
  }

  TextEditResult handle_edit(
      const TextEditInput& input,
      std::optional<usize> max_size = std::nullopt) {
    switch (input.command) {
      case TextEditCommand::MoveLeft:
        move_left(input.extend_selection);
        return {.handled = true};
      case TextEditCommand::MoveRight:
        move_right(input.extend_selection);
        return {.handled = true};
      case TextEditCommand::MoveUp:
      case TextEditCommand::MoveDown:
        return {};
      case TextEditCommand::DeleteForward:
        return {.handled = true, .text_changed = erase_forward()};
      case TextEditCommand::Copy:
        if (has_selection()) {
          return {.handled = true, .clipboard_text = selected_text()};
        }
        return {.handled = true};
      case TextEditCommand::Paste:
        return {.handled = true, .text_changed = insert(input.clipboard_text, max_size)};
      case TextEditCommand::Cut:
        if (has_selection()) {
          std::string copied = selected_text();
          const bool changed = delete_selection();
          return {
              .handled = true,
              .text_changed = changed,
              .clipboard_text = std::move(copied)};
        }
        return {.handled = true};
      case TextEditCommand::SelectAll:
        select_all();
        return {.handled = true};
    }
    return {};
  }
};

} // namespace arv::core
