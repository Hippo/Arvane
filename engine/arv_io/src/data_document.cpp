#include "arv/io/data_document.hpp"

#include <limits>
#include <utility>

namespace arv::io {

std::string DataDiagnostic::describe() const {
  return path + ": " + message;
}

DataValue::DataValue(std::nullptr_t) noexcept : storage_(std::monostate{}) {
}

DataValue::DataValue(bool value) noexcept : storage_(value) {
}

DataValue::DataValue(i64 value) noexcept : storage_(value) {
}

DataValue::DataValue(u64 value) noexcept : storage_(value) {
}

DataValue::DataValue(f64 value) noexcept : storage_(value) {
}

DataValue::DataValue(std::string value) : storage_(std::move(value)) {
}

DataValue::DataValue(std::string_view value) : storage_(std::string{value}) {
}

DataValue::DataValue(const char* value) : storage_(std::string{value == nullptr ? "" : value}) {
}

DataValue::DataValue(Bytes value) : storage_(std::move(value)) {
}

DataValue::DataValue(Array value) : storage_(std::move(value)) {
}

DataValue::DataValue(Object value) : storage_(std::move(value)) {
}

DataValue DataValue::bytes(std::span<const u8> bytes) {
  return DataValue{Bytes{bytes.begin(), bytes.end()}};
}

DataValue DataValue::array() {
  return DataValue{Array{}};
}

DataValue DataValue::object() {
  return DataValue{Object{}};
}

DataKind DataValue::kind() const noexcept {
  switch (storage_.index()) {
    case 0:
      return DataKind::Null;
    case 1:
      return DataKind::Bool;
    case 2:
      return DataKind::I64;
    case 3:
      return DataKind::U64;
    case 4:
      return DataKind::F64;
    case 5:
      return DataKind::String;
    case 6:
      return DataKind::Bytes;
    case 7:
      return DataKind::Array;
    case 8:
      return DataKind::Object;
  }
  return DataKind::Null;
}

bool DataValue::as_bool_or(bool fallback) const noexcept {
  const auto* value = std::get_if<bool>(&storage_);
  return value == nullptr ? fallback : *value;
}

i64 DataValue::as_i64_or(i64 fallback) const noexcept {
  if (const auto* value = std::get_if<i64>(&storage_); value != nullptr) {
    return *value;
  }
  if (const auto* value = std::get_if<u64>(&storage_);
      value != nullptr && *value <= static_cast<u64>(std::numeric_limits<i64>::max())) {
    return static_cast<i64>(*value);
  }
  return fallback;
}

u64 DataValue::as_u64_or(u64 fallback) const noexcept {
  if (const auto* value = std::get_if<u64>(&storage_); value != nullptr) {
    return *value;
  }
  if (const auto* value = std::get_if<i64>(&storage_); value != nullptr && *value >= 0) {
    return static_cast<u64>(*value);
  }
  return fallback;
}

f64 DataValue::as_f64_or(f64 fallback) const noexcept {
  if (const auto* value = std::get_if<f64>(&storage_); value != nullptr) {
    return *value;
  }
  if (const auto* value = std::get_if<i64>(&storage_); value != nullptr) {
    return static_cast<f64>(*value);
  }
  if (const auto* value = std::get_if<u64>(&storage_); value != nullptr) {
    return static_cast<f64>(*value);
  }
  return fallback;
}

std::string_view DataValue::as_string_or(std::string_view fallback) const noexcept {
  const auto* value = std::get_if<std::string>(&storage_);
  return value == nullptr ? fallback : std::string_view{*value};
}

const DataValue::Bytes* DataValue::bytes_if() const noexcept {
  return std::get_if<Bytes>(&storage_);
}

DataValue::Bytes* DataValue::bytes_if() noexcept {
  return std::get_if<Bytes>(&storage_);
}

const DataValue::Array* DataValue::array_if() const noexcept {
  return std::get_if<Array>(&storage_);
}

DataValue::Array* DataValue::array_if() noexcept {
  return std::get_if<Array>(&storage_);
}

const DataValue::Object* DataValue::object_if() const noexcept {
  return std::get_if<Object>(&storage_);
}

DataValue::Object* DataValue::object_if() noexcept {
  return std::get_if<Object>(&storage_);
}

usize DataValue::size() const noexcept {
  if (const auto* array = array_if(); array != nullptr) {
    return array->size();
  }
  if (const auto* object = object_if(); object != nullptr) {
    return object->size();
  }
  if (const auto* bytes = bytes_if(); bytes != nullptr) {
    return bytes->size();
  }
  if (const auto* text = std::get_if<std::string>(&storage_); text != nullptr) {
    return text->size();
  }
  return 0U;
}

const DataValue* DataValue::at(usize index) const noexcept {
  const auto* array = array_if();
  return array == nullptr || index >= array->size() ? nullptr : &(*array)[index];
}

DataValue* DataValue::at(usize index) noexcept {
  auto* array = array_if();
  return array == nullptr || index >= array->size() ? nullptr : &(*array)[index];
}

void DataValue::push(DataValue value) {
  if (auto* array = array_if(); array != nullptr) {
    array->push_back(std::move(value));
    return;
  }
  storage_ = Array{std::move(value)};
}

const DataValue* DataValue::get(std::string_view key) const noexcept {
  const auto* object = object_if();
  if (object == nullptr) {
    return nullptr;
  }
  const auto found = object->find(key);
  return found == object->end() ? nullptr : &found->second;
}

DataValue* DataValue::get(std::string_view key) noexcept {
  auto* object = object_if();
  if (object == nullptr) {
    return nullptr;
  }
  const auto found = object->find(key);
  return found == object->end() ? nullptr : &found->second;
}

DataValue& DataValue::set(std::string key, DataValue value) {
  if (object_if() == nullptr) {
    storage_ = Object{};
  }
  auto& object = *object_if();
  auto [iter, inserted] = object.insert_or_assign(std::move(key), std::move(value));
  (void)inserted;
  return iter->second;
}

Expected<bool, DataDiagnostic> DataDocument::bool_value(std::string_view path) const {
  DataDiagnostic diagnostic;
  const DataValue* value = find_path(*this, path, &diagnostic);
  if (value == nullptr) {
    return Expected<bool, DataDiagnostic>::failure(std::move(diagnostic));
  }
  if (value->kind() != DataKind::Bool) {
    return Expected<bool, DataDiagnostic>::failure(
        {std::string{"root."} + std::string{path}, "data value is not a bool"});
  }
  return Expected<bool, DataDiagnostic>::success(value->as_bool_or(false));
}

bool DataDocument::bool_value(std::string_view path, bool fallback) const {
  auto result = bool_value(path);
  return result ? result.value() : fallback;
}

Expected<i64, DataDiagnostic> DataDocument::i64_value(std::string_view path) const {
  DataDiagnostic diagnostic;
  const DataValue* value = find_path(*this, path, &diagnostic);
  if (value == nullptr) {
    return Expected<i64, DataDiagnostic>::failure(std::move(diagnostic));
  }
  if (value->kind() != DataKind::I64 && value->kind() != DataKind::U64) {
    return Expected<i64, DataDiagnostic>::failure(
        {std::string{"root."} + std::string{path}, "data value is not an integer"});
  }
  return Expected<i64, DataDiagnostic>::success(value->as_i64_or(0));
}

i64 DataDocument::i64_value(std::string_view path, i64 fallback) const {
  auto result = i64_value(path);
  return result ? result.value() : fallback;
}

Expected<u64, DataDiagnostic> DataDocument::u64_value(std::string_view path) const {
  DataDiagnostic diagnostic;
  const DataValue* value = find_path(*this, path, &diagnostic);
  if (value == nullptr) {
    return Expected<u64, DataDiagnostic>::failure(std::move(diagnostic));
  }
  if (value->kind() != DataKind::I64 && value->kind() != DataKind::U64) {
    return Expected<u64, DataDiagnostic>::failure(
        {std::string{"root."} + std::string{path}, "data value is not an integer"});
  }
  return Expected<u64, DataDiagnostic>::success(value->as_u64_or(0));
}

u64 DataDocument::u64_value(std::string_view path, u64 fallback) const {
  auto result = u64_value(path);
  return result ? result.value() : fallback;
}

Expected<f64, DataDiagnostic> DataDocument::f64_value(std::string_view path) const {
  DataDiagnostic diagnostic;
  const DataValue* value = find_path(*this, path, &diagnostic);
  if (value == nullptr) {
    return Expected<f64, DataDiagnostic>::failure(std::move(diagnostic));
  }
  if (value->kind() != DataKind::I64 && value->kind() != DataKind::U64 &&
      value->kind() != DataKind::F64) {
    return Expected<f64, DataDiagnostic>::failure(
        {std::string{"root."} + std::string{path}, "data value is not numeric"});
  }
  return Expected<f64, DataDiagnostic>::success(value->as_f64_or(0.0));
}

f64 DataDocument::f64_value(std::string_view path, f64 fallback) const {
  auto result = f64_value(path);
  return result ? result.value() : fallback;
}

Expected<std::string_view, DataDiagnostic> DataDocument::string_value(std::string_view path) const {
  DataDiagnostic diagnostic;
  const DataValue* value = find_path(*this, path, &diagnostic);
  if (value == nullptr) {
    return Expected<std::string_view, DataDiagnostic>::failure(std::move(diagnostic));
  }
  if (value->kind() != DataKind::String) {
    return Expected<std::string_view, DataDiagnostic>::failure(
        {std::string{"root."} + std::string{path}, "data value is not a string"});
  }
  return Expected<std::string_view, DataDiagnostic>::success(value->as_string_or({}));
}

std::string_view DataDocument::string_value(
    std::string_view path,
    std::string_view fallback) const {
  auto result = string_value(path);
  return result ? result.value() : fallback;
}

Expected<std::span<const u8>, DataDiagnostic> DataDocument::bytes_value(std::string_view path) const {
  DataDiagnostic diagnostic;
  const DataValue* value = find_path(*this, path, &diagnostic);
  if (value == nullptr) {
    return Expected<std::span<const u8>, DataDiagnostic>::failure(std::move(diagnostic));
  }
  const DataValue::Bytes* bytes = value->bytes_if();
  if (bytes == nullptr) {
    return Expected<std::span<const u8>, DataDiagnostic>::failure(
        {std::string{"root."} + std::string{path}, "data value is not bytes"});
  }
  return Expected<std::span<const u8>, DataDiagnostic>::success(std::span<const u8>{*bytes});
}

std::span<const u8> DataDocument::bytes_value(
    std::string_view path,
    std::span<const u8> fallback) const {
  auto result = bytes_value(path);
  return result ? result.value() : fallback;
}

std::string data_kind_name(DataKind kind) {
  switch (kind) {
    case DataKind::Null:
      return "null";
    case DataKind::Bool:
      return "bool";
    case DataKind::I64:
      return "i64";
    case DataKind::U64:
      return "u64";
    case DataKind::F64:
      return "f64";
    case DataKind::String:
      return "string";
    case DataKind::Bytes:
      return "bytes";
    case DataKind::Array:
      return "array";
    case DataKind::Object:
      return "object";
  }
  return "unknown";
}

std::optional<std::vector<std::string>> split_data_path(
    std::string_view path,
    std::string& error) {
  std::vector<std::string> parts;
  if (path.empty()) {
    return parts;
  }
  usize start = 0U;
  while (start <= path.size()) {
    const usize dot = path.find('.', start);
    const usize end = dot == std::string_view::npos ? path.size() : dot;
    if (end == start) {
      error = "data path contains an empty segment";
      return std::nullopt;
    }
    parts.emplace_back(path.substr(start, end - start));
    if (dot == std::string_view::npos) {
      break;
    }
    start = dot + 1U;
  }
  return parts;
}

[[nodiscard]] std::optional<usize> parse_array_index(std::string_view part) noexcept {
  if (part.empty()) {
    return std::nullopt;
  }
  usize value = 0U;
  for (const char ch : part) {
    if (ch < '0' || ch > '9') {
      return std::nullopt;
    }
    value = value * 10U + static_cast<usize>(ch - '0');
  }
  return value;
}

const DataValue* find_path(
    const DataDocument& document,
    std::string_view path,
    DataDiagnostic* diagnostic) {
  std::string error;
  auto parts = split_data_path(path, error);
  if (!parts.has_value()) {
    if (diagnostic != nullptr) {
      *diagnostic = {"root", std::move(error)};
    }
    return nullptr;
  }
  const DataValue* current = &document.root;
  std::string current_path = "root";
  for (const std::string& part : parts.value()) {
    current_path += "." + part;
    if (const DataValue::Array* array = current->array_if(); array != nullptr) {
      std::optional<usize> index = parse_array_index(part);
      if (!index.has_value() || index.value() >= array->size()) {
        if (diagnostic != nullptr) {
          *diagnostic = {current_path, "array index was not found"};
        }
        return nullptr;
      }
      current = &(*array)[index.value()];
      continue;
    }
    current = current->get(part);
    if (current == nullptr) {
      if (diagnostic != nullptr) {
        *diagnostic = {current_path, "data path was not found"};
      }
      return nullptr;
    }
  }
  return current;
}

DataValue* find_path(
    DataDocument& document,
    std::string_view path,
    DataDiagnostic* diagnostic) {
  return const_cast<DataValue*>(
      find_path(static_cast<const DataDocument&>(document), path, diagnostic));
}

DataPathResult set_path(
    DataDocument& document,
    std::string_view path,
    DataValue value) {
  std::string error;
  auto parts = split_data_path(path, error);
  if (!parts.has_value()) {
    return DataPathResult::failure({"root", std::move(error)});
  }
  if (parts.value().empty()) {
    document.root = std::move(value);
    return DataPathResult::success();
  }

  DataValue* current = &document.root;
  std::string current_path = "root";
  for (usize index = 0U; index + 1U < parts.value().size(); ++index) {
    const std::string& part = parts.value()[index];
    current_path += "." + part;
    if (DataValue::Array* array = current->array_if(); array != nullptr) {
      std::optional<usize> array_index = parse_array_index(part);
      if (!array_index.has_value() || array_index.value() >= array->size()) {
        return DataPathResult::failure({current_path, "array index was not found"});
      }
      current = &(*array)[array_index.value()];
      continue;
    }
    if (current->kind() != DataKind::Object) {
      *current = DataValue::object();
    }
    DataValue* child = current->get(part);
    if (child == nullptr) {
      child = &current->set(part, DataValue::object());
    }
    current = child;
  }

  if (current->kind() != DataKind::Object) {
    if (DataValue::Array* array = current->array_if(); array != nullptr) {
      std::optional<usize> index = parse_array_index(parts.value().back());
      if (!index.has_value() || index.value() >= array->size()) {
        return DataPathResult::failure({current_path, "array index was not found"});
      }
      (*array)[index.value()] = std::move(value);
      return DataPathResult::success();
    }
    return DataPathResult::failure({current_path, "parent value is not an object"});
  }
  current->set(parts.value().back(), std::move(value));
  return DataPathResult::success();
}

} // namespace arv::io
