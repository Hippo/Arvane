#include "command_utils.hpp"

#include <sstream>

namespace arv_mason {

std::vector<std::string> copy_args(const std::vector<std::string_view>& args) {
  std::vector<std::string> out;
  out.reserve(args.size());
  for (std::string_view arg : args) {
    out.emplace_back(arg);
  }
  return out;
}

std::vector<std::string_view> view_args(const std::vector<std::string>& args) {
  std::vector<std::string_view> out;
  out.reserve(args.size());
  for (const std::string& arg : args) {
    out.emplace_back(arg);
  }
  return out;
}

std::string count_text(std::uint64_t count) {
  std::ostringstream out;
  out << count;
  return out.str();
}

} // namespace arv_mason
