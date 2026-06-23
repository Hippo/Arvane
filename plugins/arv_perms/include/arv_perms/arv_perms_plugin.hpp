#pragma once

#include "arv_perms/permission_store.hpp"

#include <arv/plugin.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv_perms {

class ArvPermsPlugin {
public:
  explicit ArvPermsPlugin(arv::Plugin plugin);

  [[nodiscard]] bool initialize();

private:
  using Args = std::vector<std::string_view>;

  [[nodiscard]] arv::Result register_permissions();
  [[nodiscard]] arv::Result register_commands();
  [[nodiscard]] arv::Result handle_perm(arv::CommandContext& ctx);

  [[nodiscard]] bool check_command_permission(
      arv::CommandContext& ctx,
      std::string_view permission) const;

  [[nodiscard]] arv::Result handle_group(arv::CommandContext& ctx, const Args& args);
  [[nodiscard]] arv::Result handle_user(arv::CommandContext& ctx, const Args& args);
  [[nodiscard]] arv::Result handle_default(arv::CommandContext& ctx, const Args& args);
  [[nodiscard]] arv::Result handle_save(arv::CommandContext& ctx);
  [[nodiscard]] arv::Result handle_reload(arv::CommandContext& ctx);
  [[nodiscard]] arv::Result handle_validate(arv::CommandContext& ctx);

  [[nodiscard]] bool load_store();
  [[nodiscard]] bool save_store(std::string& out_error);
  [[nodiscard]] bool save_config_files(std::string& out_error);
  [[nodiscard]] bool save_player_record(
      const PlayerPermissionRecord& record,
      std::string& out_error);
  bool load_player_if_present(std::string_view profile_id);
  [[nodiscard]] PlayerPermissionRecord& ensure_player_record(
      std::string profile_id,
      std::optional<std::string> last_known_name = std::nullopt);
  void mark_dirty_and_autosave(arv::CommandContext& ctx);
  void upsert_runtime_groups();
  void apply_all_online_players();
  void apply_player_if_online(std::string_view profile_id);
  void apply_player_record(ArvPlayerID player, PlayerPermissionRecord& record);
  void handle_server_start();
  void schedule_reapply();

  [[nodiscard]] std::vector<ArvCustomPermissionEntry> custom_entries(
      const CustomPermissionMap& permissions,
      std::vector<std::string>& stable_nodes) const;

  arv::Plugin plugin_;
  PermissionStore store_{};
  arv::Job reapply_job_{};
  bool store_loaded_{};
};

} // namespace arv_perms
