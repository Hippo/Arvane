#pragma once

#include "arv/api.h"
#include "arv/data.hpp"
#include "arv/io/codec.hpp"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace arv {

class ConfigScope {
public:
  ConfigScope(const ArvAPI* api, ArvConfigScope scope) noexcept;

  [[nodiscard]] ValueResult<DataDocument> load(std::string_view key) const;

  [[nodiscard]] ValueResult<DataDocument> load_file(std::string_view file_name) const;

  [[nodiscard]] Result save(std::string_view key, const DataDocument& document) const;

  [[nodiscard]] Result save_file(std::string_view file_name, const DataDocument& document) const;

  [[nodiscard]] bool exists(std::string_view key) const;

  [[nodiscard]] bool exists_file(std::string_view file_name) const;

  [[nodiscard]] Result remove(std::string_view key) const;

  [[nodiscard]] Result remove_file(std::string_view file_name) const;

  template<typename T>
  [[nodiscard]] DecodeResult<T> load(std::string_view key) const {
    auto loaded = load(key);
    if (!loaded) {
      return {loaded.result, {}};
    }
    return Codec<T>::decode(loaded.value);
  }

  template<typename T>
  [[nodiscard]] DecodeResult<T> load_file(std::string_view file_name) const {
    auto loaded = load_file(file_name);
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
  [[nodiscard]] T load_file_or_default(std::string_view file_name, T fallback = {}) const {
    auto decoded = load_file<T>(file_name);
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

  template<typename T>
  [[nodiscard]] Result save_file(std::string_view file_name, const T& value) const {
    Data data{api_};
    auto created = data.create_document(Codec<T>::schema_version);
    if (!created) {
      return created.result;
    }
    Result encoded = Codec<T>::encode(created.value, value);
    if (!encoded) {
      return encoded;
    }
    return save_file(file_name, created.value);
  }

private:
  [[nodiscard]] ValueResult<std::string> local_key_from_file(
      std::string_view file_name) const;

  const ArvAPI* api_{};
  ArvConfigScope scope_{};
};

class Config {
public:
  explicit Config(const ArvAPI* api) noexcept;

  [[nodiscard]] ConfigScope global() const noexcept;

  [[nodiscard]] ConfigScope world(ArvWorldHandle world) const noexcept;

private:
  const ArvAPI* api_{};
};

} // namespace arv
