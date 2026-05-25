#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <map>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace arv::io {

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

struct DataDocument;

template<typename T, typename Enable = void>
struct DataCodec;

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

[[nodiscard]] inline bool is_integer_value(const DataValue& value) noexcept {
  return value.kind() == DataKind::I64 || value.kind() == DataKind::U64;
}

[[nodiscard]] inline bool is_number_value(const DataValue& value) noexcept {
  return is_integer_value(value) || value.kind() == DataKind::F64;
}

[[nodiscard]] inline bool is_string_value(const DataValue& value) noexcept {
  return value.kind() == DataKind::String;
}

[[nodiscard]] inline bool is_object_value(const DataValue& value) noexcept {
  return value.kind() == DataKind::Object;
}

[[nodiscard]] inline std::optional<std::string_view> string_value_if(
    const DataValue& value) noexcept {
  return is_string_value(value)
             ? std::optional<std::string_view>{value.as_string_or({})}
             : std::nullopt;
}

struct DataDocument {
  struct ValueProxy {
    const DataDocument* document{};
    std::string_view path{};

    template<typename T>
    [[nodiscard]] operator Expected<T, DataDiagnostic>() const {
      return document == nullptr
                 ? Expected<T, DataDiagnostic>::failure(
                       {"root", "data document is unavailable"})
                 : document->value_as<T>(path);
    }

    template<typename T>
    [[nodiscard]] Expected<T, DataDiagnostic> as() const {
      return static_cast<Expected<T, DataDiagnostic>>(*this);
    }
  };

  u32 schema_version{};
  DataValue root{DataValue::object()};

  [[nodiscard]] ValueProxy value(std::string_view path) const noexcept {
    return {this, path};
  }

  template<typename T>
  [[nodiscard]] Expected<T, DataDiagnostic> value(std::string_view path) const {
    return value_as<T>(path);
  }

  template<typename T>
  [[nodiscard]] Expected<T, DataDiagnostic> value_as(std::string_view path) const {
    return DataCodec<T>::decode(*this, path);
  }

  [[nodiscard]] Expected<bool, DataDiagnostic> bool_value(std::string_view path) const;
  [[nodiscard]] bool bool_value(std::string_view path, bool fallback) const;

  [[nodiscard]] Expected<i64, DataDiagnostic> i64_value(std::string_view path) const;
  [[nodiscard]] i64 i64_value(std::string_view path, i64 fallback) const;

  [[nodiscard]] Expected<u64, DataDiagnostic> u64_value(std::string_view path) const;
  [[nodiscard]] u64 u64_value(std::string_view path, u64 fallback) const;

  [[nodiscard]] Expected<f64, DataDiagnostic> f64_value(std::string_view path) const;
  [[nodiscard]] f64 f64_value(std::string_view path, f64 fallback) const;

  [[nodiscard]] Expected<std::string_view, DataDiagnostic> string_value(std::string_view path) const;
  [[nodiscard]] std::string_view string_value(
      std::string_view path,
      std::string_view fallback) const;

  [[nodiscard]] Expected<std::span<const u8>, DataDiagnostic> bytes_value(std::string_view path) const;
  [[nodiscard]] std::span<const u8> bytes_value(
      std::string_view path,
      std::span<const u8> fallback) const;
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

template<>
struct DataCodec<bool> {
  [[nodiscard]] static Expected<bool, DataDiagnostic> decode(
      const DataDocument& document,
      std::string_view path) {
    return document.bool_value(path);
  }
};

template<typename T>
struct DataCodec<
    T,
    std::enable_if_t<
        std::is_integral_v<T> && std::is_signed_v<T> &&
        !std::is_same_v<T, bool>>> {
  [[nodiscard]] static Expected<T, DataDiagnostic> decode(
      const DataDocument& document,
      std::string_view path) {
    auto value = document.i64_value(path);
    if (!value) {
      return Expected<T, DataDiagnostic>::failure(std::move(value.error()));
    }
    if (value.value() < static_cast<i64>(std::numeric_limits<T>::min()) ||
        value.value() > static_cast<i64>(std::numeric_limits<T>::max())) {
      return Expected<T, DataDiagnostic>::failure(
          {std::string{"root."} + std::string{path}, "integer value is out of range"});
    }
    return Expected<T, DataDiagnostic>::success(static_cast<T>(value.value()));
  }
};

template<typename T>
struct DataCodec<
    T,
    std::enable_if_t<std::is_integral_v<T> && std::is_unsigned_v<T>>> {
  [[nodiscard]] static Expected<T, DataDiagnostic> decode(
      const DataDocument& document,
      std::string_view path) {
    auto value = document.u64_value(path);
    if (!value) {
      return Expected<T, DataDiagnostic>::failure(std::move(value.error()));
    }
    if (value.value() > static_cast<u64>(std::numeric_limits<T>::max())) {
      return Expected<T, DataDiagnostic>::failure(
          {std::string{"root."} + std::string{path}, "integer value is out of range"});
    }
    return Expected<T, DataDiagnostic>::success(static_cast<T>(value.value()));
  }
};

template<typename T>
struct DataCodec<T, std::enable_if_t<std::is_floating_point_v<T>>> {
  [[nodiscard]] static Expected<T, DataDiagnostic> decode(
      const DataDocument& document,
      std::string_view path) {
    auto value = document.f64_value(path);
    if (!value) {
      return Expected<T, DataDiagnostic>::failure(std::move(value.error()));
    }
    return Expected<T, DataDiagnostic>::success(static_cast<T>(value.value()));
  }
};

template<>
struct DataCodec<std::string> {
  [[nodiscard]] static Expected<std::string, DataDiagnostic> decode(
      const DataDocument& document,
      std::string_view path) {
    auto value = document.string_value(path);
    if (!value) {
      return Expected<std::string, DataDiagnostic>::failure(std::move(value.error()));
    }
    return Expected<std::string, DataDiagnostic>::success(std::string{value.value()});
  }
};

template<>
struct DataCodec<std::string_view> {
  [[nodiscard]] static Expected<std::string_view, DataDiagnostic> decode(
      const DataDocument& document,
      std::string_view path) {
    return document.string_value(path);
  }
};

template<>
struct DataCodec<std::span<const u8>> {
  [[nodiscard]] static Expected<std::span<const u8>, DataDiagnostic> decode(
      const DataDocument& document,
      std::string_view path) {
    return document.bytes_value(path);
  }
};

} // namespace arv::io
