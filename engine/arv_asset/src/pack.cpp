#include "arv/asset/pack.hpp"

#include "arv/asset/resource.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/io/data_codecs.hpp"
#include "arv/io/files.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <queue>
#include <sstream>
#include <utility>

namespace arv::asset {
namespace {

constexpr u32 SupportedNativePluginApiVersion = 1U;

[[nodiscard]] PackError pack_error(
    std::string phase,
    std::string message,
    std::string pack_id = {},
    std::filesystem::path file = {},
    std::string resource_id = {},
    std::string pack_version = {}) {
  return {
      std::move(pack_id),
      std::move(pack_version),
      std::move(file),
      std::move(resource_id),
      std::move(phase),
      std::move(message)};
}

[[nodiscard]] bool valid_identifier_char(char c, bool first) noexcept {
  const bool lower = c >= 'a' && c <= 'z';
  const bool digit = c >= '0' && c <= '9';
  if (first) {
    return lower || digit || c == '_';
  }
  return lower || digit || c == '_' || c == '-' || c == '.';
}

[[nodiscard]] bool valid_resource_path_char(char c) noexcept {
  return (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') ||
         c == '_' ||
         c == '-' ||
         c == '.' ||
         c == '/';
}

[[nodiscard]] bool contains_ascii_space(std::string_view text) noexcept {
  for (const unsigned char ch : text) {
    if (std::isspace(ch) != 0) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool valid_native_entrypoint(std::string_view entrypoint) noexcept {
  if (entrypoint.empty()) {
    return false;
  }
  for (usize index = 0; index < entrypoint.size(); ++index) {
    const char c = entrypoint[index];
    const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    const bool digit = c >= '0' && c <= '9';
    const bool underscore = c == '_';
    if (index == 0U) {
      if (!alpha && !underscore) {
        return false;
      }
    } else if (!alpha && !digit && !underscore) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool path_has_unsafe_segment(std::string_view path) noexcept {
  if (path.empty() || path.front() == '/' || path.back() == '/' ||
      path.find('\\') != std::string_view::npos ||
      path.find(':') != std::string_view::npos) {
    return true;
  }
  usize start = 0;
  while (start <= path.size()) {
    const usize slash = path.find('/', start);
    const std::string_view segment =
        slash == std::string_view::npos ? path.substr(start) : path.substr(start, slash - start);
    if (segment.empty() || segment == "." || segment == "..") {
      return true;
    }
    if (slash == std::string_view::npos) {
      break;
    }
    start = slash + 1U;
  }
  return false;
}

[[nodiscard]] std::filesystem::path path_with_extension(
    std::filesystem::path path,
    std::string_view extension) {
  if (!extension.empty() && path.extension().empty()) {
    path += extension;
  }
  return path;
}

[[nodiscard]] std::filesystem::path logical_asset_relative_path(
    const ResourceID& id,
    AssetKind kind) {
  std::filesystem::path relative{"assets"};
  relative /= id.name_space;

  std::string_view logical = id.path;
  std::filesystem::path category;
  std::string_view extension;
  switch (kind) {
    case AssetKind::Texture:
      if (logical.starts_with("texture/")) {
        logical.remove_prefix(std::string_view{"texture/"}.size());
      }
      category = "textures";
      extension = ".png";
      break;
    case AssetKind::ShaderSource:
      if (logical.starts_with("shader/")) {
        logical.remove_prefix(std::string_view{"shader/"}.size());
      }
      category = "shaders";
      extension = "";
      break;
    case AssetKind::Font:
      if (logical.starts_with("font/")) {
        logical.remove_prefix(std::string_view{"font/"}.size());
      }
      category = "fonts";
      extension = ".ttf";
      break;
    case AssetKind::Model:
      if (logical.starts_with("model/")) {
        logical.remove_prefix(std::string_view{"model/"}.size());
      }
      category = "models";
      extension = ".json";
      break;
    case AssetKind::Animation:
      if (logical.starts_with("animation/")) {
        logical.remove_prefix(std::string_view{"animation/"}.size());
      }
      category = "animations";
      extension = ".json";
      break;
    case AssetKind::Theme:
      if (logical.starts_with("theme/")) {
        logical.remove_prefix(std::string_view{"theme/"}.size());
      }
      category = "themes";
      extension = ".yaml";
      break;
  }

  relative /= category;
  relative /= std::filesystem::path{std::string{logical}};
  return path_with_extension(relative, extension);
}

[[nodiscard]] std::filesystem::path logical_data_relative_path(
    const ResourceID& id,
    std::string_view extension) {
  std::filesystem::path relative{"data"};
  relative /= id.name_space;
  relative /= std::filesystem::path{id.path};
  return path_with_extension(relative, extension);
}

[[nodiscard]] std::filesystem::path logical_root_data_relative_path(
    const ResourceID& id,
    std::string_view extension) {
  std::filesystem::path relative{"data"};
  relative /= std::filesystem::path{id.path};
  return path_with_extension(relative, extension);
}

[[nodiscard]] std::filesystem::path logical_config_relative_path(
    const ResourceID& id,
    std::string_view extension) {
  std::filesystem::path relative{"config"};
  relative /= id.name_space;
  relative /= std::filesystem::path{id.path};
  return path_with_extension(relative, extension);
}

[[nodiscard]] std::filesystem::path logical_root_config_relative_path(
    const ResourceID& id,
    std::string_view extension) {
  std::filesystem::path relative{"config"};
  relative /= std::filesystem::path{id.path};
  return path_with_extension(relative, extension);
}

[[nodiscard]] bool contains_string(std::span<const std::string> values, std::string_view value) noexcept {
  return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] std::filesystem::path relative_to_pack(
    const PackSource& pack,
    const std::filesystem::path& path) {
  return path.lexically_relative(pack.root);
}

[[nodiscard]] bool canonical_path_is_under(
    const std::filesystem::path& root,
    const std::filesystem::path& path) {
  std::error_code error;
  const std::filesystem::path canonical_root =
      std::filesystem::weakly_canonical(root, error);
  if (error) {
    return false;
  }
  const std::filesystem::path canonical_path =
      std::filesystem::canonical(path, error);
  if (error) {
    return false;
  }

  auto root_it = canonical_root.begin();
  auto path_it = canonical_path.begin();
  for (; root_it != canonical_root.end(); ++root_it, ++path_it) {
    if (path_it == canonical_path.end() || *path_it != *root_it) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool file_exists_inside_root(
    const std::filesystem::path& root,
    const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error) || error) {
    return false;
  }
  return canonical_path_is_under(root, path);
}

[[nodiscard]] bool directory_exists_inside_root(
    const std::filesystem::path& root,
    const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::is_directory(path, error) || error) {
    return false;
  }
  return canonical_path_is_under(root, path);
}

[[nodiscard]] PackTextResult read_text_file(
    const std::filesystem::path& path,
    const PackSource& pack,
    std::string phase,
    std::string resource_id = {}) {
  if (!file_exists_inside_root(pack.root, path)) {
    return PackTextResult::failure(pack_error(
        phase,
        "file does not exist inside the pack root",
        pack.manifest.id,
        relative_to_pack(pack, path),
        std::move(resource_id),
        pack.manifest.version));
  }
  std::ifstream input(path);
  if (!input) {
    return PackTextResult::failure(pack_error(
        phase,
        "file could not be opened",
        pack.manifest.id,
        relative_to_pack(pack, path),
        std::move(resource_id),
        pack.manifest.version));
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!input && !input.eof()) {
    return PackTextResult::failure(pack_error(
        phase,
        "file read failed",
        pack.manifest.id,
        relative_to_pack(pack, path),
        std::move(resource_id),
        pack.manifest.version));
  }
  return PackTextResult::success(buffer.str());
}

[[nodiscard]] const io::DataValue* member(
    const io::DataValue& root,
    std::string_view field) noexcept {
  return root.get(field);
}

[[nodiscard]] bool is_array(const io::DataValue& value) noexcept {
  return value.kind() == io::DataKind::Array;
}

[[nodiscard]] std::string required_scalar(
    const io::DataValue& root,
    std::string_view field,
    PackError& error,
    const std::filesystem::path& file,
    std::string_view pack_id = {}) {
  const io::DataValue* node = member(root, field);
  if (node == nullptr || !io::is_string_value(*node)) {
    error = pack_error(
        "manifest validation",
        "missing required scalar field: " + std::string{field},
        std::string{pack_id},
        file);
    return {};
  }
  return std::string{node->as_string_or({})};
}

[[nodiscard]] std::vector<std::string> read_string_list(
    const io::DataValue& root,
    std::string_view field,
    PackError& error,
    const std::filesystem::path& file,
    std::string_view pack_id,
    bool required = false) {
  std::vector<std::string> values;
  const io::DataValue* node = member(root, field);
  if (node == nullptr) {
    if (required) {
      error = pack_error(
          "manifest validation",
          "missing required list field: " + std::string{field},
          std::string{pack_id},
          file);
    }
    return values;
  }
  const io::DataValue::Array* array = node->array_if();
  if (array == nullptr) {
    error = pack_error(
        "manifest validation",
        "field must be a list: " + std::string{field},
        std::string{pack_id},
        file);
    return values;
  }
  values.reserve(array->size());
  for (const io::DataValue& entry : *array) {
    if (!io::is_string_value(entry)) {
      error = pack_error(
          "manifest validation",
          "list field contains a non-scalar entry: " + std::string{field},
          std::string{pack_id},
          file);
      values.clear();
      return values;
    }
    values.emplace_back(entry.as_string_or({}));
  }
  return values;
}

[[nodiscard]] std::optional<bool> read_required_bool(
    const io::DataValue& root,
    std::string_view field,
    PackError& error,
    const std::filesystem::path& file,
    std::string_view pack_id) {
  const io::DataValue* node = member(root, field);
  if (node == nullptr || node->kind() != io::DataKind::Bool) {
    error = pack_error(
        "manifest validation",
        "missing required boolean field: " + std::string{field},
        std::string{pack_id},
        file);
    return std::nullopt;
  }
  return node->as_bool_or(false);
}

[[nodiscard]] std::optional<bool> read_optional_bool(
    const io::DataValue& root,
    std::string_view field,
    bool fallback,
    PackError& error,
    const std::filesystem::path& file,
    std::string_view pack_id) {
  const io::DataValue* node = member(root, field);
  if (node == nullptr) {
    return fallback;
  }
  if (node->kind() != io::DataKind::Bool) {
    error = pack_error(
        "manifest validation",
        "boolean field has invalid type: " + std::string{field},
        std::string{pack_id},
        file);
    return std::nullopt;
  }
  return node->as_bool_or(fallback);
}

[[nodiscard]] std::optional<PackCommandAlias> read_command_alias(
    const io::DataValue& entry,
    usize index,
    PackError& error,
    const std::filesystem::path& file,
    std::string_view pack_id) {
  if (!io::is_object_value(entry)) {
    error = pack_error(
        "manifest validation",
        "commands[" + std::to_string(index) + "] must be a map",
        std::string{pack_id},
        file);
    return std::nullopt;
  }

  const io::DataValue* alias = member(entry, "alias");
  if (alias == nullptr || !io::is_string_value(*alias)) {
    error = pack_error(
        "manifest validation",
        "commands[" + std::to_string(index) + "] requires scalar alias",
        std::string{pack_id},
        file);
    return std::nullopt;
  }
  const io::DataValue* command = member(entry, "command");
  if (command == nullptr || !io::is_string_value(*command)) {
    error = pack_error(
        "manifest validation",
        "commands[" + std::to_string(index) + "] requires scalar command",
        std::string{pack_id},
        file);
    return std::nullopt;
  }

  PackCommandAlias result;
  result.alias = std::string{alias->as_string_or({})};
  result.command = std::string{command->as_string_or({})};
  if (result.alias.empty() || contains_ascii_space(result.alias) ||
      result.command.empty() || contains_ascii_space(result.command)) {
    error = pack_error(
        "manifest validation",
        "commands[" + std::to_string(index) + "] alias and command must be non-empty without whitespace",
        std::string{pack_id},
        file);
    return std::nullopt;
  }
  if (const io::DataValue* visible = member(entry, "visible_in_help");
      visible != nullptr) {
    if (visible->kind() != io::DataKind::Bool) {
      error = pack_error(
          "manifest validation",
          "commands[" + std::to_string(index) + "].visible_in_help must be a boolean",
          std::string{pack_id},
          file);
      return std::nullopt;
    }
    result.visible_in_help = visible->as_bool_or(false);
  }
  return result;
}

[[nodiscard]] std::optional<PackPermissionNode> read_permission_node(
    const io::DataValue& entry,
    usize index,
    PackError& error,
    const std::filesystem::path& file,
    std::string_view pack_id) {
  if (!io::is_object_value(entry)) {
    error = pack_error(
        "manifest validation",
        "permissions[" + std::to_string(index) + "] must be a map",
        std::string{pack_id},
        file);
    return std::nullopt;
  }
  const io::DataValue* id = member(entry, "id");
  if (id == nullptr || !io::is_string_value(*id)) {
    error = pack_error(
        "manifest validation",
        "permissions[" + std::to_string(index) + "] requires scalar id",
        std::string{pack_id},
        file);
    return std::nullopt;
  }

  PackPermissionNode result;
  result.id = std::string{id->as_string_or({})};
  if (result.id.empty() || contains_ascii_space(result.id)) {
    error = pack_error(
        "manifest validation",
        "permissions[" + std::to_string(index) + "] id must be non-empty without whitespace",
        std::string{pack_id},
        file);
    return std::nullopt;
  }
  if (const io::DataValue* description = member(entry, "description");
      description != nullptr) {
    if (!io::is_string_value(*description)) {
      error = pack_error(
          "manifest validation",
          "permissions[" + std::to_string(index) + "].description must be scalar",
          std::string{pack_id},
          file);
      return std::nullopt;
    }
    result.description = std::string{description->as_string_or({})};
  }
  if (const io::DataValue* default_value = member(entry, "default");
      default_value != nullptr) {
    if (!io::is_string_value(*default_value)) {
      error = pack_error(
          "manifest validation",
          "permissions[" + std::to_string(index) + "].default must be scalar",
          std::string{pack_id},
          file);
      return std::nullopt;
    }
    result.default_value = std::string{default_value->as_string_or({})};
  }
  return result;
}

[[nodiscard]] bool dependency_version_satisfied(std::string_view actual, std::string_view requirement) {
  if (requirement.empty()) {
    return true;
  }
  if (requirement.starts_with(">=")) {
    auto parse_parts = [](std::string_view version) {
      std::vector<u32> parts;
      usize start = 0;
      while (start <= version.size()) {
        const usize dot = version.find('.', start);
        std::string_view part =
            dot == std::string_view::npos ? version.substr(start) : version.substr(start, dot - start);
        u32 value = 0;
        for (char ch : part) {
          if (ch < '0' || ch > '9') {
            break;
          }
          value = value * 10U + static_cast<u32>(ch - '0');
        }
        parts.push_back(value);
        if (dot == std::string_view::npos) {
          break;
        }
        start = dot + 1U;
      }
      while (parts.size() < 3U) {
        parts.push_back(0U);
      }
      return parts;
    };
    return parse_parts(actual) >= parse_parts(requirement.substr(2U));
  }
  return actual == requirement;
}

[[nodiscard]] bool hash_included_relative_path(const std::filesystem::path& relative) {
  const std::string first = relative.begin() == relative.end() ? std::string{} : relative.begin()->string();
  return first == "pack.yaml" || first == "assets" || first == "data" ||
         first == "native" || first == "server";
}

[[nodiscard]] Expected<std::string, PackError> compute_pack_content_hash(
    const std::filesystem::path& root) {
  std::vector<std::filesystem::path> files;
  std::error_code error;
  if (!std::filesystem::exists(root, error)) {
    return Expected<std::string, PackError>::success({});
  }
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::recursive_directory_iterator(
           root,
           std::filesystem::directory_options::skip_permission_denied,
           error)) {
    if (error) {
      error.clear();
      continue;
    }
    if (!file_exists_inside_root(root, entry.path())) {
      error.clear();
      continue;
    }
    const std::filesystem::path relative = entry.path().lexically_relative(root);
    if (hash_included_relative_path(relative)) {
      files.push_back(relative);
    }
  }
  std::sort(files.begin(), files.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.generic_string() < rhs.generic_string();
  });

  std::string combined;
  for (const std::filesystem::path& relative : files) {
    auto file_hash = crypto::hash_256_file(root / relative);
    if (!file_hash) {
      return Expected<std::string, PackError>::failure(pack_error(
          "pack hashing",
          file_hash.error().message,
          {},
          relative));
    }
    combined += relative.generic_string();
    combined += '\n';
    combined += crypto::hash_256_hex(file_hash.value());
    combined += '\n';
  }
  const auto* first = reinterpret_cast<const std::byte*>(combined.data());
  const crypto::Hash256 content_hash =
      crypto::hash_256(std::span<const std::byte>{first, combined.size()});
  return Expected<std::string, PackError>::success(
      crypto::hash_256_hex(content_hash));
}

[[nodiscard]] PackError validate_manifest(const PackManifest& manifest, const std::filesystem::path& file) {
  if (manifest.schema_version != 1U) {
    return pack_error(
        "manifest validation",
        "unsupported schema_version: " + std::to_string(manifest.schema_version),
        manifest.id,
        file,
        {},
        manifest.version);
  }
  if (!is_valid_pack_id(manifest.id)) {
    return pack_error(
        "manifest validation",
        "invalid pack id: " + manifest.id,
        manifest.id,
        file,
        {},
        manifest.version);
  }
  if (manifest.display_name.empty()) {
    return pack_error("manifest validation", "display_name must not be empty", manifest.id, file);
  }
  if (manifest.version.empty()) {
    return pack_error("manifest validation", "version must not be empty", manifest.id, file);
  }
  if (manifest.namespaces.empty()) {
    return pack_error("manifest validation", "namespaces must not be empty", manifest.id, file);
  }
  HashSet<std::string> namespaces;
  for (const std::string& name_space : manifest.namespaces) {
    if (!is_valid_pack_id(name_space)) {
      return pack_error(
          "manifest validation",
          "invalid namespace: " + name_space,
          manifest.id,
          file,
          {},
          manifest.version);
    }
    if (!namespaces.insert(name_space).second) {
      return pack_error(
          "manifest validation",
          "duplicate namespace: " + name_space,
          manifest.id,
          file,
          {},
          manifest.version);
    }
  }
  HashSet<std::string> dependencies;
  for (const PackDependency& dependency : manifest.dependencies) {
    if (!is_valid_pack_id(dependency.id)) {
      return pack_error(
          "manifest validation",
          "invalid dependency id: " + dependency.id,
          manifest.id,
          file,
          {},
          manifest.version);
    }
    if (!dependencies.insert(dependency.id).second) {
      return pack_error(
          "manifest validation",
          "duplicate dependency: " + dependency.id,
          manifest.id,
          file,
          {},
          manifest.version);
    }
  }
  if (manifest.native.has_value() && !manifest.capabilities.native_plugin) {
    return pack_error(
        "manifest validation",
        "native section is only valid when capabilities.native_plugin is true",
        manifest.id,
        file,
        {},
        manifest.version);
  }
  if (manifest.native.has_value()) {
    if (manifest.native->api_version == 0U) {
      return pack_error(
          "manifest validation",
          "native.api_version must be non-zero",
          manifest.id,
          file,
          {},
          manifest.version);
    }
    if (manifest.native->api_version != SupportedNativePluginApiVersion) {
      return pack_error(
          "manifest validation",
          "unsupported native.api_version: " +
              std::to_string(manifest.native->api_version),
          manifest.id,
          file,
          {},
          manifest.version);
    }
    if (!valid_native_entrypoint(manifest.native->entrypoint)) {
      return pack_error(
          "manifest validation",
          "native.entrypoint must be a non-empty C symbol name",
          manifest.id,
          file,
          {},
          manifest.version);
    }
    const std::string platform_key = native_plugin_platform_key();
    const auto library = manifest.native->libraries.find(platform_key);
    if (library == manifest.native->libraries.end()) {
      return pack_error(
          "manifest validation",
          "native.libraries is missing current platform key: " + platform_key,
          manifest.id,
          file,
          {},
          manifest.version);
    }
    if (!native_plugin_library_extension_matches_platform(library->second)) {
      return pack_error(
          "manifest validation",
          "native library extension does not match current platform: " +
              library->second.generic_string(),
          manifest.id,
          file,
          {},
          manifest.version);
    }
  }
  if (manifest.builtin.has_value()) {
    if (!is_valid_pack_id(manifest.builtin->module)) {
      return pack_error(
          "manifest validation",
          "invalid builtin module: " + manifest.builtin->module,
          manifest.id,
          file,
          {},
          manifest.version);
    }
  }
  return {};
}

[[nodiscard]] Expected<std::vector<PackSource>, PackError> discover_packs(
    const std::filesystem::path& packs_root) {
  std::vector<PackSource> packs;
  HashSet<std::string> seen_ids;
  std::error_code error;
  if (!std::filesystem::exists(packs_root, error)) {
    return Expected<std::vector<PackSource>, PackError>::failure(
        pack_error("pack discovery", "packs root does not exist", {}, packs_root));
  }
  if (!std::filesystem::is_directory(packs_root, error)) {
    return Expected<std::vector<PackSource>, PackError>::failure(
        pack_error("pack discovery", "packs root is not a directory", {}, packs_root));
  }
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(packs_root, error)) {
    if (error) {
      error.clear();
      continue;
    }
    if (!entry.is_directory()) {
      continue;
    }
    const std::filesystem::path manifest_path = entry.path() / "pack.yaml";
    if (!file_exists_inside_root(entry.path(), manifest_path)) {
      continue;
    }
    auto manifest = load_pack_manifest(entry.path());
    if (!manifest) {
      return Expected<std::vector<PackSource>, PackError>::failure(manifest.error());
    }
    if (!seen_ids.insert(manifest.value().id).second) {
      return Expected<std::vector<PackSource>, PackError>::failure(
          pack_error(
              "pack discovery",
              "duplicate installed pack id: " + manifest.value().id,
              manifest.value().id,
              manifest_path,
              {},
              manifest.value().version));
    }
    PackSource source;
    source.root = entry.path();
    source.manifest = std::move(manifest.value());
    auto content_hash = compute_pack_content_hash(source.root);
    if (!content_hash) {
      return Expected<std::vector<PackSource>, PackError>::failure(content_hash.error());
    }
    source.content_hash = std::move(content_hash.value());
    packs.push_back(std::move(source));
  }
  std::sort(packs.begin(), packs.end(), [](const PackSource& lhs, const PackSource& rhs) {
    return lhs.manifest.id < rhs.manifest.id;
  });
  return Expected<std::vector<PackSource>, PackError>::success(std::move(packs));
}

[[nodiscard]] Expected<std::vector<PackSource>, PackError> order_discovered_packs(
    std::vector<PackSource> discovered) {
  std::vector<PackSource> enabled = std::move(discovered);
  HashMap<std::string, usize> enabled_order;
  for (usize index = 0; index < enabled.size(); ++index) {
    enabled_order.emplace(enabled[index].manifest.id, index);
  }

  HashMap<std::string, usize> index_by_id;
  for (usize index = 0; index < enabled.size(); ++index) {
    index_by_id.emplace(enabled[index].manifest.id, index);
  }

  std::vector<std::vector<usize>> edges(enabled.size());
  std::vector<u32> indegree(enabled.size());
  const auto add_edge = [&](usize before, usize after) {
    if (before == after) {
      return;
    }
    auto& outgoing = edges[before];
    if (std::find(outgoing.begin(), outgoing.end(), after) == outgoing.end()) {
      outgoing.push_back(after);
      ++indegree[after];
    }
  };

  for (usize index = 0; index < enabled.size(); ++index) {
    const PackSource& pack = enabled[index];
    for (const PackDependency& dependency : pack.manifest.dependencies) {
      const auto dep_index = index_by_id.find(dependency.id);
      if (dep_index == index_by_id.end()) {
        return Expected<std::vector<PackSource>, PackError>::failure(
            pack_error(
                "pack ordering",
                "missing dependency '" + dependency.id + "' required by " + pack.manifest.id,
                pack.manifest.id,
                {},
                {},
                pack.manifest.version));
      }
      const PackSource& dependency_pack = enabled[dep_index->second];
      if (!dependency_version_satisfied(dependency_pack.manifest.version, dependency.version)) {
        return Expected<std::vector<PackSource>, PackError>::failure(
            pack_error(
                "pack ordering",
                "dependency version does not satisfy requirement: " + dependency.id +
                    " " + dependency.version + " actual " + dependency_pack.manifest.version,
                pack.manifest.id,
                {},
                {},
                pack.manifest.version));
      }
      add_edge(dep_index->second, index);
    }
    for (const std::string& after : pack.manifest.load_after) {
      if (const auto found = index_by_id.find(after); found != index_by_id.end()) {
        add_edge(found->second, index);
      }
    }
    for (const std::string& before : pack.manifest.load_before) {
      if (const auto found = index_by_id.find(before); found != index_by_id.end()) {
        add_edge(index, found->second);
      }
    }
  }

  auto compare_enabled_order = [&](usize lhs, usize rhs) {
    return enabled_order[enabled[lhs].manifest.id] > enabled_order[enabled[rhs].manifest.id];
  };
  std::priority_queue<usize, std::vector<usize>, decltype(compare_enabled_order)> ready(compare_enabled_order);
  for (usize index = 0; index < indegree.size(); ++index) {
    if (indegree[index] == 0U) {
      ready.push(index);
    }
  }

  std::vector<PackSource> ordered;
  ordered.reserve(enabled.size());
  while (!ready.empty()) {
    const usize index = ready.top();
    ready.pop();
    ordered.push_back(std::move(enabled[index]));
    std::sort(edges[index].begin(), edges[index].end(), [&](usize lhs, usize rhs) {
      return enabled_order[enabled[lhs].manifest.id] < enabled_order[enabled[rhs].manifest.id];
    });
    for (const usize after : edges[index]) {
      --indegree[after];
      if (indegree[after] == 0U) {
        ready.push(after);
      }
    }
  }

  if (ordered.size() != enabled.size()) {
    return Expected<std::vector<PackSource>, PackError>::failure(
        pack_error("pack ordering", "dependency or load-order cycle detected"));
  }

  return Expected<std::vector<PackSource>, PackError>::success(std::move(ordered));
}

[[nodiscard]] Expected<std::vector<io::DataValue>, PackError> entries_from_json_file(
    const PackDataFile& file,
    std::string_view root_key) {
  auto text = io::read_text_file_contents(file.path);
  if (!text) {
    return Expected<std::vector<io::DataValue>, PackError>::failure(pack_error(
        "data loading",
        "data file could not be opened",
        file.pack_id,
        file.relative_path,
        {},
        file.pack_version));
  }
  auto decoded = io::decode_json_value(text.value());
  if (!decoded) {
    return Expected<std::vector<io::DataValue>, PackError>::failure(pack_error(
        "data parsing",
        "failed to parse JSON: " + decoded.error().describe(),
        file.pack_id,
        file.relative_path,
        {},
        file.pack_version));
  }
  const io::DataValue* entries = &decoded.value().root;
  if (io::is_object_value(decoded.value().root)) {
    if (const io::DataValue* found = member(decoded.value().root, root_key);
        found != nullptr) {
      entries = found;
    } else if (member(decoded.value().root, "name") != nullptr ||
               member(decoded.value().root, "operation") != nullptr) {
      return Expected<std::vector<io::DataValue>, PackError>::success(
          {decoded.value().root});
    } else {
      return Expected<std::vector<io::DataValue>, PackError>::failure(pack_error(
          "data validation",
          "JSON object must contain " + std::string{root_key} + " array",
          file.pack_id,
          file.relative_path,
          {},
          file.pack_version));
    }
  }
  const io::DataValue::Array* array = entries->array_if();
  if (array == nullptr) {
    return Expected<std::vector<io::DataValue>, PackError>::failure(pack_error(
        "data validation",
        "JSON data entries must be an array",
        file.pack_id,
        file.relative_path,
        {},
        file.pack_version));
  }
  std::vector<io::DataValue> result;
  result.reserve(array->size());
  for (const io::DataValue& entry : *array) {
    result.push_back(entry);
  }
  return Expected<std::vector<io::DataValue>, PackError>::success(std::move(result));
}

[[nodiscard]] Expected<std::string, PackError> resource_name_from_definition(
    const io::DataValue& definition,
    const PackDataFile& file) {
  const io::DataValue* name = member(definition, "name");
  if (name == nullptr || !io::is_string_value(*name)) {
    return Expected<std::string, PackError>::failure(pack_error(
        "data validation",
        "definition is missing string field name",
        file.pack_id,
        file.relative_path,
        {},
        file.pack_version));
  }
  return Expected<std::string, PackError>::success(std::string{name->as_string_or({})});
}

} // namespace

std::string ResourceID::string() const {
  return name_space + ":" + path;
}

Expected<ResourceID, std::string> ResourceID::parse(std::string_view text) {
  const usize separator = text.find(':');
  if (separator == std::string_view::npos) {
    return Expected<ResourceID, std::string>::failure("resource ID is missing namespace separator ':'");
  }
  if (separator == 0U) {
    return Expected<ResourceID, std::string>::failure("resource ID namespace is empty");
  }
  if (separator + 1U >= text.size()) {
    return Expected<ResourceID, std::string>::failure("resource ID path is empty");
  }
  if (text.find(':', separator + 1U) != std::string_view::npos) {
    return Expected<ResourceID, std::string>::failure("resource ID contains more than one ':'");
  }

  const std::string_view name_space = text.substr(0U, separator);
  const std::string_view path = text.substr(separator + 1U);
  for (usize index = 0; index < name_space.size(); ++index) {
    if (!valid_identifier_char(name_space[index], index == 0U)) {
      return Expected<ResourceID, std::string>::failure("resource ID namespace contains invalid characters");
    }
  }
  if (path_has_unsafe_segment(path)) {
    return Expected<ResourceID, std::string>::failure("resource ID path contains an unsafe segment");
  }
  if (!std::all_of(path.begin(), path.end(), valid_resource_path_char)) {
    return Expected<ResourceID, std::string>::failure("resource ID path contains invalid characters");
  }

  return Expected<ResourceID, std::string>::success(
      {std::string{name_space}, std::string{path}});
}

bool ResourceID::valid(std::string_view text) noexcept {
  return parse(text).has_value();
}

std::string pack_side_to_string(PackSide side) {
  switch (side) {
    case PackSide::Client:
      return "client";
    case PackSide::Server:
      return "server";
    case PackSide::Both:
      return "both";
  }
  return "both";
}

std::optional<PackSide> pack_side_from_string(std::string_view value) {
  if (value == "client") {
    return PackSide::Client;
  }
  if (value == "server") {
    return PackSide::Server;
  }
  if (value == "both") {
    return PackSide::Both;
  }
  return std::nullopt;
}

bool is_valid_pack_id(std::string_view id) noexcept {
  if (id.empty()) {
    return false;
  }
  for (usize index = 0; index < id.size(); ++index) {
    if (!valid_identifier_char(id[index], index == 0U)) {
      return false;
    }
  }
  return true;
}

bool is_safe_pack_relative_path(const std::filesystem::path& path) noexcept {
  if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
    return false;
  }
  for (const std::filesystem::path& part : path) {
    const std::string value = part.string();
    if (value.empty() || value == "." || value == "..") {
      return false;
    }
  }
  return true;
}

std::string native_plugin_platform_key() {
#if defined(_WIN32)
  constexpr std::string_view os = "windows";
#elif defined(__APPLE__)
  constexpr std::string_view os = "macos";
#elif defined(__linux__)
  constexpr std::string_view os = "linux";
#else
  constexpr std::string_view os = "unknown";
#endif

#if defined(__x86_64__) || defined(_M_X64)
  constexpr std::string_view arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  constexpr std::string_view arch = "arm64";
#else
  constexpr std::string_view arch = "unknown";
#endif
  return std::string{os} + "-" + std::string{arch};
}

bool native_plugin_library_extension_matches_platform(
    const std::filesystem::path& path) {
#if defined(_WIN32)
  return path.extension() == ".dll";
#elif defined(__APPLE__)
  return path.extension() == ".dylib";
#else
  return path.extension() == ".so";
#endif
}

std::string PackError::describe() const {
  std::string text;
  if (!phase.empty()) {
    text += phase;
    text += ": ";
  }
  text += message;
  if (!pack_id.empty()) {
    text += " (pack ";
    text += pack_id;
    if (!pack_version.empty()) {
      text += " ";
      text += pack_version;
    }
    text += ")";
  }
  if (!file.empty()) {
    text += " [file ";
    text += file.generic_string();
    text += "]";
  }
  if (!resource_id.empty()) {
    text += " [resource ";
    text += resource_id;
    text += "]";
  }
  return text;
}

PackManifestResult load_pack_manifest(const std::filesystem::path& pack_root) {
  const std::filesystem::path manifest_path = pack_root / "pack.yaml";
  if (!file_exists_inside_root(pack_root, manifest_path)) {
    return PackManifestResult::failure(
        pack_error("manifest loading", "pack.yaml is missing", {}, manifest_path));
  }

  auto text = io::read_text_file_contents(manifest_path);
  if (!text) {
    return PackManifestResult::failure(
        pack_error("manifest loading", text.error().message, {}, manifest_path));
  }
  auto decoded = io::decode_yaml_value(text.value());
  if (!decoded) {
    return PackManifestResult::failure(
        pack_error("manifest parsing", "failed to parse pack.yaml: " + decoded.error().describe(), {}, manifest_path));
  }
  const io::DataValue& root = decoded.value().root;
  PackError error;

  PackManifest manifest;
  const io::DataValue* schema = member(root, "schema_version");
  if (schema == nullptr ||
      (schema->kind() != io::DataKind::I64 && schema->kind() != io::DataKind::U64)) {
    return PackManifestResult::failure(
        pack_error("manifest validation", "missing required scalar field: schema_version", {}, manifest_path));
  }
  manifest.schema_version = static_cast<u32>(schema->as_u64_or(0));
  manifest.id = required_scalar(root, "id", error, manifest_path);
  if (!error.message.empty()) {
    return PackManifestResult::failure(error);
  }
  manifest.display_name = required_scalar(root, "display_name", error, manifest_path, manifest.id);
  if (!error.message.empty()) {
    return PackManifestResult::failure(error);
  }
  manifest.version = required_scalar(root, "version", error, manifest_path, manifest.id);
  if (!error.message.empty()) {
    return PackManifestResult::failure(error);
  }
  if (const io::DataValue* description = member(root, "description");
      description != nullptr && io::is_string_value(*description)) {
    manifest.description = std::string{description->as_string_or({})};
  }
  manifest.authors = read_string_list(root, "authors", error, manifest_path, manifest.id);
  if (!error.message.empty()) {
    return PackManifestResult::failure(error);
  }
  manifest.namespaces = read_string_list(root, "namespaces", error, manifest_path, manifest.id, true);
  if (!error.message.empty()) {
    return PackManifestResult::failure(error);
  }
  const std::string side = required_scalar(root, "side", error, manifest_path, manifest.id);
  if (!error.message.empty()) {
    return PackManifestResult::failure(error);
  }
  const std::optional<PackSide> parsed_side = pack_side_from_string(side);
  if (!parsed_side.has_value()) {
    return PackManifestResult::failure(
        pack_error("manifest validation", "invalid side: " + side, manifest.id, manifest_path));
  }
  manifest.side = parsed_side.value();

  if (const io::DataValue* engine = member(root, "engine");
      engine != nullptr && io::is_object_value(*engine)) {
    if (const io::DataValue* min = member(*engine, "min_version");
        min != nullptr && io::is_string_value(*min)) {
      manifest.engine_min_version = std::string{min->as_string_or({})};
    }
    if (const io::DataValue* max = member(*engine, "max_version");
        max != nullptr && io::is_string_value(*max)) {
      manifest.engine_max_version = std::string{max->as_string_or({})};
    }
  }

  if (const io::DataValue* dependencies = member(root, "dependencies");
      dependencies != nullptr) {
    const io::DataValue::Array* array = dependencies->array_if();
    if (array == nullptr) {
      return PackManifestResult::failure(
          pack_error("manifest validation", "dependencies must be a list", manifest.id, manifest_path));
    }
    manifest.dependencies.reserve(array->size());
    for (const io::DataValue& entry : *array) {
      const io::DataValue* id = member(entry, "id");
      if (!io::is_object_value(entry) || id == nullptr || !io::is_string_value(*id)) {
        return PackManifestResult::failure(
            pack_error("manifest validation", "dependency entries require scalar id", manifest.id, manifest_path));
      }
      PackDependency dependency;
      dependency.id = std::string{id->as_string_or({})};
      if (const io::DataValue* version = member(entry, "version");
          version != nullptr && io::is_string_value(*version)) {
        dependency.version = std::string{version->as_string_or({})};
      }
      manifest.dependencies.push_back(std::move(dependency));
    }
  }

  manifest.load_after = read_string_list(root, "load_after", error, manifest_path, manifest.id);
  if (!error.message.empty()) {
    return PackManifestResult::failure(error);
  }
  manifest.load_before = read_string_list(root, "load_before", error, manifest_path, manifest.id);
  if (!error.message.empty()) {
    return PackManifestResult::failure(error);
  }

  const io::DataValue* capabilities = member(root, "capabilities");
  if (capabilities == nullptr || !io::is_object_value(*capabilities)) {
    return PackManifestResult::failure(
        pack_error("manifest validation", "missing required capabilities map", manifest.id, manifest_path));
  }
  auto assets = read_required_bool(*capabilities, "assets", error, manifest_path, manifest.id);
  if (!assets) {
    return PackManifestResult::failure(error);
  }
  auto data = read_required_bool(*capabilities, "data", error, manifest_path, manifest.id);
  if (!data) {
    return PackManifestResult::failure(error);
  }
  auto config = read_optional_bool(
      *capabilities,
      "config",
      false,
      error,
      manifest_path,
      manifest.id);
  if (!config) {
    return PackManifestResult::failure(error);
  }
  auto native_plugin = read_required_bool(*capabilities, "native_plugin", error, manifest_path, manifest.id);
  if (!native_plugin) {
    return PackManifestResult::failure(error);
  }
  manifest.capabilities = {
      assets.value(),
      data.value(),
      config.value(),
      native_plugin.value()};

  if (const io::DataValue* commands = member(root, "commands");
      commands != nullptr) {
    const io::DataValue::Array* array = commands->array_if();
    if (array == nullptr) {
      return PackManifestResult::failure(
          pack_error("manifest validation", "commands must be a list", manifest.id, manifest_path));
    }
    manifest.commands.reserve(array->size());
    for (usize index = 0U; index < array->size(); ++index) {
      PackError command_error;
      auto command = read_command_alias(
          (*array)[index],
          index,
          command_error,
          manifest_path,
          manifest.id);
      if (!command.has_value()) {
        return PackManifestResult::failure(std::move(command_error));
      }
      manifest.commands.push_back(std::move(command.value()));
    }
  }

  if (const io::DataValue* permissions = member(root, "permissions");
      permissions != nullptr) {
    const io::DataValue::Array* array = permissions->array_if();
    if (array == nullptr) {
      return PackManifestResult::failure(
          pack_error("manifest validation", "permissions must be a list", manifest.id, manifest_path));
    }
    manifest.permissions.reserve(array->size());
    for (usize index = 0U; index < array->size(); ++index) {
      PackError permission_error;
      auto permission = read_permission_node(
          (*array)[index],
          index,
          permission_error,
          manifest_path,
          manifest.id);
      if (!permission.has_value()) {
        return PackManifestResult::failure(std::move(permission_error));
      }
      manifest.permissions.push_back(std::move(permission.value()));
    }
  }

  if (const io::DataValue* builtin = member(root, "builtin"); builtin != nullptr) {
    if (!io::is_object_value(*builtin)) {
      return PackManifestResult::failure(
          pack_error("manifest validation", "builtin must be a map", manifest.id, manifest_path));
    }
    const io::DataValue* module = member(*builtin, "module");
    if (module == nullptr || !io::is_string_value(*module)) {
      return PackManifestResult::failure(
          pack_error("manifest validation", "builtin.module is required", manifest.id, manifest_path));
    }
    manifest.builtin = PackBuiltinManifest{std::string{module->as_string_or({})}};
  }

  if (const io::DataValue* native = member(root, "native"); native != nullptr) {
    if (!io::is_object_value(*native)) {
      return PackManifestResult::failure(
          pack_error("manifest validation", "native must be a map", manifest.id, manifest_path));
    }
    PackNativeManifest native_manifest;
    const io::DataValue* api_version = member(*native, "api_version");
    if (api_version == nullptr ||
        (api_version->kind() != io::DataKind::I64 && api_version->kind() != io::DataKind::U64)) {
      return PackManifestResult::failure(
          pack_error("manifest validation", "native.api_version is required", manifest.id, manifest_path));
    }
    native_manifest.api_version = static_cast<u32>(api_version->as_u64_or(0));
    const io::DataValue* entrypoint = member(*native, "entrypoint");
    if (entrypoint == nullptr || !io::is_string_value(*entrypoint)) {
      return PackManifestResult::failure(
          pack_error("manifest validation", "native.entrypoint is required", manifest.id, manifest_path));
    }
    native_manifest.entrypoint = std::string{entrypoint->as_string_or({})};
    const io::DataValue* libraries = member(*native, "libraries");
    const io::DataValue::Object* library_object =
        libraries == nullptr ? nullptr : libraries->object_if();
    if (library_object == nullptr) {
      return PackManifestResult::failure(
          pack_error("manifest validation", "native.libraries map is required", manifest.id, manifest_path));
    }
    for (const auto& [platform, value] : *library_object) {
      if (!io::is_string_value(value)) {
        return PackManifestResult::failure(
            pack_error("manifest validation", "native library entries must be scalar", manifest.id, manifest_path));
      }
      std::filesystem::path library_path = std::string{value.as_string_or({})};
      if (!is_safe_pack_relative_path(library_path)) {
        return PackManifestResult::failure(
            pack_error(
                "manifest validation",
                "unsafe native library path: " + library_path.generic_string(),
                manifest.id,
                manifest_path,
                {},
                manifest.version));
      }
      native_manifest.libraries.emplace(platform, std::move(library_path));
    }
    manifest.native = std::move(native_manifest);
  }

  PackError validation = validate_manifest(manifest, manifest_path);
  if (!validation.message.empty()) {
    return PackManifestResult::failure(std::move(validation));
  }
  return PackManifestResult::success(std::move(manifest));
}

Expected<PackManager, PackError> PackManager::load(const std::filesystem::path& arvane_home) {
  return load_from_packs_root(arvane_home / "packs");
}

Expected<PackManager, PackError> PackManager::load_from_packs_root(
    const std::filesystem::path& packs_root) {
  auto discovered = discover_packs(packs_root);
  if (!discovered) {
    return Expected<PackManager, PackError>::failure(discovered.error());
  }
  auto ordered = order_discovered_packs(std::move(discovered.value()));
  if (!ordered) {
    return Expected<PackManager, PackError>::failure(ordered.error());
  }

  PackManager manager;
  manager.packs_root_ = packs_root;
  manager.stack_ = std::move(ordered.value());
  return Expected<PackManager, PackError>::success(std::move(manager));
}

Expected<PackManager, PackError> PackManager::load_client_bundle_roots(
    std::span<const std::filesystem::path> bundle_roots) {
  if (bundle_roots.empty()) {
    return Expected<PackManager, PackError>::failure(
        pack_error("client bundle loading", "no client bundle cache roots were provided"));
  }

  PackManager manager;
  manager.packs_root_ = bundle_roots.front().parent_path();
  manager.stack_.reserve(bundle_roots.size());

  for (usize index = 0; index < bundle_roots.size(); ++index) {
    const std::filesystem::path& root = bundle_roots[index];
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
      return Expected<PackManager, PackError>::failure(
          pack_error("client bundle loading", "client bundle cache root is not a directory", {}, root));
    }
    if (!file_exists_inside_root(root, root / "manifest.json")) {
      return Expected<PackManager, PackError>::failure(
          pack_error("client bundle loading", "client bundle manifest.json is missing", {}, root));
    }
    if (!file_exists_inside_root(root, root / "registry-client.json")) {
      return Expected<PackManager, PackError>::failure(
          pack_error("client bundle loading", "client bundle registry-client.json is missing", {}, root));
    }

    HashSet<std::string> namespace_set;
    const auto collect_namespaces = [&](const std::filesystem::path& base) -> std::optional<PackError> {
      if (!std::filesystem::is_directory(base, error)) {
        error.clear();
        return std::nullopt;
      }
      for (const std::filesystem::directory_entry& entry :
           std::filesystem::directory_iterator(base, error)) {
        if (error) {
          error.clear();
          continue;
        }
        if (!entry.is_directory(error)) {
          error.clear();
          continue;
        }
        const std::string name_space = entry.path().filename().string();
        if (!is_valid_pack_id(name_space)) {
          return pack_error(
              "client bundle loading",
              "invalid client bundle namespace: " + name_space,
              {},
              entry.path());
        }
        namespace_set.insert(name_space);
      }
      return std::nullopt;
    };
    if (std::optional<PackError> ns_error = collect_namespaces(root / "data");
        ns_error.has_value()) {
      return Expected<PackManager, PackError>::failure(std::move(ns_error.value()));
    }
    if (std::optional<PackError> ns_error = collect_namespaces(root / "assets");
        ns_error.has_value()) {
      return Expected<PackManager, PackError>::failure(std::move(ns_error.value()));
    }
    if (namespace_set.empty()) {
      return Expected<PackManager, PackError>::failure(
          pack_error("client bundle loading", "client bundle has no data or asset namespaces", {}, root));
    }

    PackSource source;
    source.root = root;
    source.manifest.schema_version = 1U;
    source.manifest.id = "server_bundle_" + std::to_string(index);
    source.manifest.display_name = "Server Bundle " + std::to_string(index + 1U);
    source.manifest.version = "0";
    source.manifest.side = PackSide::Client;
    source.manifest.capabilities = {
        .assets = true,
        .data = true,
        .config = false,
        .native_plugin = false};
    source.manifest.namespaces.assign(namespace_set.begin(), namespace_set.end());
    std::sort(source.manifest.namespaces.begin(), source.manifest.namespaces.end());
    source.content_hash = root.filename().string();
    manager.stack_.push_back(std::move(source));
  }

