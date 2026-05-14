#pragma once

#include "arv/api.h"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace arv {

enum class CommandDomain {
  ClientBound = ARV_COMMAND_CLIENT_BOUND,
  ServerBound = ARV_COMMAND_SERVER_BOUND,
};

enum class CommandMessageKind {
  Info = ARV_COMMAND_MESSAGE_INFO,
  Success = ARV_COMMAND_MESSAGE_SUCCESS,
  Warning = ARV_COMMAND_MESSAGE_WARNING,
  Error = ARV_COMMAND_MESSAGE_ERROR,
};

enum class CommandSourceKind {
  Player = ARV_COMMAND_SOURCE_PLAYER,
  ServerConsole = ARV_COMMAND_SOURCE_SERVER_CONSOLE,
};

class CommandContext {
public:
  explicit constexpr CommandContext(ArvCommandContext* context) noexcept
      : context_(context) {
  }

  [[nodiscard]] std::uint32_t arg_count() const noexcept {
    return context_ == nullptr ? 0 : context_->arg_count;
  }

  [[nodiscard]] std::string_view arg(std::uint32_t index) const noexcept {
    if (context_ == nullptr || index >= context_->arg_count || context_->args == nullptr) {
      return {};
    }
    return view(context_->args[index]);
  }

  [[nodiscard]] std::vector<std::string_view> args() const {
    std::vector<std::string_view> values;
    const std::uint32_t count = arg_count();
    values.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      values.push_back(arg(i));
    }
    return values;
  }

  [[nodiscard]] CommandSourceKind source_kind() const noexcept {
    return context_ == nullptr ? CommandSourceKind::ServerConsole
                               : static_cast<CommandSourceKind>(context_->source_kind);
  }

  [[nodiscard]] bool is_player() const noexcept {
    return source_kind() == CommandSourceKind::Player;
  }

  [[nodiscard]] std::optional<ArvPlayerID> player() const noexcept {
    if (context_ == nullptr || !context_->has_player) {
      return std::nullopt;
    }
    return context_->player;
  }

  [[nodiscard]] std::optional<ArvEntityID> entity() const noexcept {
    if (context_ == nullptr || !context_->has_entity) {
      return std::nullopt;
    }
    return context_->entity;
  }

  [[nodiscard]] std::optional<ArvWorldHandle> world() const noexcept {
    if (context_ == nullptr || !context_->has_world) {
      return std::nullopt;
    }
    return context_->world;
  }

  [[nodiscard]] std::optional<ArvVec3> position() const noexcept {
    if (context_ == nullptr || !context_->has_position) {
      return std::nullopt;
    }
    return context_->position;
  }

  void reply(CommandMessageKind kind, std::string_view message) const {
    if (context_ != nullptr && context_->reply != nullptr) {
      context_->reply(
          context_,
          static_cast<ArvCommandMessageKind>(kind),
          view(message));
    }
  }

  void info(std::string_view message) const {
    reply(CommandMessageKind::Info, message);
  }

  void success(std::string_view message) const {
    reply(CommandMessageKind::Success, message);
  }

  void warn(std::string_view message) const {
    reply(CommandMessageKind::Warning, message);
  }

  void error(std::string_view message) const {
    reply(CommandMessageKind::Error, message);
  }

private:
  ArvCommandContext* context_{};
};

struct CommandDescriptor {
  std::string_view executor_id{};
  std::string_view name{};
  CommandDomain domain{CommandDomain::ServerBound};
  std::string_view permission{};
  std::string_view description{};
  std::string_view usage{};
  bool public_to_players{};
  bool console_only{};
};

class Commands {
public:
  explicit constexpr Commands(const ArvAPI* api) noexcept : api_(api) {
  }

  template<typename Handler>
  [[nodiscard]] Result register_command(
      const CommandDescriptor& descriptor,
      Handler&& handler) const {
    if (api_ == nullptr || api_->commands == nullptr ||
        api_->commands->register_executor == nullptr) {
      return Result::unsupported("command API is unavailable");
    }
    using State = CommandHandlerState<std::decay_t<Handler>>;
    auto state = std::make_unique<State>(std::forward<Handler>(handler));

    ArvCommandDescriptor abi_descriptor{};
    abi_descriptor.executor_id = view(descriptor.executor_id);
    abi_descriptor.command_name = view(descriptor.name);
    abi_descriptor.domain = static_cast<ArvCommandDomain>(descriptor.domain);
    abi_descriptor.permission = view(descriptor.permission);
    abi_descriptor.description = view(descriptor.description);
    abi_descriptor.usage = view(descriptor.usage);
    abi_descriptor.public_to_players = descriptor.public_to_players;
    abi_descriptor.console_only = descriptor.console_only;
    abi_descriptor.execute = &State::invoke;
    abi_descriptor.user_data = state.get();

    Result result{api_->commands->register_executor(
        api_->commands_ctx,
        &abi_descriptor)};
    if (result) {
      state.release();
    }
    return result;
  }

  template<typename Handler>
  [[nodiscard]] Result register_executor(
      const CommandDescriptor& descriptor,
      Handler&& handler) const {
    return register_command(descriptor, std::forward<Handler>(handler));
  }

private:
  template<typename Handler>
  struct CommandHandlerState {
    template<typename HandlerValue>
    explicit CommandHandlerState(HandlerValue&& handler_value) : handler(std::forward<HandlerValue>(handler_value)) {
    }

    static ArvResult invoke(ArvCommandContext* context, void* user_data) {
      auto* self = static_cast<CommandHandlerState*>(user_data);
      if (self == nullptr) {
        return Result{ResultCode::InvalidState, "command handler is unavailable"}.abi();
      }
      CommandContext command_context{context};
      if constexpr (std::is_same_v<std::invoke_result_t<Handler&, CommandContext&>, Result>) {
        return std::invoke(self->handler, command_context).abi();
      } else if constexpr (std::is_same_v<std::invoke_result_t<Handler&, CommandContext&>, bool>) {
        return (std::invoke(self->handler, command_context)
                    ? Result::ok()
                    : Result{ResultCode::InternalError, "command handler returned false"})
            .abi();
      } else {
        std::invoke(self->handler, command_context);
        return Result::ok().abi();
      }
    }

    Handler handler;
  };

  const ArvAPI* api_{};
};

} // namespace arv
