#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::player {

inline constexpr std::string_view SurvivalGameModeID = "survival";
inline constexpr std::string_view CreativeGameModeID = "creative";
inline constexpr std::string_view SpectatorGameModeID = "spectator";
inline constexpr std::string_view LobbyGameModeID = "lobby";
inline constexpr std::string_view NoClipGameModeID = "noclip";

enum class TriState : u8 {
  Unset,
  Allow,
  Deny,
};

struct CorePermissionSet {
  TriState can_take_damage{TriState::Unset};
  TriState can_modify_world{TriState::Unset};
  TriState can_interact{TriState::Unset};
  TriState can_open_inventory{TriState::Unset};
  TriState has_creative_inventory{TriState::Unset};
  TriState can_fly{TriState::Unset};
  TriState has_collision{TriState::Unset};
  TriState is_visible_to_others{TriState::Unset};

  constexpr bool operator==(const CorePermissionSet&) const = default;
};

struct ResolvedCorePermissions {
  bool can_take_damage{true};
  bool can_modify_world{true};
  bool can_interact{true};
  bool can_open_inventory{true};
  bool has_creative_inventory{};
  bool can_fly{};
  bool has_collision{true};
  bool is_visible_to_others{true};

  constexpr bool operator==(const ResolvedCorePermissions&) const = default;
};

using CustomPermissionMap = HashMap<std::string, TriState>;

struct PermissionLayer {
  CorePermissionSet core{};
  CustomPermissionMap custom{};
};

struct ResolvedPermissions {
  ResolvedCorePermissions core{};
  CustomPermissionMap custom{};

  [[nodiscard]] TriState custom_value(std::string_view node) const;
  [[nodiscard]] bool has(std::string_view node) const;
};

struct PermissionError {
  std::string message{};
};

struct GameModeDefinition {
  std::string id{};
  std::string base_id{};
  ResolvedCorePermissions permissions{};
  CorePermissionSet overrides{};
  bool builtin{};
};

class GameModeRegistry {
public:
  using Result = Expected<void, PermissionError>;

  [[nodiscard]] Result add_builtin_mode(
      std::string id,
      ResolvedCorePermissions permissions);

  [[nodiscard]] Result add_custom_mode(
      std::string id,
      std::string base_id,
      CorePermissionSet overrides);

  [[nodiscard]] const GameModeDefinition* find(std::string_view id) const noexcept;
  [[nodiscard]] std::optional<ResolvedCorePermissions> permissions_for(
      std::string_view id) const noexcept;
  [[nodiscard]] std::vector<std::string> mode_ids() const;

private:
  std::vector<GameModeDefinition> modes_{};
  HashMap<std::string, usize> mode_indices_{};
};

struct PermissionGroup {
  std::string id{};
  i32 priority{};
  std::vector<std::string> inherits{};
  CorePermissionSet core{};
  CustomPermissionMap custom{};
};

struct PlayerPermissions {
  std::vector<std::string> groups{};
  CorePermissionSet overrides{};
  CustomPermissionMap custom_overrides{};
};

class PermissionGroupRegistry {
public:
  using Result = Expected<void, PermissionError>;
  using LayerResult = Expected<PermissionLayer, PermissionError>;

  [[nodiscard]] Result add_group(PermissionGroup group);
  [[nodiscard]] const PermissionGroup* find(std::string_view id) const noexcept;
  [[nodiscard]] LayerResult resolve_groups(std::span<const std::string> group_ids) const;

private:
  [[nodiscard]] LayerResult resolve_group(
      std::string_view id,
      std::vector<std::string>& stack) const;

  std::vector<PermissionGroup> groups_{};
  HashMap<std::string, usize> group_indices_{};
};

struct PermissionResolveRequest {
  std::string_view game_mode_id{SurvivalGameModeID};
  const PlayerPermissions* player_permissions{};
  std::span<const PermissionLayer> temporary_modifiers{};
  const PermissionLayer* region_permissions{};
  std::span<const PermissionLayer> plugin_overrides{};
};

[[nodiscard]] constexpr ResolvedCorePermissions survival_core_permissions() noexcept {
  return {};
}

[[nodiscard]] ResolvedPermissions resolve_permissions(
    const GameModeRegistry& game_modes,
    const PermissionGroupRegistry& groups,
    const PermissionResolveRequest& request);

[[nodiscard]] ResolvedCorePermissions apply_core_overrides(
    ResolvedCorePermissions base,
    std::span<const CorePermissionSet> overrides);

[[nodiscard]] TriState custom_permission_value(
    const CustomPermissionMap& permissions,
    std::string_view node);

[[nodiscard]] bool is_valid_custom_permission_node(std::string_view node) noexcept;

} // namespace arv::player
