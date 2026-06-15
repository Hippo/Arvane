#pragma once

#include <arv/data.hpp>
#include <arv/permissions.hpp>
#include <arv/result.hpp>
#include <arv/types.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv_perms {

using GroupId = std::string;
using ProfileId = std::string;
using CustomPermissionMap = std::map<std::string, arv::PermissionValue>;

struct ArvPermsConfig {
  std::vector<GroupId> default_groups{"player"};
  std::optional<GroupId> operator_group{"owner"};
  bool auto_save{true};
  int save_interval_seconds{60};
  bool track_last_known_names{true};
};

struct PermissionGroupRecord {
  GroupId id{};
  int priority{};
  std::vector<GroupId> inherits{};
  arv::CorePermissionSet core{};
  CustomPermissionMap permissions{};
};

struct PlayerPermissionRecord {
  ProfileId player_id{};
  std::optional<std::string> last_known_name{};
  std::vector<GroupId> groups{};
  arv::CorePermissionSet core_overrides{};
  CustomPermissionMap permission_overrides{};
};

struct ResolvedPlayerOverrideLayer {
  std::vector<GroupId> effective_groups{};
  arv::CorePermissionSet core{};
  CustomPermissionMap permissions{};
};

struct ValidationDiagnostic {
  std::string message{};
};

struct ValidationResult {
  std::vector<ValidationDiagnostic> diagnostics{};

  [[nodiscard]] bool ok() const noexcept {
    return diagnostics.empty();
  }
};

struct PermissionTraceLine {
  GroupId group_id{};
  int priority{};
  arv::PermissionValue value{arv::PermissionValue::Unset};
  std::string matched_node{};
};

struct PermissionTrace {
  std::string node{};
  std::vector<GroupId> assigned_groups{};
  std::vector<GroupId> effective_groups{};
  std::vector<PermissionTraceLine> group_lines{};
  arv::PermissionValue direct_override{arv::PermissionValue::Unset};
  arv::PermissionValue player_overrides_result{arv::PermissionValue::Unset};
};

class PermissionStore {
public:
  ArvPermsConfig config{};
  std::map<GroupId, PermissionGroupRecord> groups{};
  std::map<ProfileId, PlayerPermissionRecord> players{};
  bool dirty{};

  [[nodiscard]] PlayerPermissionRecord& ensure_player(
      std::string profile_id,
      std::optional<std::string> last_known_name = std::nullopt);

  [[nodiscard]] const PlayerPermissionRecord* find_player(
      std::string_view profile_id) const noexcept;

  [[nodiscard]] ValidationResult validate() const;
  [[nodiscard]] ResolvedPlayerOverrideLayer resolve_player(
      const PlayerPermissionRecord& player) const;
  [[nodiscard]] PermissionTrace trace_custom(
      const PlayerPermissionRecord& player,
      std::string_view node) const;
};

struct StoreLoadResult {
  bool ok{};
  PermissionStore store{};
  std::vector<std::string> diagnostics{};
};

[[nodiscard]] std::optional<arv::PermissionValue> permission_value_from_string(
    std::string_view text);
[[nodiscard]] const char* permission_value_name(arv::PermissionValue value) noexcept;
[[nodiscard]] bool is_permission_allow(arv::PermissionValue value) noexcept;
[[nodiscard]] bool is_permission_deny(arv::PermissionValue value) noexcept;
[[nodiscard]] bool is_permission_unset(arv::PermissionValue value) noexcept;

[[nodiscard]] std::optional<arv::CorePermission> core_permission_from_name(
    std::string_view name) noexcept;
[[nodiscard]] std::string_view core_permission_name(arv::CorePermission permission) noexcept;
[[nodiscard]] bool set_core_permission(
    arv::CorePermissionSet& set,
    arv::CorePermission permission,
    arv::PermissionValue value) noexcept;
[[nodiscard]] arv::PermissionValue core_permission_value(
    const arv::CorePermissionSet& set,
    arv::CorePermission permission) noexcept;
[[nodiscard]] std::vector<std::string_view> core_permission_names();

[[nodiscard]] bool is_valid_group_id(std::string_view text) noexcept;
[[nodiscard]] bool is_valid_profile_id(std::string_view text) noexcept;
[[nodiscard]] bool is_valid_permission_node(std::string_view node) noexcept;

[[nodiscard]] PermissionStore default_store();
[[nodiscard]] StoreLoadResult load_store_from_documents(
    const arv::DataDocument* config_document,
    const arv::DataDocument* groups_document);
[[nodiscard]] std::optional<PlayerPermissionRecord> load_player_record_from_document(
    std::string_view profile_id,
    const arv::DataDocument& document,
    std::vector<std::string>& diagnostics);

[[nodiscard]] arv::Result encode_config_document(
    const ArvPermsConfig& config,
    const arv::DataDocument& document);
[[nodiscard]] arv::Result encode_groups_document(
    const std::map<GroupId, PermissionGroupRecord>& groups,
    const arv::DataDocument& document);
[[nodiscard]] arv::Result encode_player_document(
    const PlayerPermissionRecord& player,
    const arv::DataDocument& document);

} // namespace arv_perms
