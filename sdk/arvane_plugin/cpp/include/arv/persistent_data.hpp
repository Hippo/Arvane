#pragma once

#include "arv/api.h"
#include "arv/data.hpp"
#include "arv/io/codec.hpp"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <string_view>
#include <utility>

namespace arv {

class PersistentScopeRef {
public:
  PersistentScopeRef(const ArvAPI* api, ArvPersistentScope scope) noexcept;

  [[nodiscard]] ValueResult<DataDocument> load(std::string_view key) const;

  [[nodiscard]] Result save(std::string_view key, const DataDocument& document) const;

  [[nodiscard]] bool exists(std::string_view key) const;

  [[nodiscard]] Result remove(std::string_view key) const;

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
  ArvPersistentScope scope_{};
};

class PersistentData {
public:
  explicit PersistentData(const ArvAPI* api) noexcept;

  [[nodiscard]] PersistentScopeRef server() const noexcept;

  [[nodiscard]] PersistentScopeRef world(ArvWorldHandle world) const noexcept;

  [[nodiscard]] PersistentScopeRef player(ArvPlayerID player) const noexcept;

  [[nodiscard]] PersistentScopeRef entity(ArvEntityHandle entity) const noexcept;

  [[nodiscard]] PersistentScopeRef block(
      ArvWorldHandle world,
      ArvBlockPos pos) const noexcept;

  [[nodiscard]] PersistentScopeRef chunk(
      ArvWorldHandle world,
      ArvChunkPos pos) const noexcept;

private:
  const ArvAPI* api_{};
};

} // namespace arv
