#include "arv/config.hpp"

namespace arv {
namespace {

[[nodiscard]] std::string_view strip_extension(
    std::string_view file_name,
    std::string_view extension) noexcept {
  if (file_name.ends_with(extension)) {
    file_name.remove_suffix(extension.size());
  }
  return file_name;
}

[[nodiscard]] std::string_view strip_config_extension(
    std::string_view file_name) noexcept {
  file_name = strip_extension(file_name, ".yaml");
  file_name = strip_extension(file_name, ".yml");
  return file_name;
}

[[nodiscard]] std::string_view strip_data_extension(
    std::string_view file_name) noexcept {
  return strip_extension(file_name, ".json");
}

[[nodiscard]] bool plugin_file_path_valid(std::string_view path) noexcept {
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
                        (ch >= 'A' && ch <= 'Z') ||
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

Config::Config(const ArvAPI* api) noexcept : api_(api) {
}

ValueResult<DataDocument> Config::load(std::string_view key) const {
  ArvDataDocumentHandle handle{};
  if (api_ == nullptr || api_->config == nullptr ||
      api_->config->load_config == nullptr) {
    return {Result::unsupported("config API is unavailable"), {}};
  }
  Result result{api_->config->load_config(api_->config_ctx, view(key), &handle)};
  return {result, result ? DataDocument{api_, handle} : DataDocument{}};
}

ValueResult<DataDocument> Config::load_file(std::string_view file_name) const {
  auto key = local_key_from_config_file(file_name);
  if (!key) {
    return {key.result, {}};
  }
  return load(key.value);
}

Result Config::save(std::string_view key, const DataDocument& document) const {
  if (api_ == nullptr || api_->config == nullptr ||
      api_->config->save_config == nullptr) {
    return Result::unsupported("config API is unavailable");
  }
  return Result{api_->config->save_config(api_->config_ctx, view(key), document.handle())};
}

Result Config::save_file(
    std::string_view file_name,
    const DataDocument& document) const {
  auto key = local_key_from_config_file(file_name);
  if (!key) {
    return key.result;
  }
  return save(key.value, document);
}

bool Config::exists(std::string_view key) const {
  return api_ != nullptr && api_->config != nullptr &&
         api_->config->config_exists != nullptr &&
         api_->config->config_exists(api_->config_ctx, view(key));
}

bool Config::exists_file(std::string_view file_name) const {
  auto key = local_key_from_config_file(file_name);
  return key && exists(key.value);
}

Result Config::remove(std::string_view key) const {
  if (api_ == nullptr || api_->config == nullptr ||
      api_->config->remove_config == nullptr) {
    return Result::unsupported("config API is unavailable");
  }
  return Result{api_->config->remove_config(api_->config_ctx, view(key))};
}

Result Config::remove_file(std::string_view file_name) const {
  auto key = local_key_from_config_file(file_name);
  if (!key) {
    return key.result;
  }
  return remove(key.value);
}

ValueResult<DataDocument> Config::load_data(std::string_view key) const {
  ArvDataDocumentHandle handle{};
  if (api_ == nullptr || api_->config == nullptr ||
      api_->config->load_data == nullptr) {
    return {Result::unsupported("config API is unavailable"), {}};
  }
  Result result{api_->config->load_data(api_->config_ctx, view(key), &handle)};
  return {result, result ? DataDocument{api_, handle} : DataDocument{}};
}

ValueResult<DataDocument> Config::load_data_file(std::string_view file_name) const {
  auto key = local_key_from_data_file(file_name);
  if (!key) {
    return {key.result, {}};
  }
  return load_data(key.value);
}

Result Config::save_data(std::string_view key, const DataDocument& document) const {
  if (api_ == nullptr || api_->config == nullptr ||
      api_->config->save_data == nullptr) {
    return Result::unsupported("config API is unavailable");
  }
  return Result{api_->config->save_data(api_->config_ctx, view(key), document.handle())};
}

Result Config::save_data_file(
    std::string_view file_name,
    const DataDocument& document) const {
  auto key = local_key_from_data_file(file_name);
  if (!key) {
    return key.result;
  }
  return save_data(key.value, document);
}

bool Config::data_exists(std::string_view key) const {
  return api_ != nullptr && api_->config != nullptr &&
         api_->config->data_exists != nullptr &&
         api_->config->data_exists(api_->config_ctx, view(key));
}

bool Config::data_file_exists(std::string_view file_name) const {
  auto key = local_key_from_data_file(file_name);
  return key && data_exists(key.value);
}

Result Config::remove_data(std::string_view key) const {
  if (api_ == nullptr || api_->config == nullptr ||
      api_->config->remove_data == nullptr) {
    return Result::unsupported("config API is unavailable");
  }
  return Result{api_->config->remove_data(api_->config_ctx, view(key))};
}

Result Config::remove_data_file(std::string_view file_name) const {
  auto key = local_key_from_data_file(file_name);
  if (!key) {
    return key.result;
  }
  return remove_data(key.value);
}

ValueResult<DataDocument> Config::load_player_data(
    std::string_view profile_id,
    std::string_view key) const {
  ArvDataDocumentHandle handle{};
  if (api_ == nullptr || api_->config == nullptr ||
      api_->config->load_player_data == nullptr) {
    return {Result::unsupported("config API is unavailable"), {}};
  }
  Result result{api_->config->load_player_data(
      api_->config_ctx,
      view(profile_id),
      view(key),
      &handle)};
  return {result, result ? DataDocument{api_, handle} : DataDocument{}};
}

ValueResult<DataDocument> Config::load_player_data_file(
    std::string_view profile_id,
    std::string_view file_name) const {
  auto key = local_key_from_data_file(file_name);
  if (!key) {
    return {key.result, {}};
  }
  return load_player_data(profile_id, key.value);
}

Result Config::save_player_data(
    std::string_view profile_id,
    std::string_view key,
    const DataDocument& document) const {
  if (api_ == nullptr || api_->config == nullptr ||
      api_->config->save_player_data == nullptr) {
    return Result::unsupported("config API is unavailable");
  }
  return Result{api_->config->save_player_data(
      api_->config_ctx,
      view(profile_id),
      view(key),
      document.handle())};
}

Result Config::save_player_data_file(
    std::string_view profile_id,
    std::string_view file_name,
    const DataDocument& document) const {
  auto key = local_key_from_data_file(file_name);
  if (!key) {
    return key.result;
  }
  return save_player_data(profile_id, key.value, document);
}

bool Config::player_data_exists(
    std::string_view profile_id,
    std::string_view key) const {
  return api_ != nullptr && api_->config != nullptr &&
         api_->config->player_data_exists != nullptr &&
         api_->config->player_data_exists(api_->config_ctx, view(profile_id), view(key));
}

bool Config::player_data_file_exists(
    std::string_view profile_id,
    std::string_view file_name) const {
  auto key = local_key_from_data_file(file_name);
  return key && player_data_exists(profile_id, key.value);
}

Result Config::remove_player_data(
    std::string_view profile_id,
    std::string_view key) const {
  if (api_ == nullptr || api_->config == nullptr ||
      api_->config->remove_player_data == nullptr) {
    return Result::unsupported("config API is unavailable");
  }
  return Result{
      api_->config->remove_player_data(api_->config_ctx, view(profile_id), view(key))};
}

Result Config::remove_player_data_file(
    std::string_view profile_id,
    std::string_view file_name) const {
  auto key = local_key_from_data_file(file_name);
  if (!key) {
    return key.result;
  }
  return remove_player_data(profile_id, key.value);
}

ValueResult<std::string> Config::local_key_from_config_file(
    std::string_view file_name) const {
  if (api_ == nullptr || api_->context == nullptr ||
      api_->context->pack_id == nullptr) {
    return {Result{ResultCode::InvalidState, "plugin context is unavailable"}, {}};
  }

  const std::string_view pack_id = view(api_->context->pack_id(api_->context_ctx));
  if (pack_id.empty()) {
    return {Result{ResultCode::InvalidState, "plugin pack id is unavailable"}, {}};
  }

  const std::string_view path = strip_config_extension(file_name);
  if (!plugin_file_path_valid(path)) {
    return {Result{ResultCode::InvalidArgument, "config file name is invalid"}, {}};
  }

  std::string key;
  key.reserve(pack_id.size() + 1U + path.size());
  key.append(pack_id);
  key.push_back(':');
  key.append(path);
  return {Result::ok(), std::move(key)};
}

ValueResult<std::string> Config::local_key_from_data_file(
    std::string_view file_name) const {
  if (api_ == nullptr || api_->context == nullptr ||
      api_->context->pack_id == nullptr) {
    return {Result{ResultCode::InvalidState, "plugin context is unavailable"}, {}};
  }

  const std::string_view pack_id = view(api_->context->pack_id(api_->context_ctx));
  if (pack_id.empty()) {
    return {Result{ResultCode::InvalidState, "plugin pack id is unavailable"}, {}};
  }

  const std::string_view path = strip_data_extension(file_name);
  if (!plugin_file_path_valid(path)) {
    return {Result{ResultCode::InvalidArgument, "data file name is invalid"}, {}};
  }

  std::string key;
  key.reserve(pack_id.size() + 1U + path.size());
  key.append(pack_id);
  key.push_back(':');
  key.append(path);
  return {Result::ok(), std::move(key)};
}

} // namespace arv
