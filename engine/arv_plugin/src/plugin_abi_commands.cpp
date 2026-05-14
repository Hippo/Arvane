#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

ArvResult api_register_executor(
    void* context,
    const ArvCommandDescriptor* descriptor) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult phase = require_registration_phase(ctx);
      phase.code != ARV_RESULT_OK) {
    return phase;
  }
  if (descriptor == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "command descriptor is null");
  }
  if (descriptor->execute == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "command descriptor execute callback is null");
  }
  const std::string executor_id{abi_view(descriptor->executor_id)};
  if (auto error = validate_owned_resource_id(ctx->info(), executor_id);
      error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  const std::string command_name{abi_view(descriptor->command_name)};
  if (!command_name_valid(command_name)) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "command name must be non-empty and contain no whitespace");
  }
  const auto duplicate = std::find_if(
      ctx->impl->commands.begin(),
      ctx->impl->commands.end(),
      [&](const PluginCommandRegistration& command) {
        return command.executor_id == executor_id ||
               command.command_name == command_name;
      });
  if (duplicate != ctx->impl->commands.end()) {
    return make_result(
        ctx,
        ARV_RESULT_DUPLICATE_ID,
        "duplicate command executor or command name: " + executor_id);
  }

  ctx->impl->commands.push_back({
      ctx->plugin_index,
      executor_id,
      command_name,
      command_domain_from_abi(descriptor->domain),
      abi_string(descriptor->permission),
      abi_string(descriptor->description),
      abi_string(descriptor->usage),
      descriptor->public_to_players,
      descriptor->console_only,
      descriptor->execute,
      descriptor->user_data,
  });
  return ok(ctx);
}


struct CommandExecutionContext {
  explicit CommandExecutionContext(command::CommandCtx& cpp_ctx) : cpp_context(cpp_ctx) {
    args.reserve(cpp_ctx.args.tokens.size());
    arg_views.reserve(cpp_ctx.args.tokens.size());
    for (std::string_view token : cpp_ctx.args.tokens) {
      args.emplace_back(token);
    }
    for (const std::string& arg : args) {
      arg_views.push_back(stable_view(arg));
    }
    abi_context.arg_count = static_cast<uint32_t>(arg_views.size());
    abi_context.args = arg_views.data();
    abi_context.engine_context = this;
    abi_context.reply = &reply;
    abi_context.source_kind =
        cpp_ctx.sender.kind == command::CommandSourceKind::Player
            ? ARV_COMMAND_SOURCE_PLAYER
            : ARV_COMMAND_SOURCE_SERVER_CONSOLE;
    if (cpp_ctx.sender.player_id.has_value()) {
      abi_context.has_player = true;
      abi_context.player = cpp_ctx.sender.player_id->value;
    }
    if (cpp_ctx.sender.entity_id.has_value()) {
      abi_context.has_entity = true;
      abi_context.entity = cpp_ctx.sender.entity_id->value;
    }
    if (cpp_ctx.sender.world_id.has_value()) {
      abi_context.has_world = true;
      abi_context.world = cpp_ctx.sender.world_id->value;
    }
    if (cpp_ctx.sender.position.has_value()) {
      abi_context.has_position = true;
      abi_context.position = {
          cpp_ctx.sender.position->x,
          cpp_ctx.sender.position->y,
          cpp_ctx.sender.position->z};
    }
  }

  static void reply(
      ArvCommandContext* command_context,
      ArvCommandMessageKind kind,
      ArvStringView message) {
    if (command_context == nullptr || command_context->engine_context == nullptr) {
      return;
    }
    auto* self = static_cast<CommandExecutionContext*>(command_context->engine_context);
    self->messages.push_back({
        command_message_kind_from_abi(kind),
        abi_string(message),
    });
  }

  command::CommandCtx& cpp_context;
  std::vector<std::string> args{};
  std::vector<ArvStringView> arg_views{};
  std::vector<command::CommandMessage> messages{};
  ArvCommandContext abi_context{};
};

[[nodiscard]] command::CommandResult invoke_plugin_command(
    PluginManagerImpl& impl,
    const PluginCommandRegistration& registration,
    command::CommandCtx& ctx) {
  if (registration.owner_index >= impl.plugins.size() ||
      registration.callback == nullptr) {
    return command::CommandResult::internal_error("Plugin command owner is unavailable.");
  }

  CommandExecutionContext execution{ctx};
  ThreadContextScope sync_scope{PluginThreadContext::Sync};
  ArvResult result =
      registration.callback(&execution.abi_context, registration.user_data);

  command::CommandResult command_result;
  command_result.status = command_status_from_result(result.code);
  command_result.messages = std::move(execution.messages);
  if (result.code != ARV_RESULT_OK && command_result.messages.empty()) {
    const std::string message =
        result.message != nullptr && result.message[0] != '\0'
            ? std::string{result.message}
            : "Plugin command failed: " + result_code_name(result.code);
    command_result.add_message(command::CommandMessageKind::Error, message);
  }
  return command_result;
}

} // namespace

void fill_command_api(PluginAPIStorage& storage) {
  storage.commands_api = {&api_register_executor};
}

void PluginManager::register_commands(command::CommandRegistry& registry) {
  for (const PluginCommandRegistration& registration : impl_->commands) {
    command::Command command;
    command.name = registration.command_name;
    command.domain = registration.domain;
    command.permission = registration.permission;
    command.description = registration.description;
    command.usage = registration.usage;
    command.public_to_players = registration.public_to_players;
    command.console_only = registration.console_only;
    command.execute = [impl = impl_.get(), registration](command::CommandCtx& ctx) {
      return invoke_plugin_command(*impl, registration, ctx);
    };
    auto result = registry.register_command(std::move(command));
    if (!result) {
      append_diagnostic(
          *impl_,
          impl_->plugins[registration.owner_index].info.pack_id,
          "command registration",
          {},
          result.error().message);
    }
  }
  for (const PluginRecord& plugin : impl_->plugins) {
    if (plugin.pack == nullptr || plugin.info.state != PluginState::Initialized) {
      continue;
    }
    detail::PluginCommandAliasLoadResult aliases =
        detail::load_command_aliases_from_data(*plugin.pack, plugin.info);
    for (PluginError& diagnostic : aliases.diagnostics) {
      append_diagnostic(*impl_, std::move(diagnostic));
    }
    for (detail::PluginCommandAliasData& alias : aliases.aliases) {
      auto result = registry.register_alias(
          std::move(alias.alias),
          std::move(alias.command_name),
          alias.visible_in_help);
      if (!result) {
        append_diagnostic(
            *impl_,
            plugin.info.pack_id,
            "command alias data",
            aliases.source_file,
            result.error().message);
      }
    }
  }
}


} // namespace arv::plugin
