#include "config_internal.hpp"

#include <fstream>
#include <sstream>

namespace arv::io::detail {

[[nodiscard]] DataValue config_value(bool value) {
  return DataValue{value};
}

[[nodiscard]] DataValue config_value(std::string_view value) {
  return DataValue{value};
}

[[nodiscard]] DataValue config_value(const std::string& value) {
  return DataValue{value};
}

[[nodiscard]] DataValue config_value(const char* value) {
  return DataValue{value};
}

[[nodiscard]] DataValue config_value(const std::filesystem::path& value) {
  return DataValue{value.string()};
}

void put_path(DataValue& root, std::string_view path, DataValue value) {
  DataDocument document{0U, std::move(root)};
  (void)set_path(document, path, std::move(value));
  root = std::move(document.root);
}

[[nodiscard]] DataValue string_array_value(const std::vector<std::string>& values) {
  DataValue array = DataValue::array();
  for (const std::string& value : values) {
    array.push(DataValue{value});
  }
  return array;
}

void read_path_scalar(const DataValue& parent, const char* key, std::filesystem::path& value) {
  const DataValue* child = parent.get(key);
  if (child != nullptr && child->kind() == DataKind::String) {
    value = std::filesystem::path{std::string{child->as_string_or({})}};
  }
}

void read_string_sequence(
    const DataValue& parent,
    const char* key,
    std::vector<std::string>& values) {
  const DataValue* child = parent.get(key);
  const DataValue::Array* array = child == nullptr ? nullptr : child->array_if();
  if (array == nullptr) {
    return;
  }
  values.clear();
  values.reserve(array->size());
  for (const DataValue& node : *array) {
    if (node.kind() == DataKind::String) {
      values.emplace_back(node.as_string_or({}));
    }
  }
}

} // namespace arv::io::detail

namespace arv::io {

TextFileResult read_text_file(const std::filesystem::path& path, std::string_view purpose) {
  std::ifstream input(path);
  if (!input) {
    std::string message = "failed to open ";
    message += purpose;
    message += ": ";
    message += path.string();
    return TextFileResult::failure(ConfigError{std::move(message)});
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return TextFileResult::success(buffer.str());
}

} // namespace arv::io