  return Expected<PackManager, PackError>::success(std::move(manager));
}

const PackSource* PackManager::find_pack(std::string_view id) const noexcept {
  const auto found = std::find_if(stack_.begin(), stack_.end(), [&](const PackSource& pack) {
    return pack.manifest.id == id;
  });
  return found == stack_.end() ? nullptr : &(*found);
}

PackPathResult PackManager::resolve_asset_path(
    std::string_view asset_id,
    AssetKind asset_kind) const {
  auto parsed = ResourceID::parse(asset_id);
  if (!parsed) {
    return PackPathResult::failure(
        pack_error("asset resolution", parsed.error(), {}, {}, std::string{asset_id}));
  }
  const ResourceID id = std::move(parsed.value());
  const std::filesystem::path relative = logical_asset_relative_path(id, asset_kind);
  if (!is_safe_pack_relative_path(relative)) {
    return PackPathResult::failure(
        pack_error("asset resolution", "asset path is unsafe", {}, relative, id.string()));
  }

  for (usize reverse_index = stack_.size(); reverse_index > 0U; --reverse_index) {
    const usize index = reverse_index - 1U;
    const PackSource& pack = stack_[index];
    if (!pack.manifest.capabilities.assets ||
        !contains_string(pack.manifest.namespaces, id.name_space)) {
      continue;
    }
    const std::filesystem::path path = pack.root / relative;
    if (file_exists_inside_root(pack.root, path)) {
      return PackPathResult::success(
          {index, pack.manifest.id, pack.manifest.version, relative, path});
    }
  }

  return PackPathResult::failure(
      pack_error("asset resolution", "asset was not found in installed pack stack", {}, relative, id.string()));
}

