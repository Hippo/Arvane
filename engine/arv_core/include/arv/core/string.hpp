#pragma once

#include "arv/core/types.hpp"

#include <string>
#include <string_view>

namespace arv {

[[nodiscard]] constexpr bool ascii_space(char ch) noexcept {
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
         ch == '\f' || ch == '\v';
}

[[nodiscard]] constexpr bool contains_ascii_space(std::string_view text) noexcept {
  for (const char ch : text) {
    if (ascii_space(ch)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] constexpr std::string_view trim_ascii_view(std::string_view text) noexcept {
  while (!text.empty() && ascii_space(text.front())) {
    text.remove_prefix(1U);
  }
  while (!text.empty() && ascii_space(text.back())) {
    text.remove_suffix(1U);
  }
  return text;
}

[[nodiscard]] constexpr char lower_ascii_char(char ch) noexcept {
  return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
}

[[nodiscard]] inline std::string trim_ascii_copy(std::string_view text) {
  return std::string{trim_ascii_view(text)};
}

[[nodiscard]] inline std::string lower_ascii_copy(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char ch : text) {
    out.push_back(lower_ascii_char(ch));
  }
  return out;
}

[[nodiscard]] constexpr bool equals_ignore_ascii_case(
    std::string_view lhs,
    std::string_view rhs) noexcept {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (usize index = 0; index < lhs.size(); ++index) {
    if (lower_ascii_char(lhs[index]) != lower_ascii_char(rhs[index])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] constexpr bool contains_ignore_ascii_case(
    std::string_view haystack,
    std::string_view needle) noexcept {
  if (needle.empty()) {
    return true;
  }
  if (needle.size() > haystack.size()) {
    return false;
  }
  for (usize start = 0; start + needle.size() <= haystack.size(); ++start) {
    bool matches = true;
    for (usize offset = 0; offset < needle.size(); ++offset) {
      if (lower_ascii_char(haystack[start + offset]) != lower_ascii_char(needle[offset])) {
        matches = false;
        break;
      }
    }
    if (matches) {
      return true;
    }
  }
  return false;
}

} // namespace arv
