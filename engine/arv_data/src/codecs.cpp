#include "arv/data/codecs.hpp"

#include <cstring>
#include <charconv>
#include <limits>
#include <sstream>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

namespace arv::data {
namespace {

constexpr std::string_view BinaryMagic = "ARVDATA1";
constexpr u16 BinaryFormatVersion = 1U;

DataCodecError error_at(std::string path, std::string message) {
  return {std::move(path), std::move(message)};
}

YAML::Node value_to_yaml(const DataValue& value) {
  YAML::Node node;
  switch (value.kind()) {
    case DataKind::Null:
      return node;
    case DataKind::Bool:
      node = value.as_bool_or(false);
      return node;
    case DataKind::I64:
      node = value.as_i64_or(0);
      return node;
    case DataKind::U64:
      node = value.as_u64_or(0);
      return node;
    case DataKind::F64:
      node = value.as_f64_or(0.0);
      return node;
    case DataKind::String:
      node = std::string{value.as_string_or({})};
      return node;
    case DataKind::Bytes: {
      node.SetTag("!bytes");
      const auto* bytes = value.bytes_if();
      if (bytes != nullptr) {
        for (u8 byte : *bytes) {
          node.push_back(static_cast<u32>(byte));
        }
      }
      return node;
    }
    case DataKind::Array: {
      const auto* array = value.array_if();
      if (array != nullptr) {
        for (const DataValue& child : *array) {
          node.push_back(value_to_yaml(child));
        }
      }
      return node;
    }
    case DataKind::Object: {
      const auto* object = value.object_if();
      if (object != nullptr) {
        for (const auto& [key, child] : *object) {
          node[key] = value_to_yaml(child);
        }
      }
      return node;
    }
  }
  return node;
}

DataDocumentResult value_from_yaml(const YAML::Node& node, std::string path);

DataDocumentResult scalar_from_yaml(const YAML::Node& node, std::string path) {
  if (node.IsNull()) {
    return DataDocumentResult::success({0U, nullptr});
  }
  try {
    const std::string text = node.as<std::string>();
    if (text == "true") {
      return DataDocumentResult::success({0U, true});
    }
    if (text == "false") {
      return DataDocumentResult::success({0U, false});
    }
    if (!text.empty() && text.find_first_of(".eE") == std::string::npos) {
      if (text[0] == '-') {
        i64 value{};
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (ec == std::errc{} && ptr == text.data() + text.size()) {
          return DataDocumentResult::success({0U, value});
        }
      } else {
        u64 value{};
        const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (ec == std::errc{} && ptr == text.data() + text.size()) {
          return DataDocumentResult::success({0U, value});
        }
      }
    }
    if (!text.empty() && text.find_first_of(".eE") != std::string::npos) {
      f64 value{};
      const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
      if (ec == std::errc{} && ptr == text.data() + text.size()) {
        return DataDocumentResult::success({0U, value});
      }
    }
    return DataDocumentResult::success({0U, text});
  } catch (const YAML::Exception&) {
    return DataDocumentResult::failure(error_at(std::move(path), "failed to decode scalar"));
  }
}

DataDocumentResult value_from_yaml(const YAML::Node& node, std::string path) {
  try {
    if (node.Tag().find("bytes") != std::string::npos) {
      if (!node.IsSequence()) {
        return DataDocumentResult::failure(
            error_at(std::move(path), "!bytes value must be a sequence"));
      }
      std::vector<u8> bytes;
      bytes.reserve(node.size());
      for (usize index = 0U; index < node.size(); ++index) {
        const u32 byte = node[index].as<u32>();
        if (byte > std::numeric_limits<u8>::max()) {
          return DataDocumentResult::failure(
              error_at(path + "[" + std::to_string(index) + "]", "byte value is out of range"));
        }
        bytes.push_back(static_cast<u8>(byte));
      }
      return DataDocumentResult::success({0U, DataValue{std::move(bytes)}});
    }
    if (node.IsScalar() || node.IsNull()) {
      return scalar_from_yaml(node, std::move(path));
    }
    if (node.IsSequence()) {
      DataValue::Array array;
      array.reserve(node.size());
      for (usize index = 0U; index < node.size(); ++index) {
        auto child = value_from_yaml(node[index], path + "[" + std::to_string(index) + "]");
        if (!child) {
          return child;
        }
        array.push_back(std::move(child.value().root));
      }
      return DataDocumentResult::success({0U, DataValue{std::move(array)}});
    }
    if (node.IsMap()) {
      DataValue::Object object;
      for (const auto& entry : node) {
        const std::string key = entry.first.as<std::string>();
        auto child = value_from_yaml(entry.second, path + "." + key);
        if (!child) {
          return child;
        }
        object.emplace(key, std::move(child.value().root));
      }
      return DataDocumentResult::success({0U, DataValue{std::move(object)}});
    }
  } catch (const YAML::Exception& ex) {
    return DataDocumentResult::failure(error_at(std::move(path), ex.what()));
  }
  return DataDocumentResult::failure(error_at(std::move(path), "unsupported YAML node kind"));
}

nlohmann::json value_to_json(const DataValue& value) {
  switch (value.kind()) {
    case DataKind::Null:
      return nullptr;
    case DataKind::Bool:
      return value.as_bool_or(false);
    case DataKind::I64:
      return value.as_i64_or(0);
    case DataKind::U64:
      return value.as_u64_or(0);
    case DataKind::F64:
      return value.as_f64_or(0.0);
    case DataKind::String:
      return std::string{value.as_string_or({})};
    case DataKind::Bytes: {
      nlohmann::json bytes = nlohmann::json::array();
      if (const auto* raw = value.bytes_if(); raw != nullptr) {
        for (u8 byte : *raw) {
          bytes.push_back(byte);
        }
      }
      return nlohmann::json{{"$arvane_bytes", std::move(bytes)}};
    }
    case DataKind::Array: {
      nlohmann::json array = nlohmann::json::array();
      if (const auto* values = value.array_if(); values != nullptr) {
        for (const DataValue& child : *values) {
          array.push_back(value_to_json(child));
        }
      }
      return array;
    }
    case DataKind::Object: {
      nlohmann::json object = nlohmann::json::object();
      if (const auto* values = value.object_if(); values != nullptr) {
        for (const auto& [key, child] : *values) {
          object[key] = value_to_json(child);
        }
      }
      return object;
    }
  }
  return nullptr;
}

DataDocumentResult value_from_json(const nlohmann::json& node, std::string path) {
  try {
    if (node.is_null()) {
      return DataDocumentResult::success({0U, nullptr});
    }
    if (node.is_boolean()) {
      return DataDocumentResult::success({0U, node.get<bool>()});
    }
    if (node.is_number_integer()) {
      return DataDocumentResult::success({0U, node.get<i64>()});
    }
    if (node.is_number_unsigned()) {
      return DataDocumentResult::success({0U, node.get<u64>()});
    }
    if (node.is_number_float()) {
      return DataDocumentResult::success({0U, node.get<f64>()});
    }
    if (node.is_string()) {
      return DataDocumentResult::success({0U, node.get<std::string>()});
    }
    if (node.is_array()) {
      DataValue::Array array;
      array.reserve(node.size());
      for (usize index = 0U; index < node.size(); ++index) {
        auto child = value_from_json(node[index], path + "[" + std::to_string(index) + "]");
        if (!child) {
          return child;
        }
        array.push_back(std::move(child.value().root));
      }
      return DataDocumentResult::success({0U, DataValue{std::move(array)}});
    }
    if (node.is_object()) {
      if (node.size() == 1U && node.contains("$arvane_bytes")) {
        const nlohmann::json& bytes_json = node.at("$arvane_bytes");
        if (!bytes_json.is_array()) {
          return DataDocumentResult::failure(
              error_at(std::move(path), "$arvane_bytes must be an array"));
        }
        std::vector<u8> bytes;
        bytes.reserve(bytes_json.size());
        for (usize index = 0U; index < bytes_json.size(); ++index) {
          const u32 byte = bytes_json[index].get<u32>();
          if (byte > std::numeric_limits<u8>::max()) {
            return DataDocumentResult::failure(
                error_at(path + ".$arvane_bytes[" + std::to_string(index) + "]", "byte value is out of range"));
          }
          bytes.push_back(static_cast<u8>(byte));
        }
        return DataDocumentResult::success({0U, DataValue{std::move(bytes)}});
      }
      DataValue::Object object;
      for (auto iter = node.begin(); iter != node.end(); ++iter) {
        auto child = value_from_json(iter.value(), path + "." + iter.key());
        if (!child) {
          return child;
        }
        object.emplace(iter.key(), std::move(child.value().root));
      }
      return DataDocumentResult::success({0U, DataValue{std::move(object)}});
    }
  } catch (const nlohmann::json::exception& ex) {
    return DataDocumentResult::failure(error_at(std::move(path), ex.what()));
  }
  return DataDocumentResult::failure(error_at(std::move(path), "unsupported JSON node kind"));
}

class BinaryWriter {
public:
  template<typename T, bool IsEnum = std::is_enum_v<T>>
  struct RawType {
    using Type = T;
  };