PackPathResult PackManager::resolve_data_path(
    std::string_view data_id,
    std::string_view extension) const {
  auto parsed = ResourceID::parse(data_id);
  if (!parsed) {
    return PackPathResult::failure(
        pack_error("data resolution", parsed.error(), {}, {}, std::string{data_id}));
  }
  const ResourceID id = std::move(parsed.value());
  const std::filesystem::path relative = logical_data_relative_path(id, extension);
  const std::filesystem::path root_relative = logical_root_data_relative_path(id, extension);
  if (!is_safe_pack_relative_path(relative)) {
    return PackPathResult::failure(
        pack_error("data resolution", "data path is unsafe", {}, relative, id.string()));
  }
  if (!is_safe_pack_relative_path(root_relative)) {
    return PackPathResult::failure(
        pack_error("data resolution", "data path is unsafe", {}, root_relative, id.string()));
  }

  for (usize reverse_index = stack_.size(); reverse_index > 0U; --reverse_index) {
    const usize index = reverse_index - 1U;
    const PackSource& pack = stack_[index];
    if (!pack.manifest.capabilities.data ||
        !contains_string(pack.manifest.namespaces, id.name_space)) {
      continue;
    }
    const std::filesystem::path path = pack.root / relative;
    if (file_exists_inside_root(pack.root, path)) {
      return PackPathResult::success(
          {index, pack.manifest.id, pack.manifest.version, relative, path});
    }
    const std::filesystem::path root_path = pack.root / root_relative;
    if (file_exists_inside_root(pack.root, root_path)) {
      return PackPathResult::success(
          {index, pack.manifest.id, pack.manifest.version, root_relative, root_path});
    }
  }

  return PackPathResult::failure(
      pack_error("data resolution", "data file was not found in installed pack stack", {}, relative, id.string()));
}

