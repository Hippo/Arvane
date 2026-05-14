#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace arv::io {

struct FileReadError {
  std::filesystem::path path{};
  std::string message{};
};

using BinaryFileResult = Expected<std::vector<u8>, FileReadError>;
using TextFileContentsResult = Expected<std::string, FileReadError>;

[[nodiscard]] BinaryFileResult read_binary_file(const std::filesystem::path& path);
[[nodiscard]] std::optional<std::vector<u8>> try_read_binary_file(
    const std::filesystem::path& path);
[[nodiscard]] TextFileContentsResult read_text_file_contents(
    const std::filesystem::path& path);
[[nodiscard]] bool regular_file_exists(const std::filesystem::path& path);

} // namespace arv::io
