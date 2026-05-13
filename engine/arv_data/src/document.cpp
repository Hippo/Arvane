#include "arv/data/document.hpp"

#include <limits>
#include <utility>

namespace arv::data {

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
    return DataPathResult::failure({current_path, "parent value is not an object"});
  }
  current->set(parts.value().back(), std::move(value));
  return DataPathResult::success();
}

} // namespace arv::data
