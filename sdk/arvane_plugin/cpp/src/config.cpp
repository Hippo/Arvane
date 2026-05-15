#include "arv/config.hpp"

namespace arv {
namespace {

[[nodiscard]] std::string_view strip_yaml_extension(std::string_view file_name) noexcept {
  constexpr std::string_view yaml_extension = ".yaml";
  constexpr std::string_view yml_extension = ".yml";
  if (file_name.ends_with(yaml_extension)) {
    file_name.remove_suffix(yaml_extension.size());
  } else if (file_name.ends_with(yml_extension)) {
    file_name.remove_suffix(yml_extension.size());
  }
  return file_name;
}

[[nodiscard]] bool config_file_path_valid(std::string_view path) noexcept {
  if (path.empty() || path.front() == '/' || path.back() == '/' ||
      path.find('\\') != std::string_view::npos ||
      path.find(':') != std::string_view::npos) {
    return false;
  }

  std::size_t start = 0U;
  while (start <= path.size()) {
    const std::size_t slash = path.find('/', start);
    const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
    const std::string_view segment = path.substr(start, end - start);
    if (segment.empty() || segment == "." || segment == "..") {
      return false;
    }
    for (const char ch : segment) {
      const bool safe = (ch >= 'a' && ch <= 'z') ||
                        (ch >= '0' && ch <= '9') ||
                        ch == '_' ||
                        ch == '-' ||
                        ch == '.';
      if (!safe) {
        return false;
      }
    }
    if (slash == std::string_view::npos) {
      break;
    }
    start = slash + 1U;
  }
  return true;
}

} // namespace

ConfigScope::ConfigScope(const ArvAPI* api, ArvConfigScope scope) noexcept
    : api_(api), scope_(scope) {
}

ValueResult<DataDocument> ConfigScope::load(std::string_view key) const {
  ArvDataDocumentHandle handle{};
  if (api_ == nullptr || api_->config == nullptr || api_->config->load == nullptr) {
    return {Result::unsupported("config API is unavailable"), {}};
  }
  Result result{api_->config->load(api_->config_ctx, &scope_, view(key), &handle)};
  return {result, result ? DataDocument{api_, handle} : DataDocument{}};
}

ValueResult<DataDocument> ConfigScope::load_file(std::string_view file_name) const {
  auto key = local_key_from_file(file_name);
  if (!key) {
    return {key.result, {}};
  }
  return load(key.value);
}

Result ConfigScope::save(std::string_view key, const DataDocument& document) const {
  if (api_ == nullptr || api_->config == nullptr || api_->config->save == nullptr) {
    return Result::unsupported("config API is unavailable");
  }
  return Result{api_->config->save(api_->config_ctx, &scope_, view(key), document.handle())};
}

Result ConfigScope::save_file(
    std::string_view file_name,
    const DataDocument& document) const {
  auto key = local_key_from_file(file_name);
  if (!key) {
    return key.result;
  }
  return save(key.value, document);
}

bool ConfigScope::exists(std::string_view key) const {
  return api_ != nullptr && api_->config != nullptr && api_->config->exists != nullptr &&
         api_->config->exists(api_->config_ctx, &scope_, view(key));
}

bool ConfigScope::exists_file(std::string_view file_name) const {
  auto key = local_key_from_file(file_name);
  return key && exists(key.value);
}

Result ConfigScope::remove(std::string_view key) const {
  if (api_ == nullptr || api_->config == nullptr || api_->config->remove == nullptr) {
    return Result::unsupported("config API is unavailable");
  }
  return Result{api_->config->remove(api_->config_ctx, &scope_, view(key))};
}

Result ConfigScope::remove_file(std::string_view file_name) const {
  auto key = local_key_from_file(file_name);
  if (!key) {
    return key.result;
  }
  return remove(key.value);
}

ValueResult<std::string> ConfigScope::local_key_from_file(
    std::string_view file_name) const {
  if (api_ == nullptr || api_->context == nullptr ||
      api_->context->pack_id == nullptr) {
    return {Result{ResultCode::InvalidState, "plugin context is unavailable"}, {}};
  }

  const std::string_view pack_id = view(api_->context->pack_id(api_->context_ctx));
  if (pack_id.empty()) {
    return {Result{ResultCode::InvalidState, "plugin pack id is unavailable"}, {}};
  }

  const std::string_view path = strip_yaml_extension(file_name);
  if (!config_file_path_valid(path)) {
    return {Result{ResultCode::InvalidArgument, "config file name is invalid"}, {}};
  }

  std::string key;
  key.reserve(pack_id.size() + 1U + path.size());
  key.append(pack_id);
  key.push_back(':');
  key.append(path);
  return {Result::ok(), std::move(key)};
}

Config::Config(const ArvAPI* api) noexcept : api_(api) {
}

ConfigScope Config::global() const noexcept {
  return ConfigScope{api_, {ARV_CONFIG_SCOPE_GLOBAL, 0}};
}

ConfigScope Config::world(ArvWorldHandle world) const noexcept {
  return ConfigScope{api_, {ARV_CONFIG_SCOPE_WORLD, world}};
}

} // namespace arv
