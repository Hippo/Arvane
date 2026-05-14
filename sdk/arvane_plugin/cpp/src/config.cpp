#include "arv/config.hpp"

namespace arv {

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

Result ConfigScope::save(std::string_view key, const DataDocument& document) const {
  if (api_ == nullptr || api_->config == nullptr || api_->config->save == nullptr) {
    return Result::unsupported("config API is unavailable");
  }
  return Result{api_->config->save(api_->config_ctx, &scope_, view(key), document.handle())};
}

bool ConfigScope::exists(std::string_view key) const {
  return api_ != nullptr && api_->config != nullptr && api_->config->exists != nullptr &&
         api_->config->exists(api_->config_ctx, &scope_, view(key));
}

Result ConfigScope::remove(std::string_view key) const {
  if (api_ == nullptr || api_->config == nullptr || api_->config->remove == nullptr) {
    return Result::unsupported("config API is unavailable");
  }
  return Result{api_->config->remove(api_->config_ctx, &scope_, view(key))};
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