PackPathResult PackManager::resolve_config_path(
    std::string_view config_id,
    std::string_view extension) const {
  auto parsed = ResourceID::parse(config_id);
  if (!parsed) {
    return PackPathResult::failure(
        pack_error("config resolution", parsed.error(), {}, {}, std::string{config_id}));
  }
  const ResourceID id = std::move(parsed.value());
  const std::filesystem::path relative = logical_config_relative_path(id, extension);
  const std::filesystem::path root_relative =
      logical_root_config_relative_path(id, extension);
  if (!is_safe_pack_relative_path(relative)) {
    return PackPathResult::failure(
        pack_error("config resolution", "config path is unsafe", {}, relative, id.string()));
  }
  if (!is_safe_pack_relative_path(root_relative)) {
    return PackPathResult::failure(
        pack_error("config resolution", "config path is unsafe", {}, root_relative, id.string()));
  }

  for (usize reverse_index = stack_.size(); reverse_index > 0U; --reverse_index) {
    const usize index = reverse_index - 1U;
    const PackSource& pack = stack_[index];
    if (!pack.manifest.capabilities.config ||
        !contains_string(pack.manifest.namespaces, id.name_space)) {
      continue;
    }
    const std::filesystem::path path = pack.root / relative;
    if (file_exists_inside_root(pack.root, path)) {
      return PackPathResult::success(
          {index, pack.manifest.id, pack.manifest.version, relative, path});
    }
    const std::filesystem::path root_path = pack.root / root_relative;
    if (file_exists_inside_root(pack.root, root_path)) {
      return PackPathResult::success(
          {index, pack.manifest.id, pack.manifest.version, root_relative, root_path});
    }
  }

  return PackPathResult::failure(
      pack_error("config resolution", "config file was not found in installed pack stack", {}, relative, id.string()));
}

