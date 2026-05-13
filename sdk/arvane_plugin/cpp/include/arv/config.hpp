#pragma once

#include "arv/api.h"
#include "arv/codec.hpp"
#include "arv/data.hpp"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <string_view>
#include <utility>

namespace arv {

class ConfigScope {
public:
  constexpr ConfigScope(const ArvAPI* api, ArvConfigScope scope) noexcept
      : api_(api),
        scope_(scope) {
  }

  [[nodiscard]] ValueResult<DataDocument> load(std::string_view key) const {
    ArvDataDocumentHandle handle{};
    if (api_ == nullptr || api_->config == nullptr || api_->config->load == nullptr) {
      return {Result::unsupported("config API is unavailable"), {}};
    }
    Result result{api_->config->load(api_->config_ctx, &scope_, view(key), &handle)};
    return {result, result ? DataDocument{api_, handle} : DataDocument{}};
  }

  [[nodiscard]] Result save(std::string_view key, const DataDocument& document) const {
    if (api_ == nullptr || api_->config == nullptr || api_->config->save == nullptr) {
      return Result::unsupported("config API is unavailable");
    }
    return Result{api_->config->save(api_->config_ctx, &scope_, view(key), document.handle())};
  }

  [[nodiscard]] bool exists(std::string_view key) const {
    return api_ != nullptr && api_->config != nullptr && api_->config->exists != nullptr &&
           api_->config->exists(api_->config_ctx, &scope_, view(key));
  }

  [[nodiscard]] Result remove(std::string_view key) const {
    if (api_ == nullptr || api_->config == nullptr || api_->config->remove == nullptr) {
      return Result::unsupported("config API is unavailable");
    }
    return Result{api_->config->remove(api_->config_ctx, &scope_, view(key))};
  }

  template<typename T>
  [[nodiscard]] DecodeResult<T> load(std::string_view key) const {
    auto loaded = load(key);
    if (!loaded) {
      return {loaded.result, {}};
    }
    return Codec<T>::decode(loaded.value);
  }

  template<typename T>
  [[nodiscard]] T load_or_default(std::string_view key, T fallback = {}) const {
    auto decoded = load<T>(key);
    return decoded ? std::move(decoded.value) : std::move(fallback);
  }

  template<typename T>
  [[nodiscard]] Result save(std::string_view key, const T& value) const {
    Data data{api_};
    auto created = data.create_document(Codec<T>::schema_version);
    if (!created) {
      return created.result;
    }
    Result encoded = Codec<T>::encode(created.value, value);
    if (!encoded) {
      return encoded;
    }
    return save(key, created.value);
  }

private:
  const ArvAPI* api_{};
  ArvConfigScope scope_{};
};

class Config {
public:
  explicit constexpr Config(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] constexpr ConfigScope global() const noexcept {
    return ConfigScope{api_, {ARV_CONFIG_SCOPE_GLOBAL, 0}};
  }

  [[nodiscard]] constexpr ConfigScope world(ArvWorldHandle world) const noexcept {
    return ConfigScope{api_, {ARV_CONFIG_SCOPE_WORLD, world}};
  }

private:
  const ArvAPI* api_{};
};

} // namespace arv
