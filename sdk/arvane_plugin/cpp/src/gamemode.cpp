#include "arv/gamemode.hpp"

namespace arv {

GameModes::GameModes(const ArvAPI* api) noexcept : api_(api) {
}

Result GameModes::register_mode(const GameModeDefinition& definition) const {
  if (api_ == nullptr || api_->gamemodes == nullptr ||
      api_->gamemodes->register_mode == nullptr) {
    return Result::unsupported("game mode API is unavailable");
  }
  return Result{api_->gamemodes->register_mode(
      api_->gamemodes_ctx,
      &definition)};
}

bool GameModes::exists(std::string_view mode_id) const {
  if (api_ == nullptr || api_->gamemodes == nullptr ||
      api_->gamemodes->exists == nullptr) {
    return false;
  }
  return api_->gamemodes->exists(api_->gamemodes_ctx, view(mode_id));
}

ValueResult<GameModeInfo> GameModes::info(std::string_view mode_id) const {
  GameModeInfo value{};
  if (api_ == nullptr || api_->gamemodes == nullptr ||
      api_->gamemodes->info == nullptr) {
    return {Result::unsupported("game mode API is unavailable"), value};
  }
  return {
      Result{api_->gamemodes->info(api_->gamemodes_ctx, view(mode_id), &value)},
      value,
  };
}

ValueResult<std::string_view> GameModes::current(ArvPlayerID player) const {
  ArvStringView mode{};
  if (api_ == nullptr || api_->gamemodes == nullptr ||
      api_->gamemodes->current == nullptr) {
    return {Result::unsupported("game mode API is unavailable"), {}};
  }
  Result result{api_->gamemodes->current(
      api_->gamemodes_ctx,
      player,
      &mode)};
  return {result, view(mode)};
}

Result GameModes::set(
    ArvPlayerID player,
    std::string_view mode_id,
    std::string_view reason) const {
  if (api_ == nullptr || api_->gamemodes == nullptr ||
      api_->gamemodes->set == nullptr) {
    return Result::unsupported("game mode API is unavailable");
  }
  return Result{api_->gamemodes->set(
      api_->gamemodes_ctx,
      player,
      view(mode_id),
      view(reason))};
}

ValueResult<ResolvedCorePermissions> GameModes::base_core_permissions(
    std::string_view mode_id) const {
  ResolvedCorePermissions permissions{};
  if (api_ == nullptr || api_->gamemodes == nullptr ||
      api_->gamemodes->base_core_permissions == nullptr) {
    return {Result::unsupported("game mode API is unavailable"), permissions};
  }
  return {
      Result{api_->gamemodes->base_core_permissions(
          api_->gamemodes_ctx,
          view(mode_id),
          &permissions)},
      permissions,
  };
}

std::vector<GameModeInfo> GameModes::list_modes() const {
  std::vector<GameModeInfo> result;
  if (api_ == nullptr || api_->gamemodes == nullptr ||
      api_->gamemodes->list_modes == nullptr) {
    return result;
  }
  const std::uint64_t count =
      api_->gamemodes->list_modes(api_->gamemodes_ctx, nullptr, 0U);
  result.resize(count);
  const std::uint64_t written = api_->gamemodes->list_modes(
      api_->gamemodes_ctx,
      result.data(),
      result.size());
  result.resize(written);
  return result;
}

} // namespace arv
