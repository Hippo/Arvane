#include "arv/io/data_codecs.hpp"

#include <charconv>
#include <limits>
#include <string>
#include <utility>

#include <yaml-cpp/yaml.h>

namespace arv::io {
namespace {

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

} // namespace

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

DataTextResult encode_yaml_value(const DataValue& value) {
  YAML::Emitter emitter;
  emitter << value_to_yaml(value);
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

DataDocumentResult decode_yaml_value(std::string_view text, u32 schema_version) {
  try {
    YAML::Node root = YAML::Load(std::string{text});
    auto decoded = value_from_yaml(root, "root");
    if (!decoded) {
      return decoded;
    }
    decoded.value().schema_version = schema_version;
    return decoded;
  } catch (const YAML::Exception& ex) {
    return DataDocumentResult::failure(error_at("root", ex.what()));
  }
}

} // namespace arv::io
