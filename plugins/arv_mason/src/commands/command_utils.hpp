#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace arv_mason {

[[nodiscard]] std::vector<std::string> copy_args(const std::vector<std::string_view>& args);
[[nodiscard]] std::vector<std::string_view> view_args(const std::vector<std::string>& args);
[[nodiscard]] std::string count_text(std::uint64_t count);

} // namespace arv_mason
