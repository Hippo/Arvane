#include "arv/io/data_codecs.hpp"

#include <limits>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace arv::io {
namespace {

DataCodecError error_at(std::string path, std::string message) {
  return {std::move(path), std::move(message)};
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
                error_at(path + ".$arvane_bytes[" + std::to_string(index) + "]",
                         "byte value is out of range"));
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

} // namespace

DataTextResult encode_json(const DataDocument& document) {
  nlohmann::json root{
      {"schema_version", document.schema_version},
      {"data", value_to_json(document.root)},
  };
  return DataTextResult::success(root.dump(2) + "\n");
}

DataTextResult encode_json_value(const DataValue& value) {
  return DataTextResult::success(value_to_json(value).dump(2) + "\n");
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

DataDocumentResult decode_json_value(std::string_view text, u32 schema_version) {
  try {
    const nlohmann::json root = nlohmann::json::parse(text);
    auto decoded = value_from_json(root, "root");
    if (!decoded) {
      return decoded;
    }
    decoded.value().schema_version = schema_version;
    return decoded;
  } catch (const nlohmann::json::exception& ex) {
    return DataDocumentResult::failure(error_at("root", ex.what()));
  }
}

} // namespace arv::io
