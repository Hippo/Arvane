#pragma once

#include <filesystem>
#include <fstream>
#include <string_view>

namespace arv::testsupport {

inline void write_text(const std::filesystem::path& path, std::string_view text) {
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream output(path, std::ios::trunc);
  output << text;
}

inline void write_file(const std::filesystem::path& path, std::string_view text) {
  write_text(path, text);
}

} // namespace arv::testsupport