std::vector<PackDataFile> PackManager::data_files(
    std::string_view category,
    std::string_view extension) const {
  std::vector<PackDataFile> files;
  std::error_code error;
  for (usize pack_index = 0; pack_index < stack_.size(); ++pack_index) {
    const PackSource& pack = stack_[pack_index];
    if (!pack.manifest.capabilities.data) {
      continue;
    }
    const std::filesystem::path root_data = pack.root / "data";
    const std::filesystem::path root_category_file =
        path_with_extension(root_data / std::string{category}, extension);
    if (file_exists_inside_root(pack.root, root_category_file)) {
      files.push_back({pack_index,
                       pack.manifest.id,
                       pack.manifest.version,
                       {},
                       relative_to_pack(pack, root_category_file),
                       root_category_file});
    }
    const std::filesystem::path root_directory = root_data / std::string{category};
    if (directory_exists_inside_root(pack.root, root_directory)) {
      std::vector<PackDataFile> root_directory_files;
      for (const std::filesystem::directory_entry& entry :
           std::filesystem::recursive_directory_iterator(
               root_directory,
               std::filesystem::directory_options::skip_permission_denied,
               error)) {
        if (error) {
          error.clear();
          continue;
        }
        if (!file_exists_inside_root(pack.root, entry.path())) {
          error.clear();
          continue;
        }
        if (!extension.empty() && entry.path().extension() != extension) {
          continue;
        }
        root_directory_files.push_back({pack_index,
                                        pack.manifest.id,
                                        pack.manifest.version,
                                        {},
                                        relative_to_pack(pack, entry.path()),
                                        entry.path()});
      }
      std::sort(root_directory_files.begin(), root_directory_files.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.relative_path.generic_string() < rhs.relative_path.generic_string();
      });
      files.insert(files.end(), root_directory_files.begin(), root_directory_files.end());
    }
    for (const std::string& name_space : pack.manifest.namespaces) {
      const std::filesystem::path namespace_root = pack.root / "data" / name_space;
      const std::filesystem::path category_file =
          path_with_extension(namespace_root / std::string{category}, extension);
      if (file_exists_inside_root(pack.root, category_file)) {
        files.push_back({pack_index,
                         pack.manifest.id,
                         pack.manifest.version,
                         name_space,
                         relative_to_pack(pack, category_file),
                         category_file});
      }
      const std::filesystem::path directory = namespace_root / std::string{category};
      if (!directory_exists_inside_root(pack.root, directory)) {
        error.clear();
        continue;
      }
      std::vector<PackDataFile> directory_files;
      for (const std::filesystem::directory_entry& entry :
           std::filesystem::recursive_directory_iterator(
               directory,
               std::filesystem::directory_options::skip_permission_denied,
               error)) {
        if (error) {
          error.clear();
          continue;
        }
        if (!file_exists_inside_root(pack.root, entry.path())) {
          error.clear();
          continue;
        }
        if (!extension.empty() && entry.path().extension() != extension) {
          continue;
        }
        directory_files.push_back({pack_index,
                                   pack.manifest.id,
                                   pack.manifest.version,
                                   name_space,
                                   relative_to_pack(pack, entry.path()),
                                   entry.path()});
      }
      std::sort(directory_files.begin(), directory_files.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.relative_path.generic_string() < rhs.relative_path.generic_string();
      });
      files.insert(files.end(), directory_files.begin(), directory_files.end());
    }
  }
  return files;
}

