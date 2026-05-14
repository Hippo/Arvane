#include "arv/persistent_data.hpp"

namespace arv {

PersistentScopeRef::PersistentScopeRef(const ArvAPI* api, ArvPersistentScope scope) noexcept
    : api_(api), scope_(scope) {
}

ValueResult<DataDocument> PersistentScopeRef::load(std::string_view key) const {
  ArvDataDocumentHandle handle{};
  if (api_ == nullptr || api_->persistent_data == nullptr ||
      api_->persistent_data->load == nullptr) {
    return {Result::unsupported("persistent data API is unavailable"), {}};
  }
  Result result{
      api_->persistent_data->load(api_->persistent_data_ctx, &scope_, view(key), &handle)};
  return {result, result ? DataDocument{api_, handle} : DataDocument{}};
}

Result PersistentScopeRef::save(std::string_view key, const DataDocument& document) const {
  if (api_ == nullptr || api_->persistent_data == nullptr ||
      api_->persistent_data->save == nullptr) {
    return Result::unsupported("persistent data API is unavailable");
  }
  return Result{
      api_->persistent_data->save(api_->persistent_data_ctx, &scope_, view(key), document.handle())};
}

bool PersistentScopeRef::exists(std::string_view key) const {
  return api_ != nullptr && api_->persistent_data != nullptr &&
         api_->persistent_data->exists != nullptr &&
         api_->persistent_data->exists(api_->persistent_data_ctx, &scope_, view(key));
}

Result PersistentScopeRef::remove(std::string_view key) const {
  if (api_ == nullptr || api_->persistent_data == nullptr ||
      api_->persistent_data->remove == nullptr) {
    return Result::unsupported("persistent data API is unavailable");
  }
  return Result{api_->persistent_data->remove(api_->persistent_data_ctx, &scope_, view(key))};
}

PersistentData::PersistentData(const ArvAPI* api) noexcept : api_(api) {
}

PersistentScopeRef PersistentData::server() const noexcept {
  return PersistentScopeRef{api_, {ARV_PERSISTENT_SCOPE_SERVER, 0, 0, 0, {}, {}}};
}

PersistentScopeRef PersistentData::world(ArvWorldHandle world) const noexcept {
  return PersistentScopeRef{api_, {ARV_PERSISTENT_SCOPE_WORLD, world, 0, 0, {}, {}}};
}

PersistentScopeRef PersistentData::player(ArvPlayerID player) const noexcept {
  return PersistentScopeRef{api_, {ARV_PERSISTENT_SCOPE_PLAYER, 0, player, 0, {}, {}}};
}

PersistentScopeRef PersistentData::entity(ArvEntityHandle entity) const noexcept {
  return PersistentScopeRef{api_, {ARV_PERSISTENT_SCOPE_ENTITY, 0, 0, entity, {}, {}}};
}

PersistentScopeRef PersistentData::block(
    ArvWorldHandle world,
    ArvBlockPos pos) const noexcept {
  return PersistentScopeRef{api_, {ARV_PERSISTENT_SCOPE_BLOCK, world, 0, 0, pos, {}}};
}

PersistentScopeRef PersistentData::chunk(
    ArvWorldHandle world,
    ArvChunkPos pos) const noexcept {
  return PersistentScopeRef{api_, {ARV_PERSISTENT_SCOPE_CHUNK, world, 0, 0, {}, pos}};
}

} // namespace arv
