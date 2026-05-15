#pragma once

#include "arv/io/config.hpp"
#include "arv/io/data_codecs.hpp"
#include "arv/io/data_document.hpp"

#include <filesystem>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace arv::io::detail {

inline constexpr f64 ConfigFloatDecimalScale = 1'000'000.0;

[[nodiscard]] inline f64 round_config_float(f64 value) noexcept {
  if (!std::isfinite(value)) {
    return value;
  }
  const f64 rounded = std::round(value * ConfigFloatDecimalScale) /
                      ConfigFloatDecimalScale;
  return rounded == 0.0 ? 0.0 : rounded;
}

[[nodiscard]] DataValue config_value(bool value);

template<typename T>
[[nodiscard]] DataValue config_value(T value)
  requires(std::is_integral_v<T> && std::is_signed_v<T> && !std::is_same_v<T, bool>)
{
  return DataValue{static_cast<i64>(value)};
}

template<typename T>
[[nodiscard]] DataValue config_value(T value)
  requires(std::is_integral_v<T> && std::is_unsigned_v<T> && !std::is_same_v<T, bool>)
{
  return DataValue{static_cast<u64>(value)};
}

template<typename T>
[[nodiscard]] DataValue config_value(T value)
  requires(std::is_floating_point_v<T>)
{
  return DataValue{round_config_float(static_cast<f64>(value))};
}

[[nodiscard]] DataValue config_value(std::string_view value);
[[nodiscard]] DataValue config_value(const std::string& value);
[[nodiscard]] DataValue config_value(const char* value);
[[nodiscard]] DataValue config_value(const std::filesystem::path& value);

void put_path(DataValue& root, std::string_view path, DataValue value);

template<typename T>
void put(DataValue& root, std::string_view path, T&& value) {
  put_path(root, path, config_value(std::forward<T>(value)));
}

[[nodiscard]] DataValue string_array_value(const std::vector<std::string>& values);

inline void put_under(
    DataValue& root,
    std::string_view prefix,
    std::string_view key,
    DataValue value) {
  std::string path{prefix};
  path.push_back('.');
  path += key;
  put_path(root, path, std::move(value));
}

template<typename T>
void put_under(DataValue& root, std::string_view prefix, std::string_view key, T&& value) {
  put_under(root, prefix, key, config_value(std::forward<T>(value)));
}

template<typename T>
void assign_config_scalar(const DataValue& child, T& value) {
  if constexpr (std::is_same_v<T, bool>) {
    if (child.kind() == DataKind::Bool) {
      value = child.as_bool_or(value);
    }
  } else if constexpr (std::is_same_v<T, std::string>) {
    if (child.kind() == DataKind::String) {
      value = std::string{child.as_string_or(value)};
    }
  } else if constexpr (std::is_floating_point_v<T>) {
    if (child.kind() == DataKind::I64 || child.kind() == DataKind::U64 ||
        child.kind() == DataKind::F64) {
      value = static_cast<T>(child.as_f64_or(value));
    }
  } else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>) {
    if (child.kind() == DataKind::I64 || child.kind() == DataKind::U64) {
      const u64 raw = child.as_u64_or(static_cast<u64>(value));
      if (raw <= static_cast<u64>(std::numeric_limits<T>::max())) {
        value = static_cast<T>(raw);
      }
    }
  } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
    if (child.kind() == DataKind::I64 || child.kind() == DataKind::U64) {
      const i64 raw = child.as_i64_or(static_cast<i64>(value));
      if (raw >= static_cast<i64>(std::numeric_limits<T>::min()) &&
          raw <= static_cast<i64>(std::numeric_limits<T>::max())) {
        value = static_cast<T>(raw);
      }
    }
  }
}

template<typename T>
void read_scalar(const DataValue& parent, const char* key, T& value) {
  const DataValue* child = parent.get(key);
  if (child != nullptr) {
    assign_config_scalar(*child, value);
  }
}

void read_path_scalar(const DataValue& parent, const char* key, std::filesystem::path& value);
void read_string_sequence(
    const DataValue& parent,
    const char* key,
    std::vector<std::string>& values);

std::filesystem::path normalize_lexical(std::filesystem::path path);
std::filesystem::path discover_project_root(std::filesystem::path start);
std::filesystem::path resolve_project_relative(
    const std::filesystem::path& project_root,
    const std::filesystem::path& candidate);
std::filesystem::path default_config_path_from(const std::filesystem::path& config_root);
std::filesystem::path default_shared_config_path_from(const std::filesystem::path& config_root);
std::filesystem::path default_client_config_path_from(const std::filesystem::path& config_root);
std::filesystem::path default_server_config_path_from(const std::filesystem::path& config_root);
std::filesystem::path default_input_bindings_path_from(const std::filesystem::path& config_root);
std::filesystem::path default_server_list_path_from(const std::filesystem::path& config_root);
std::filesystem::path default_profile_config_path_from(
    const std::filesystem::path& config_root,
    ConfigProfile profile);
std::string config_path_string(
    const std::filesystem::path& project_root,
    const std::filesystem::path& path);

void read_runtime_sections(const DataValue& root, EngineConfig& config);
void put_runtime_full(DataValue& root, const EngineConfig& config);
void put_runtime_shared(DataValue& root, const EngineConfig& config);
void put_runtime_client(DataValue& root, const EngineConfig& config);

void read_network_sections(const DataValue& root, EngineConfig& config);
void put_network_full(DataValue& root, const EngineConfig& config);
void put_network_client(DataValue& root, const EngineConfig& config);
void put_network_server(DataValue& root, const EngineConfig& config);

void read_plugin_sections(const DataValue& root, EngineConfig& config);
void put_plugin_policy_config(DataValue& root, const EngineConfig& config);

void read_gameplay_sections(const DataValue& root, EngineConfig& config);
void put_gameplay_full(DataValue& root, const EngineConfig& config);
void put_gameplay_shared(DataValue& root, const EngineConfig& config);
void put_gameplay_client(DataValue& root, const EngineConfig& config);

void read_render_ui_sections(const DataValue& root, EngineConfig& config);
void put_render_ui_full(DataValue& root, const EngineConfig& config);
void put_render_ui_client(DataValue& root, const EngineConfig& config);

void read_config_node(const DataValue& root, EngineConfig& config);
std::optional<ConfigError> validate_config(const EngineConfig& config);

ConfigResult save_default_config_file(const std::filesystem::path& path, const EngineConfig& config);
ConfigResult save_default_shared_config_file(
    const std::filesystem::path& path,
    const EngineConfig& config);
ConfigResult save_default_client_config_file(
    const std::filesystem::path& path,
    const EngineConfig& config);
ConfigResult save_default_server_config_file(
    const std::filesystem::path& path,
    const EngineConfig& config);
ConfigResult write_config_document_file(
    const std::filesystem::path& path,
    const DataValue& document_root,
    const EngineConfig& config);

} // namespace arv::io::detail