  template<typename T>
  struct RawType<T, true> {
    using Type = std::underlying_type_t<T>;
  };

  template<typename T>
  void write(T value) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    using Raw = typename RawType<T>::Type;
    const Raw raw = static_cast<Raw>(value);
    const usize offset = bytes_.size();
    bytes_.resize(offset + sizeof(Raw));
    for (usize i = 0U; i < sizeof(Raw); ++i) {
      bytes_[offset + i] = static_cast<u8>((static_cast<std::make_unsigned_t<Raw>>(raw) >> (i * 8U)) & 0xFFU);
    }
  }

  void write_f64(f64 value) {
    u64 raw{};
    std::memcpy(&raw, &value, sizeof(raw));
    write(raw);
  }

  void write_bytes(std::span<const u8> bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
  }

  std::vector<u8> take() && {
    return std::move(bytes_);
  }

private:
  std::vector<u8> bytes_{};
};

class BinaryReader {
public:
  explicit BinaryReader(std::span<const u8> bytes) : bytes_(bytes) {
  }

  template<typename T, bool IsEnum = std::is_enum_v<T>>
  struct RawType {
    using Type = T;
  };

  template<typename T>
  struct RawType<T, true> {
    using Type = std::underlying_type_t<T>;
  };

  template<typename T>
  Expected<T, DataCodecError> read(std::string path) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    using Raw = typename RawType<T>::Type;
    if (remaining() < sizeof(Raw)) {
      return Expected<T, DataCodecError>::failure(error_at(std::move(path), "unexpected end of data"));
    }
    std::make_unsigned_t<Raw> raw{};
    for (usize i = 0U; i < sizeof(Raw); ++i) {
      raw |= static_cast<std::make_unsigned_t<Raw>>(bytes_[offset_ + i]) << (i * 8U);
    }
    offset_ += sizeof(Raw);
    return Expected<T, DataCodecError>::success(static_cast<T>(static_cast<Raw>(raw)));
  }

