#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace arv::data {

enum class DataKind : u8 {
  Null,
  Bool,
  I64,
  U64,
  F64,
  String,
  Bytes,
  Array,
  Object,
};

struct DataDiagnostic {
  std::string path{"root"};
  std::string message{};

  [[nodiscard]] std::string describe() const;
};

class DataValue {
public:
  using Bytes = std::vector<u8>;
  using Array = std::vector<DataValue>;
  using Object = std::map<std::string, DataValue, std::less<>>;

  DataValue() = default;
  DataValue(std::nullptr_t) noexcept;
  DataValue(bool value) noexcept;
  DataValue(i64 value) noexcept;
  DataValue(u64 value) noexcept;
  DataValue(f64 value) noexcept;
  DataValue(std::string value);
  DataValue(std::string_view value);
  DataValue(const char* value);
  DataValue(Bytes value);
  DataValue(Array value);
  DataValue(Object value);

  [[nodiscard]] static DataValue bytes(std::span<const u8> bytes);
  [[nodiscard]] static DataValue array();
  [[nodiscard]] static DataValue object();

  [[nodiscard]] DataKind kind() const noexcept;

  [[nodiscard]] bool as_bool_or(bool fallback) const noexcept;
  [[nodiscard]] i64 as_i64_or(i64 fallback) const noexcept;
  [[nodiscard]] u64 as_u64_or(u64 fallback) const noexcept;
  [[nodiscard]] f64 as_f64_or(f64 fallback) const noexcept;
  [[nodiscard]] std::string_view as_string_or(std::string_view fallback) const noexcept;

  [[nodiscard]] const Bytes* bytes_if() const noexcept;
  [[nodiscard]] Bytes* bytes_if() noexcept;
  [[nodiscard]] const Array* array_if() const noexcept;
  [[nodiscard]] Array* array_if() noexcept;
  [[nodiscard]] const Object* object_if() const noexcept;
  [[nodiscard]] Object* object_if() noexcept;

  [[nodiscard]] usize size() const noexcept;
  [[nodiscard]] const DataValue* at(usize index) const noexcept;
  [[nodiscard]] DataValue* at(usize index) noexcept;
  void push(DataValue value);

  [[nodiscard]] const DataValue* get(std::string_view key) const noexcept;
  [[nodiscard]] DataValue* get(std::string_view key) noexcept;
  DataValue& set(std::string key, DataValue value);

private:
  using Storage = std::variant<
      std::monostate,
      bool,
      i64,
      u64,
      f64,
      std::string,
      Bytes,
      Array,
      Object>;

  Storage storage_{std::monostate{}};
};

struct DataDocument {
  u32 schema_version{};
  DataValue root{DataValue::object()};
};

using DataPathResult = Expected<void, DataDiagnostic>;

[[nodiscard]] std::string data_kind_name(DataKind kind);
[[nodiscard]] std::optional<std::vector<std::string>> split_data_path(
    std::string_view path,
    std::string& error);
[[nodiscard]] const DataValue* find_path(
    const DataDocument& document,
    std::string_view path,
    DataDiagnostic* diagnostic = nullptr);
[[nodiscard]] DataValue* find_path(
    DataDocument& document,
    std::string_view path,
    DataDiagnostic* diagnostic = nullptr);
[[nodiscard]] DataPathResult set_path(
    DataDocument& document,
    std::string_view path,
    DataValue value);

} // namespace arv::data
