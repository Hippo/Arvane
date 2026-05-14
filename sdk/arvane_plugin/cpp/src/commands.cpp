#include "arv/commands.hpp"

namespace arv {

CommandContext::CommandContext(ArvCommandContext* context) noexcept
    : context_(context) {
}

std::uint32_t CommandContext::arg_count() const noexcept {
  return context_ == nullptr ? 0 : context_->arg_count;
}

std::string_view CommandContext::arg(std::uint32_t index) const noexcept {
  if (context_ == nullptr || index >= context_->arg_count || context_->args == nullptr) {
    return {};
  }
  return view(context_->args[index]);
}

std::vector<std::string_view> CommandContext::args() const {
  std::vector<std::string_view> values;
  const std::uint32_t count = arg_count();
  values.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    values.push_back(arg(i));
  }
  return values;
}

CommandSourceKind CommandContext::source_kind() const noexcept {
  return context_ == nullptr ? CommandSourceKind::ServerConsole
                             : static_cast<CommandSourceKind>(context_->source_kind);
}

bool CommandContext::is_player() const noexcept {
  return source_kind() == CommandSourceKind::Player;
}

std::optional<ArvPlayerID> CommandContext::player() const noexcept {
  if (context_ == nullptr || !context_->has_player) {
    return std::nullopt;
  }
  return context_->player;
}

std::optional<ArvEntityID> CommandContext::entity() const noexcept {
  if (context_ == nullptr || !context_->has_entity) {
    return std::nullopt;
  }
  return context_->entity;
}

std::optional<ArvWorldHandle> CommandContext::world() const noexcept {
  if (context_ == nullptr || !context_->has_world) {
    return std::nullopt;
  }
  return context_->world;
}

std::optional<ArvVec3> CommandContext::position() const noexcept {
  if (context_ == nullptr || !context_->has_position) {
    return std::nullopt;
  }
  return context_->position;
}

void CommandContext::reply(CommandMessageKind kind, std::string_view message) const {
  if (context_ != nullptr && context_->reply != nullptr) {
    context_->reply(
        context_,
        static_cast<ArvCommandMessageKind>(kind),
        view(message));
  }
}

void CommandContext::info(std::string_view message) const {
  reply(CommandMessageKind::Info, message);
}

void CommandContext::success(std::string_view message) const {
  reply(CommandMessageKind::Success, message);
}

void CommandContext::warn(std::string_view message) const {
  reply(CommandMessageKind::Warning, message);
}

void CommandContext::error(std::string_view message) const {
  reply(CommandMessageKind::Error, message);
}

Commands::Commands(const ArvAPI* api) noexcept : api_(api) {
}

} // namespace arv