std::vector<PackDataFile> PackManager::data_files_at(std::string_view relative_path) const {
  std::vector<PackDataFile> files;
  const std::filesystem::path relative_logical{std::string{relative_path}};
  if (!is_safe_pack_relative_path(relative_logical)) {
    return files;
  }
  for (usize pack_index = 0; pack_index < stack_.size(); ++pack_index) {
    const PackSource& pack = stack_[pack_index];
    if (!pack.manifest.capabilities.data) {
      continue;
    }
    const std::filesystem::path root_path = pack.root / "data" / relative_logical;
    if (file_exists_inside_root(pack.root, root_path)) {
      files.push_back({pack_index,
                       pack.manifest.id,
                       pack.manifest.version,
                       {},
                       relative_to_pack(pack, root_path),
                       root_path});
    }
    for (const std::string& name_space : pack.manifest.namespaces) {
      const std::filesystem::path path = pack.root / "data" / name_space / relative_logical;
      if (!file_exists_inside_root(pack.root, path)) {
        continue;
      }
      files.push_back({pack_index,
                       pack.manifest.id,
                       pack.manifest.version,
                       name_space,
                       relative_to_pack(pack, path),
                       path});
    }
  }
  return files;
}

PackTextResult PackManager::load_data_text(
    std::string_view data_id,
    std::string_view extension) const {
  auto resolved = resolve_data_path(data_id, extension);
  if (!resolved) {
    return PackTextResult::failure(resolved.error());
  }
  const PackSource& pack = stack_[resolved.value().pack_index];
  return read_text_file(
      resolved.value().path,
      pack,
      "data loading",
      std::string{data_id});
}

