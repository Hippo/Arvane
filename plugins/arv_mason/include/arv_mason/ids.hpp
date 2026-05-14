#pragma once

#include <string_view>

namespace arv_mason::ids {

inline constexpr std::string_view pack = "arv_mason";
inline constexpr std::string_view name_space = "arv_mason";

inline constexpr std::string_view config_main = "arv_mason:config/main";
inline constexpr std::string_view player_state = "arv_mason:data/player_state";
inline constexpr std::string_view blueprint_index = "arv_mason:data/blueprint_index";

inline constexpr std::string_view perm_use = "mason.command.use";
inline constexpr std::string_view perm_select = "mason.command.select";
inline constexpr std::string_view perm_set = "mason.command.set";
inline constexpr std::string_view perm_replace = "mason.command.replace";
inline constexpr std::string_view perm_copy = "mason.command.copy";
inline constexpr std::string_view perm_paste = "mason.command.paste";
inline constexpr std::string_view perm_undo = "mason.command.undo";
inline constexpr std::string_view perm_redo = "mason.command.redo";
inline constexpr std::string_view perm_blueprint_save = "mason.command.blueprint.save";
inline constexpr std::string_view perm_blueprint_load = "mason.command.blueprint.load";
inline constexpr std::string_view perm_blueprint_paste = "mason.command.blueprint.paste";
inline constexpr std::string_view perm_blueprint_list = "mason.command.blueprint.list";
inline constexpr std::string_view perm_blueprint_info = "mason.command.blueprint.info";
inline constexpr std::string_view perm_limit_bypass = "mason.limit.bypass";
inline constexpr std::string_view perm_region_bypass = "mason.region.bypass";

inline constexpr std::string_view exec_mason = "arv_mason:command/mason";
inline constexpr std::string_view exec_select = "arv_mason:command/select";
inline constexpr std::string_view exec_set = "arv_mason:command/set";
inline constexpr std::string_view exec_replace = "arv_mason:command/replace";
inline constexpr std::string_view exec_copy = "arv_mason:command/copy";
inline constexpr std::string_view exec_paste = "arv_mason:command/paste";
inline constexpr std::string_view exec_undo = "arv_mason:command/undo";
inline constexpr std::string_view exec_redo = "arv_mason:command/redo";
inline constexpr std::string_view exec_blueprint = "arv_mason:command/blueprint";

} // namespace arv_mason::ids
