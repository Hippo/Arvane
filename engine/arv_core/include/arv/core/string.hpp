#pragma once

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

[[nodiscard]] inline std::string trim_ascii_copy(std::string_view text) {
  return std::string{trim_ascii_view(text)};
}

} // namespace arv