  Expected<f64, DataCodecError> read_f64(std::string path) {
    auto raw = read<u64>(std::move(path));
    if (!raw) {
      return Expected<f64, DataCodecError>::failure(raw.error());
    }
    f64 value{};
    const u64 bits = raw.value();
    std::memcpy(&value, &bits, sizeof(value));
    return Expected<f64, DataCodecError>::success(value);
  }

  Expected<std::vector<u8>, DataCodecError> read_bytes(usize count, std::string path) {
    if (remaining() < count) {
      return Expected<std::vector<u8>, DataCodecError>::failure(
          error_at(std::move(path), "unexpected end of data"));
    }
    std::vector<u8> bytes(bytes_.begin() + static_cast<isize>(offset_),
                          bytes_.begin() + static_cast<isize>(offset_ + count));
    offset_ += count;
    return Expected<std::vector<u8>, DataCodecError>::success(std::move(bytes));
  }

  [[nodiscard]] usize remaining() const noexcept {
    return bytes_.size() - offset_;
  }

private:
  std::span<const u8> bytes_;
  usize offset_{};
};

void write_string(BinaryWriter& writer, std::string_view text) {
  writer.write<u32>(static_cast<u32>(text.size()));
  writer.write_bytes(std::span{
      reinterpret_cast<const u8*>(text.data()),
      text.size()});
}

