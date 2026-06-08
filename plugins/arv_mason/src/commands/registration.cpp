#include "arv_mason/mason_plugin.hpp"

#include "arv_mason/ids.hpp"

#include <sstream>
#include <string>

namespace arv_mason {

MasonPlugin::MasonPlugin(arv::Plugin plugin) : plugin_(plugin) {
}

bool MasonPlugin::initialize() {
  config_ = load_config(plugin_.config(), plugin_.logger());
  messages_ = load_messages(plugin_.config(), plugin_.logger());
  (void)plugin_.variables().set_global(ids::variable_prefix, messages_.prefix);
  (void)plugin_.events().subscribe(
      "arvane:event/server_start",
      [this](arv::Event) {
        (void)plugin_.variables().set_global(ids::variable_prefix, messages_.prefix);
      });
  if (arv::Result permissions = register_permissions(); !permissions) {
    plugin_.logger().error(std::string{"Mason permission registration failed: "} + permissions.message());
    return false;
  }
  if (arv::Result commands = register_commands(); !commands) {
    plugin_.logger().error(std::string{"Mason command registration failed: "} + commands.message());
    return false;
  }
  plugin_.logger().info("Mason plugin initialized.");
  return true;
}

arv::Result MasonPlugin::register_permissions() {
  struct Node {
    std::string_view id;
    std::string_view description;
  };
  const Node nodes[] = {
      {ids::perm_use, "Use Mason commands."},
      {ids::perm_marker, "Receive a Mason marker item."},
      {ids::perm_set, "Fill a Mason selection."},
      {ids::perm_replace, "Replace blocks in a Mason selection."},
      {ids::perm_copy, "Copy a Mason selection."},
      {ids::perm_paste, "Paste a Mason clipboard."},
      {ids::perm_undo, "Undo Mason operations."},
      {ids::perm_redo, "Redo Mason operations."},
      {ids::perm_limit_bypass, "Bypass Mason operation limits."},
      {ids::perm_region_bypass, "Bypass region restrictions when server policy allows it."},
  };
  for (const Node& node : nodes) {
    arv::Result result = plugin_.permissions().register_permission_node({
        .node = node.id,
        .description = node.description,
        .default_value = arv::PermissionValue::Unset,
    });
    if (!result && result.code() != arv::ResultCode::DuplicateID) {
      return result;
    }
  }
  return arv::Result::ok();
}

arv::Result MasonPlugin::register_commands() {
  auto root = register_command(
      ids::exec_mason,
      "mason",
      ids::perm_use,
      "Mason world editing tools.",
      "/mason help",
      false,
      [this](arv::CommandContext& ctx) { return handle_mason(ctx); });
  if (!root) {
    return root;
  }

  struct CommandRegistration {
    std::string_view executor;
    std::string_view name;
    std::string_view permission;
    std::string_view description;
    std::string_view usage;
  };
  const CommandRegistration commands[] = {
      {ids::exec_marker, "mason.marker", ids::perm_marker, "Receive a Mason marker item.", "/mason marker"},
      {ids::exec_set, "mason.set", ids::perm_set, "Fill the current Mason selection.", "/mason set <block_state>"},
      {ids::exec_replace, "mason.replace", ids::perm_replace, "Replace blocks in the current Mason selection.", "/mason replace <from> <to>"},
      {ids::exec_copy, "mason.copy", ids::perm_copy, "Copy the current Mason selection.", "/mason copy"},
      {ids::exec_paste, "mason.paste", ids::perm_paste, "Paste the current Mason clipboard.", "/mason paste [--air|--no-air]"},
      {ids::exec_undo, "mason.undo", ids::perm_undo, "Undo the previous Mason operation.", "/mason undo"},
      {ids::exec_redo, "mason.redo", ids::perm_redo, "Redo the previous Mason operation.", "/mason redo"},
  };
  for (const CommandRegistration& command : commands) {
    auto handler = [this, name = command.name](arv::CommandContext& ctx) -> arv::Result {
      const Args args = ctx.args();
      if (name == "mason.marker") {
        return handle_marker(ctx, args);
      }
      if (name == "mason.set") {
        return handle_set(ctx, args);
      }
      if (name == "mason.replace") {
        return handle_replace(ctx, args);
      }
      if (name == "mason.copy") {
        return handle_copy(ctx, args);
      }
      if (name == "mason.paste") {
        return handle_paste(ctx, args);
      }
      if (name == "mason.undo") {
        return handle_undo(ctx, args);
      }
      if (name == "mason.redo") {
        return handle_redo(ctx, args);
      }
      return arv::Result{arv::ResultCode::InvalidState, "unknown Mason command"};
    };
    arv::Result result = register_command(
        command.executor,
        command.name,
        command.permission,
        command.description,
        command.usage,
        false,
        handler);
    if (!result) {
      return result;
    }
  }
  return arv::Result::ok();
}

arv::Result MasonPlugin::handle_mason(arv::CommandContext& ctx) {
  Args args = ctx.args();
  if (args.empty() || args[0] == "help") {
    info(ctx, "help");
    info(ctx, "aliases");
    return arv::Result::ok();
  }
  const std::string_view subcommand = args[0];
  Args rest{args.begin() + 1, args.end()};
  if (subcommand == "status") {
    return handle_status(ctx);
  }
  if (subcommand == "marker") {
    return handle_marker(ctx, rest);
  }
  if (subcommand == "set") {
    return handle_set(ctx, rest);
  }
  if (subcommand == "replace") {
    return handle_replace(ctx, rest);
  }
  if (subcommand == "copy") {
    return handle_copy(ctx, rest);
  }
  if (subcommand == "paste") {
    return handle_paste(ctx, rest);
  }
  if (subcommand == "undo") {
    return handle_undo(ctx, rest);
  }
  if (subcommand == "redo") {
    return handle_redo(ctx, rest);
  }
  if (subcommand == "confirm") {
    return handle_confirm(ctx, rest);
  }
  error(ctx, "unknown-subcommand");
  return arv::Result{arv::ResultCode::InvalidArgument, "unknown Mason subcommand"};
}

arv::Result MasonPlugin::handle_status(arv::CommandContext& ctx) {
  info(
      ctx,
      "status",
      {{"selection_limit", std::to_string(config_.limits.max_selection_blocks)},
       {"clipboard_limit", std::to_string(config_.limits.max_clipboard_blocks)}});
  return arv::Result::ok();
}

} // namespace arv_mason
