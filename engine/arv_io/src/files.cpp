#include "arv/io/files.hpp"

#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace arv::io {

BinaryFileResult read_binary_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return BinaryFileResult::failure({path, "file could not be opened"});
  }

  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  if (size < 0) {
    return BinaryFileResult::failure({path, "file size could not be determined"});
  }
  input.seekg(0, std::ios::beg);

  std::vector<u8> bytes(static_cast<usize>(size));
  input.read(
      reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  if (!input && !input.eof()) {
    return BinaryFileResult::failure({path, "file read failed"});
  }
  return BinaryFileResult::success(std::move(bytes));
}

std::optional<std::vector<u8>> try_read_binary_file(
    const std::filesystem::path& path) {
  BinaryFileResult result = read_binary_file(path);
  if (!result) {
    return std::nullopt;
  }
  return std::move(result.value());
}

TextFileContentsResult read_text_file_contents(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return TextFileContentsResult::failure({path, "file could not be opened"});
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!input && !input.eof()) {
    return TextFileContentsResult::failure({path, "file read failed"});
  }
  return TextFileContentsResult::success(buffer.str());
}

bool regular_file_exists(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error);
}

} // namespace arv::io