void write_value(BinaryWriter& writer, const DataValue& value) {
  writer.write<DataKind>(value.kind());
  switch (value.kind()) {
    case DataKind::Null:
      break;
    case DataKind::Bool:
      writer.write<u8>(value.as_bool_or(false) ? 1U : 0U);
      break;
    case DataKind::I64:
      writer.write<i64>(value.as_i64_or(0));
      break;
    case DataKind::U64:
      writer.write<u64>(value.as_u64_or(0));
      break;
    case DataKind::F64:
      writer.write_f64(value.as_f64_or(0.0));
      break;
    case DataKind::String:
      write_string(writer, value.as_string_or({}));
      break;
    case DataKind::Bytes: {
      const auto* bytes = value.bytes_if();
      const usize count = bytes == nullptr ? 0U : bytes->size();
      writer.write<u32>(static_cast<u32>(count));
      if (bytes != nullptr) {
        writer.write_bytes(*bytes);
      }
      break;
    }
    case DataKind::Array: {
      const auto* array = value.array_if();
      const usize count = array == nullptr ? 0U : array->size();
      writer.write<u32>(static_cast<u32>(count));
      if (array != nullptr) {
        for (const DataValue& child : *array) {
          write_value(writer, child);
        }
      }
      break;
    }
    case DataKind::Object: {
      const auto* object = value.object_if();
      const usize count = object == nullptr ? 0U : object->size();
      writer.write<u32>(static_cast<u32>(count));
      if (object != nullptr) {
        for (const auto& [key, child] : *object) {
          write_string(writer, key);
          write_value(writer, child);
        }
      }
      break;
    }
  }
}

DataDocumentResult read_value(BinaryReader& reader, std::string path) {
  auto kind_result = reader.read<DataKind>(path);
  if (!kind_result) {
    return DataDocumentResult::failure(kind_result.error());
  }
  switch (kind_result.value()) {
    case DataKind::Null:
      return DataDocumentResult::success({0U, nullptr});
    case DataKind::Bool: {
      auto raw = reader.read<u8>(path);
      if (!raw) {
        return DataDocumentResult::failure(raw.error());
      }
      if (raw.value() > 1U) {
        return DataDocumentResult::failure(error_at(std::move(path), "invalid bool encoding"));
      }
      return DataDocumentResult::success({0U, raw.value() == 1U});
    }
    case DataKind::I64: {
      auto value = reader.read<i64>(path);
      return value ? DataDocumentResult::success({0U, value.value()})
                   : DataDocumentResult::failure(value.error());
    }
    case DataKind::U64: {
      auto value = reader.read<u64>(path);
      return value ? DataDocumentResult::success({0U, value.value()})
                   : DataDocumentResult::failure(value.error());
    }
    case DataKind::F64: {
      auto value = reader.read_f64(path);
      return value ? DataDocumentResult::success({0U, value.value()})
                   : DataDocumentResult::failure(value.error());
    }
    case DataKind::String: {
      auto size = reader.read<u32>(path);
      if (!size) {
        return DataDocumentResult::failure(size.error());
      }
      auto bytes = reader.read_bytes(size.value(), path);
      if (!bytes) {
        return DataDocumentResult::failure(bytes.error());
      }
      return DataDocumentResult::success({
          0U,
          std::string{reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()}});
    }
    case DataKind::Bytes: {
      auto size = reader.read<u32>(path);
      if (!size) {
        return DataDocumentResult::failure(size.error());
      }
      auto bytes = reader.read_bytes(size.value(), path);
      return bytes ? DataDocumentResult::success({0U, DataValue{std::move(bytes.value())}})
                   : DataDocumentResult::failure(bytes.error());
    }
    case DataKind::Array: {
      auto size = reader.read<u32>(path);
      if (!size) {
        return DataDocumentResult::failure(size.error());
      }
      DataValue::Array array;
      array.reserve(size.value());
      for (u32 index = 0U; index < size.value(); ++index) {
        auto child = read_value(reader, path + "[" + std::to_string(index) + "]");
        if (!child) {
          return child;
        }
        array.push_back(std::move(child.value().root));
      }
      return DataDocumentResult::success({0U, DataValue{std::move(array)}});
    }
    case DataKind::Object: {
      auto size = reader.read<u32>(path);
      if (!size) {
        return DataDocumentResult::failure(size.error());
      }
      DataValue::Object object;
      for (u32 index = 0U; index < size.value(); ++index) {
        auto key_size = reader.read<u32>(path);
        if (!key_size) {
          return DataDocumentResult::failure(key_size.error());
        }
        auto key_bytes = reader.read_bytes(key_size.value(), path);
        if (!key_bytes) {
          return DataDocumentResult::failure(key_bytes.error());
        }
        std::string key{
            reinterpret_cast<const char*>(key_bytes.value().data()),
            key_bytes.value().size()};
        auto child = read_value(reader, path + "." + key);
        if (!child) {
          return child;
        }
        object.emplace(std::move(key), std::move(child.value().root));
      }
      return DataDocumentResult::success({0U, DataValue{std::move(object)}});
    }
  }
  return DataDocumentResult::failure(error_at(std::move(path), "unknown data kind"));
}

} // namespace

