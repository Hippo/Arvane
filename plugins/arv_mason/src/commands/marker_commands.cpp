#include "arv_mason/mason_plugin.hpp"

#include "arv_mason/ids.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace arv_mason {
namespace {

inline constexpr std::string_view MarkerItemName = "arvane:iron_sword";

} // namespace

arv::Result MasonPlugin::handle_marker(arv::CommandContext& ctx, const Args& args) {
  if (!check_custom_permission(ctx, ids::perm_marker)) {
    return arv::Result{arv::ResultCode::PermissionDenied, "permission denied"};
  }
  if (!args.empty()) {
    error(ctx, "usage-marker");
    return arv::Result{arv::ResultCode::InvalidArgument, "usage error"};
  }

  const std::optional<ArvPlayerID> player = require_player(ctx);
  if (!player.has_value()) {
    error(ctx, "player-required");
    return arv::Result{arv::ResultCode::InvalidState, "player required"};
  }

  auto document = plugin_.data().create_document();
  if (!document) {
    error(ctx, "marker-create-failed", {{"error", document.result.message()}});
    return document.result;
  }
  if (arv::Result marker_data = document.value.set_bool("marker", true); !marker_data) {
    error(ctx, "marker-create-failed", {{"error", marker_data.message()}});
    return marker_data;
  }
  std::string display_name{messages_.get("marker-display-name")};
  if (display_name.empty()) {
    const MasonMessages defaults = default_messages();
    display_name = std::string{defaults.get("marker-display-name")};
  }
  if (display_name.empty()) {
    display_name = "Marker";
  }
  if (arv::Result marker_data = document.value.set_string("display_name", display_name);
      !marker_data) {
    error(ctx, "marker-create-failed", {{"error", marker_data.message()}});
    return marker_data;
  }
  const std::string select_a_message =
      message_source("marker-select-a", {{"position", "{position}"}});
  if (arv::Result marker_data = document.value.set_string("select_a_message", select_a_message);
      !marker_data) {
    error(ctx, "marker-create-failed", {{"error", marker_data.message()}});
    return marker_data;
  }
  const std::string select_b_message =
      message_source("marker-select-b", {{"position", "{position}"}});
  if (arv::Result marker_data = document.value.set_string("select_b_message", select_b_message);
      !marker_data) {
    error(ctx, "marker-create-failed", {{"error", marker_data.message()}});
    return marker_data;
  }

  arv::Result given = plugin_.items().give(*player, MarkerItemName, 1U, document.value);
  if (!given) {
    error(ctx, "marker-give-failed", {{"error", given.message()}});
    return given;
  }

  success(ctx, "marker-given");
  return arv::Result::ok();
}

} // namespace arv_mason
