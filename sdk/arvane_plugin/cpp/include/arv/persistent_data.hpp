#pragma once

#include "arv/api.h"
#include "arv/codec.hpp"
#include "arv/data.hpp"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <string_view>
#include <utility>

namespace arv {

class PersistentScopeRef {
public:
  constexpr PersistentScopeRef(const ArvAPI* api, ArvPersistentScope scope) noexcept
      : api_(api),
        scope_(scope) {
  }

  [[nodiscard]] ValueResult<DataDocument> load(std::string_view key) const {
    ArvDataDocumentHandle handle{};
    if (api_ == nullptr || api_->persistent_data == nullptr ||
        api_->persistent_data->load == nullptr) {
      return {Result::unsupported("persistent data API is unavailable"), {}};
    }
    Result result{
        api_->persistent_data->load(api_->persistent_data_ctx, &scope_, view(key), &handle)};
    return {result, result ? DataDocument{api_, handle} : DataDocument{}};
  }

  [[nodiscard]] Result save(std::string_view key, const DataDocument& document) const {
    if (api_ == nullptr || api_->persistent_data == nullptr ||
        api_->persistent_data->save == nullptr) {
      return Result::unsupported("persistent data API is unavailable");
    }
    return Result{
        api_->persistent_data->save(api_->persistent_data_ctx, &scope_, view(key), document.handle())};
  }

  [[nodiscard]] bool exists(std::string_view key) const {
    return api_ != nullptr && api_->persistent_data != nullptr &&
           api_->persistent_data->exists != nullptr &&
           api_->persistent_data->exists(api_->persistent_data_ctx, &scope_, view(key));
  }

  [[nodiscard]] Result remove(std::string_view key) const {
    if (api_ == nullptr || api_->persistent_data == nullptr ||
        api_->persistent_data->remove == nullptr) {
      return Result::unsupported("persistent data API is unavailable");
    }
    return Result{api_->persistent_data->remove(api_->persistent_data_ctx, &scope_, view(key))};
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
  ArvPersistentScope scope_{};
};

class PersistentData {
public:
  explicit constexpr PersistentData(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] constexpr PersistentScopeRef server() const noexcept {
    return PersistentScopeRef{api_, {ARV_PERSISTENT_SCOPE_SERVER, 0, 0, 0, {}, {}}};
  }

  [[nodiscard]] constexpr PersistentScopeRef world(ArvWorldHandle world) const noexcept {
    return PersistentScopeRef{api_, {ARV_PERSISTENT_SCOPE_WORLD, world, 0, 0, {}, {}}};
  }

  [[nodiscard]] constexpr PersistentScopeRef player(ArvPlayerID player) const noexcept {
    return PersistentScopeRef{api_, {ARV_PERSISTENT_SCOPE_PLAYER, 0, player, 0, {}, {}}};
  }

  [[nodiscard]] constexpr PersistentScopeRef entity(ArvEntityHandle entity) const noexcept {
    return PersistentScopeRef{api_, {ARV_PERSISTENT_SCOPE_ENTITY, 0, 0, entity, {}, {}}};
  }

  [[nodiscard]] constexpr PersistentScopeRef block(
      ArvWorldHandle world,
      ArvBlockPos pos) const noexcept {
    return PersistentScopeRef{api_, {ARV_PERSISTENT_SCOPE_BLOCK, world, 0, 0, pos, {}}};
  }

  [[nodiscard]] constexpr PersistentScopeRef chunk(
      ArvWorldHandle world,
      ArvChunkPos pos) const noexcept {
    return PersistentScopeRef{api_, {ARV_PERSISTENT_SCOPE_CHUNK, world, 0, 0, {}, pos}};
  }

private:
  const ArvAPI* api_{};
};

} // namespace arv