std::string DataCodecError::describe() const {
  return path + ": " + message;
}

DataTextResult encode_yaml(const DataDocument& document) {
  YAML::Node root;
  root["schema_version"] = document.schema_version;
  root["data"] = value_to_yaml(document.root);
  YAML::Emitter emitter;
  emitter << root;
  if (!emitter.good()) {
    return DataTextResult::failure(error_at("root", emitter.GetLastError()));
  }
  std::string text = emitter.c_str();
  text.push_back('\n');
  return DataTextResult::success(std::move(text));
}

DataDocumentResult decode_yaml(std::string_view text) {
  try {
    YAML::Node root = YAML::Load(std::string{text});
    if (!root || !root.IsMap()) {
      return DataDocumentResult::failure(error_at("root", "document must be a YAML map"));
    }
    const YAML::Node version = root["schema_version"];
    if (!version || !version.IsScalar()) {
      return DataDocumentResult::failure(
          error_at("root.schema_version", "schema_version is required"));
    }
    const YAML::Node data = root["data"];
    if (!data) {
      return DataDocumentResult::failure(error_at("root.data", "data is required"));
    }
    auto decoded = value_from_yaml(data, "root.data");
    if (!decoded) {
      return decoded;
    }
    decoded.value().schema_version = version.as<u32>();
    return decoded;
  } catch (const YAML::Exception& ex) {
    return DataDocumentResult::failure(error_at("root", ex.what()));
  }
}

DataTextResult encode_json(const DataDocument& document) {
  nlohmann::json root{
      {"schema_version", document.schema_version},
      {"data", value_to_json(document.root)},
  };
  return DataTextResult::success(root.dump(2) + "\n");
}

DataDocumentResult decode_json(std::string_view text) {
  try {
    const nlohmann::json root = nlohmann::json::parse(text);
    if (!root.is_object()) {
      return DataDocumentResult::failure(error_at("root", "document must be a JSON object"));
    }
    if (!root.contains("schema_version") || !root.at("schema_version").is_number_unsigned()) {
      return DataDocumentResult::failure(
          error_at("root.schema_version", "schema_version is required"));
    }
    if (!root.contains("data")) {
      return DataDocumentResult::failure(error_at("root.data", "data is required"));
    }
    auto decoded = value_from_json(root.at("data"), "root.data");
    if (!decoded) {
      return decoded;
    }
    decoded.value().schema_version = root.at("schema_version").get<u32>();
    return decoded;
  } catch (const nlohmann::json::exception& ex) {
    return DataDocumentResult::failure(error_at("root", ex.what()));
  }
}

DataBytesResult encode_binary(const DataDocument& document) {
  BinaryWriter writer;
  writer.write_bytes(std::span{
      reinterpret_cast<const u8*>(BinaryMagic.data()),
      BinaryMagic.size()});
  writer.write<u16>(BinaryFormatVersion);
  writer.write<u32>(document.schema_version);
  write_value(writer, document.root);
  return DataBytesResult::success(std::move(writer).take());
}

DataDocumentResult decode_binary(std::span<const u8> bytes) {
  BinaryReader reader{bytes};
  auto magic = reader.read_bytes(BinaryMagic.size(), "root.magic");
  if (!magic) {
    return DataDocumentResult::failure(magic.error());
  }
  if (std::string_view{reinterpret_cast<const char*>(magic.value().data()), magic.value().size()} !=
      BinaryMagic) {
    return DataDocumentResult::failure(error_at("root.magic", "invalid data document magic"));
  }
  auto version = reader.read<u16>("root.format_version");
  if (!version) {
    return DataDocumentResult::failure(version.error());
  }
  if (version.value() != BinaryFormatVersion) {
    return DataDocumentResult::failure(error_at("root.format_version", "unsupported data document format version"));
  }
  auto schema_version = reader.read<u32>("root.schema_version");
  if (!schema_version) {
    return DataDocumentResult::failure(schema_version.error());
  }
  auto document = read_value(reader, "root.data");
  if (!document) {
    return document;
  }
  if (reader.remaining() != 0U) {
    return DataDocumentResult::failure(error_at("root", "trailing bytes after data document"));
  }
  document.value().schema_version = schema_version.value();
  return document;
}

} // namespace arv::data
