#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arv_mason {

[[nodiscard]] inline bool contains_string(
    const std::vector<std::string>& values,
    std::string_view value) {
  return std::any_of(values.begin(), values.end(), [value](const std::string& candidate) {
    return std::string_view{candidate.data(), candidate.size()} == value;
  });
}

inline bool append_unique_string(std::vector<std::string>& values, std::string value) {
  const std::string_view view{value.data(), value.size()};
  if (contains_string(values, view)) {
    return false;
  }
  values.push_back(std::move(value));
  return true;
}

} // namespace arv_mason
