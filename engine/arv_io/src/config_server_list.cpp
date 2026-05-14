#include "config_internal.hpp"

#include <fstream>
#include <system_error>

namespace arv::io {
namespace {

ServerListResult decode_server_list_json(std::string_view text) {
  auto decoded = decode_json_value(text);
  if (!decoded) {
    return ServerListResult::failure(
        ConfigError{"failed to parse server list file: " + decoded.error().describe()});
  }

  const DataValue* servers_value = &decoded.value().root;
  if (decoded.value().root.kind() == DataKind::Object) {
    servers_value = decoded.value().root.get("servers");
    if (servers_value == nullptr) {
      return ServerListResult::success({});
    }
  }

  const DataValue::Array* server_array = servers_value->array_if();
  if (server_array == nullptr) {
    return ServerListResult::failure(
        ConfigError{"server list must be an array or object with a servers array"});
  }

  std::vector<SavedServerListEntry> entries;
  entries.reserve(server_array->size());
  for (const DataValue& entry : *server_array) {
    if (entry.kind() != DataKind::Object) {
      return ServerListResult::failure(
          ConfigError{"server list entries must contain string name and address fields"});
    }
    const DataValue* name = entry.get("name");
    const DataValue* address = entry.get("address");
    if (name == nullptr || address == nullptr ||
        name->kind() != DataKind::String ||
        address->kind() != DataKind::String) {
      return ServerListResult::failure(
          ConfigError{"server list entries must contain string name and address fields"});
    }
    entries.push_back(
        {std::string{name->as_string_or({})}, std::string{address->as_string_or({})}});
  }
  return ServerListResult::success(std::move(entries));
}

TextFileResult encode_server_list_json(std::span<const SavedServerListEntry> servers) {
  DataValue root = DataValue::object();
  root.set("version", detail::config_value(1U));
  DataValue server_array = DataValue::array();
  for (const SavedServerListEntry& server : servers) {
    DataValue entry = DataValue::object();
    entry.set("name", DataValue{server.name});
    entry.set("address", DataValue{server.address});
    server_array.push(std::move(entry));
  }
  root.set("servers", std::move(server_array));
  auto encoded = encode_json_value(root);
  if (!encoded) {
    return TextFileResult::failure(
        ConfigError{"failed to encode server list JSON: " + encoded.error().describe()});
  }
  return TextFileResult::success(std::move(encoded.value()));
}

} // namespace

ServerListResult load_server_list_file(const std::filesystem::path& path) {
  std::error_code file_error;
  if (!std::filesystem::exists(path, file_error)) {
    if (file_error) {
      return ServerListResult::failure(
          ConfigError{"failed to inspect server list file: " + file_error.message()});
    }
    return ServerListResult::success({});
  }

  auto text = read_text_file(path, "server list file");
  if (!text) {
    return ServerListResult::failure(text.error());
  }
  return decode_server_list_json(text.value());
}

ServerListSaveResult save_server_list_file(
    const std::filesystem::path& path,
    std::span<const SavedServerListEntry> servers) {
  std::error_code error;
  const std::filesystem::path parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if (error) {
      return ServerListSaveResult::failure(
          ConfigError{"failed to create server list directory: " + error.message()});
    }
  }

  auto encoded = encode_server_list_json(servers);
  if (!encoded) {
    return ServerListSaveResult::failure(encoded.error());
  }

  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    return ServerListSaveResult::failure(
        ConfigError{"failed to open server list file for write: " + path.string()});
  }
  output << encoded.value();
  output.flush();
  if (!output) {
    return ServerListSaveResult::failure(
        ConfigError{"failed to write server list file: " + path.string()});
  }
  return ServerListSaveResult::success();
}

} // namespace arv::io