PackTextResult PackManager::load_config_text(
    std::string_view config_id,
    std::string_view extension) const {
  auto resolved = resolve_config_path(config_id, extension);
  if (!resolved) {
    return PackTextResult::failure(resolved.error());
  }
  const PackSource& pack = stack_[resolved.value().pack_index];
  return read_text_file(
      resolved.value().path,
      pack,
      "config loading",
      std::string{config_id});
}

PackTextResult PackManager::load_merged_json_array_text(
    std::string_view category,
    std::string_view root_key) const {
  std::vector<io::DataValue> definitions;
  HashMap<std::string, usize> definition_by_name;
  std::vector<PackDataFile> files = data_files(category, ".json");
  if (files.empty()) {
    return PackTextResult::failure(
        pack_error("data loading", "no data files found for category: " + std::string{category}));
  }

  for (const PackDataFile& file : files) {
    auto entries = entries_from_json_file(file, root_key);
    if (!entries) {
      return PackTextResult::failure(entries.error());
    }

    const PackSource& source = stack_[file.pack_index];
    for (io::DataValue entry : entries.value()) {
      if (!io::is_object_value(entry)) {
        return PackTextResult::failure(pack_error(
            "data validation",
            "definition entry must be an object",
            file.pack_id,
            file.relative_path,
            {},
            file.pack_version));
      }

      const io::DataValue* operation = member(entry, "operation");
      if (operation != nullptr) {
        if (!io::is_string_value(*operation)) {
          return PackTextResult::failure(pack_error(
              "data validation",
              "operation must be a string",
              file.pack_id,
              file.relative_path,
              {},
              file.pack_version));
        }
        const std::string op{operation->as_string_or({})};
        if (op != "override") {
          return PackTextResult::failure(pack_error(
              "data validation",
              "unsupported data operation: " + op,
              file.pack_id,
              file.relative_path,
              {},
              file.pack_version));
        }
        const io::DataValue* target_iter = member(entry, "target");
        if (target_iter == nullptr || !io::is_string_value(*target_iter)) {
          return PackTextResult::failure(pack_error(
              "data validation",
              "override operation requires string target",
              file.pack_id,
              file.relative_path,
              {},
              file.pack_version));
        }
        const std::string target{target_iter->as_string_or({})};
        auto target_id = ResourceID::parse(target);
        if (!target_id) {
          return PackTextResult::failure(pack_error(
              "data validation",
              target_id.error(),
              file.pack_id,
              file.relative_path,
              target,
              file.pack_version));
        }
        const auto existing = definition_by_name.find(target);
        if (existing == definition_by_name.end()) {
          return PackTextResult::failure(pack_error(
              "data validation",
              "override target does not exist in a lower-priority pack",
              file.pack_id,
              file.relative_path,
              target,
              file.pack_version));
        }

        io::DataValue replacement;
        const io::DataValue* definition_iter = member(entry, "definition");
        if (definition_iter != nullptr) {
          replacement = *definition_iter;
        } else {
          replacement = entry;
          if (io::DataValue::Object* object = replacement.object_if();
              object != nullptr) {
            object->erase("operation");
            object->erase("target");
          }
        }
        if (!io::is_object_value(replacement)) {
          return PackTextResult::failure(pack_error(
              "data validation",
              "override definition must be an object",
              file.pack_id,
              file.relative_path,
              target,
              file.pack_version));
        }
        if (member(replacement, "name") == nullptr) {
          replacement.set("name", target);
        }
        auto replacement_name = resource_name_from_definition(replacement, file);
        if (!replacement_name) {
          return PackTextResult::failure(replacement_name.error());
        }
        if (replacement_name.value() != target) {
          return PackTextResult::failure(pack_error(
              "data validation",
              "override definition name must match target",
              file.pack_id,
              file.relative_path,
              target,
              file.pack_version));
        }
        definitions[existing->second] = std::move(replacement);
        continue;
      }

      auto name = resource_name_from_definition(entry, file);
      if (!name) {
        return PackTextResult::failure(name.error());
      }
      auto parsed = ResourceID::parse(name.value());
      if (!parsed) {
        return PackTextResult::failure(pack_error(
            "data validation",
            parsed.error(),
            file.pack_id,
            file.relative_path,
            name.value(),
            file.pack_version));
      }
      if (!contains_string(source.manifest.namespaces, parsed.value().name_space)) {
        return PackTextResult::failure(pack_error(
            "data validation",
            "definition namespace is not declared by this pack",
            file.pack_id,
            file.relative_path,
            name.value(),
            file.pack_version));
      }
      if (definition_by_name.find(name.value()) != definition_by_name.end()) {
        return PackTextResult::failure(pack_error(
            "data validation",
            "duplicate definition without explicit override",
            file.pack_id,
            file.relative_path,
            name.value(),
            file.pack_version));
      }
      definition_by_name.emplace(name.value(), definitions.size());
      definitions.push_back(std::move(entry));
    }
  }

  io::DataValue::Array array;
  array.reserve(definitions.size());
  for (io::DataValue& definition : definitions) {
    array.push_back(std::move(definition));
  }
  io::DataValue root = io::DataValue::object();
  root.set(std::string{root_key}, io::DataValue{std::move(array)});
  auto encoded = io::encode_json_value(root);
  if (!encoded) {
    return PackTextResult::failure(
        pack_error("data encoding", encoded.error().describe()));
  }
  return PackTextResult::success(std::move(encoded.value()));
}

} // namespace arv::asset
