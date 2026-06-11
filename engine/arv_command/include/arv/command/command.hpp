#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"
#include "arv/crypto/hash.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::text {
class LanguageCatalog;
} // namespace arv::text

namespace arv::command {

enum class CommandDomain : u8 {
  ClientBound,
  ServerBound,
};

enum class CommandSourceKind : u8 {
  Player,
  ServerConsole,
};

enum class CommandStatus : u8 {
  Success,
  Failure,
  UsageError,
  PermissionDenied,
  UnknownCommand,
  InvalidSource,
  InternalError,
};

enum class CommandMessageKind : u8 {
  Info,
  Success,
  Warning,
  Error,
};

struct CommandMessage {
  CommandMessageKind kind{CommandMessageKind::Info};
  std::string text{};
};

struct CommandResult {
  CommandStatus status{CommandStatus::Success};
  std::vector<CommandMessage> messages{};

  [[nodiscard]] bool succeeded() const noexcept {
    return status == CommandStatus::Success;
  }

  explicit operator bool() const noexcept {
    return succeeded();
  }

  void add_message(CommandMessageKind kind, std::string text) {
    messages.push_back({kind, std::move(text)});
  }

  [[nodiscard]] static CommandResult with_message(
      CommandStatus status,
      CommandMessageKind kind,
      std::string text) {
    CommandResult result;
    result.status = status;
    if (!text.empty()) {
      result.add_message(kind, std::move(text));
    }
    return result;
  }

  [[nodiscard]] static CommandResult success(std::string text = {}) {
    return with_message(CommandStatus::Success, CommandMessageKind::Success, std::move(text));
  }

  [[nodiscard]] static CommandResult failure(std::string text) {
    return with_message(CommandStatus::Failure, CommandMessageKind::Error, std::move(text));
  }

  [[nodiscard]] static CommandResult usage_error(std::string text) {
    return with_message(CommandStatus::UsageError, CommandMessageKind::Error, std::move(text));
  }

  [[nodiscard]] static CommandResult permission_denied(std::string text) {
    return with_message(
        CommandStatus::PermissionDenied,
        CommandMessageKind::Error,
        std::move(text));
  }

  [[nodiscard]] static CommandResult unknown_command(std::string text) {
    return with_message(CommandStatus::UnknownCommand, CommandMessageKind::Error, std::move(text));
  }

  [[nodiscard]] static CommandResult invalid_source(std::string text) {
    return with_message(CommandStatus::InvalidSource, CommandMessageKind::Error, std::move(text));
  }

  [[nodiscard]] static CommandResult internal_error(std::string text) {
    return with_message(CommandStatus::InternalError, CommandMessageKind::Error, std::move(text));
  }
};

struct CommandSender {
  CommandSourceKind kind{CommandSourceKind::ServerConsole};
  std::optional<crypto::PlayerProfileID> player_profile_id{};
  std::optional<SessionID> session_id{};
  std::optional<PlayerID> player_id{};
  std::optional<EntityID> entity_id{};
  std::optional<WorldID> world_id{};
  std::optional<Vec3f> position{};
  std::string display_name{"server"};
};

struct Command;
struct CommandCtx;
class CommandRegistry;

using CommandExecuteFn = std::function<CommandResult(CommandCtx&)>;

enum class CommandArgumentType : u8 {
  String,
  Integer,
  UnsignedInteger,
  Number,
  Boolean,
  Enum,
  Identifier,
  TargetSelector,
  Position,
  Message,
  Metadata,
};

struct CommandArgumentSpec {
  std::string name{};
  CommandArgumentType type{CommandArgumentType::String};
  bool required{true};
  bool variadic{};
  std::vector<std::string> enum_values{};
  std::string description{};
};

struct Command {
  std::string name{};
  CommandDomain domain{CommandDomain::ServerBound};
  std::vector<std::string> aliases{};
  std::string permission{};
  std::string description{};
  std::string usage{};
  std::vector<CommandArgumentSpec> arguments{};
  std::string technical_notes{};
  std::vector<std::string> examples{};
  bool public_to_players{};
  bool console_only{};
  CommandExecuteFn execute{};
};

struct CommandPermissionResult {
  bool allowed{};
  bool has_message{};
  std::string message{};

  [[nodiscard]] static CommandPermissionResult allow() {
    return {true, false, {}};
  }

  [[nodiscard]] static CommandPermissionResult deny(std::string message) {
    return {false, true, std::move(message)};
  }

  [[nodiscard]] static CommandPermissionResult deny_without_message() {
    return {false, false, {}};
  }
};

using CommandPermissionCheckFn =
    FunctionRef<CommandPermissionResult(const CommandSender&, const Command&)>;

struct CommandServices {
  void* domain_services{};
  CommandPermissionCheckFn permission_check{};
  const arv::text::LanguageCatalog* language{};
  const CommandRegistry* registry{};

  template<typename T>
  [[nodiscard]] T* as() noexcept {
    return static_cast<T*>(domain_services);
  }

  template<typename T>
  [[nodiscard]] const T* as() const noexcept {
    return static_cast<const T*>(domain_services);
  }
};

struct CommandArgs {
  std::vector<std::string_view> tokens{};

  [[nodiscard]] bool empty() const noexcept {
    return tokens.empty();
  }

  [[nodiscard]] usize size() const noexcept {
    return tokens.size();
  }

  [[nodiscard]] std::string_view operator[](usize index) const noexcept {
    return tokens[index];
  }
};

struct RelativeCoord {
  bool relative{};
  f64 value{};
};

struct CommandPosition {
  RelativeCoord x{};
  RelativeCoord y{};
  RelativeCoord z{};
};

class CommandArgReader {
public:
  explicit CommandArgReader(const CommandArgs& args) noexcept;

  [[nodiscard]] usize cursor() const noexcept;
  [[nodiscard]] usize remaining() const noexcept;
  [[nodiscard]] bool exhausted() const noexcept;

  [[nodiscard]] Expected<std::string_view, CommandResult> read_token(
      std::string_view label = "argument");
  [[nodiscard]] Expected<std::string, CommandResult> read_string(
      std::string_view label = "argument");
  [[nodiscard]] Expected<i32, CommandResult> read_i32(std::string_view label = "integer");
  [[nodiscard]] Expected<u32, CommandResult> read_u32(std::string_view label = "unsigned integer");
  [[nodiscard]] Expected<f32, CommandResult> read_f32(std::string_view label = "number");
  [[nodiscard]] Expected<f64, CommandResult> read_f64(std::string_view label = "number");
  [[nodiscard]] Expected<bool, CommandResult> read_bool(std::string_view label = "boolean");
  [[nodiscard]] Expected<RelativeCoord, CommandResult> read_relative_coord(
      std::string_view label = "coordinate");
  [[nodiscard]] Expected<CommandPosition, CommandResult> read_position();
  [[nodiscard]] Expected<void, CommandResult> expect_end();

private:
  const CommandArgs* args_{};
  usize cursor_{};
};

struct CommandCtx {
  const Command& command;
  const CommandSender& sender;
  const CommandArgs& args;
  CommandServices& services;

  [[nodiscard]] CommandArgReader reader() const noexcept {
    return CommandArgReader{args};
  }

  [[nodiscard]] CommandResult success(std::string text = {}) const {
    return CommandResult::success(std::move(text));
  }

  [[nodiscard]] CommandResult failure(std::string text) const {
    return CommandResult::failure(std::move(text));
  }

  [[nodiscard]] CommandResult usage_error(std::string text) const {
    return CommandResult::usage_error(std::move(text));
  }
};

[[nodiscard]] bool position_requires_anchor(const CommandPosition& position) noexcept;

[[nodiscard]] Expected<Vec3f, CommandResult> resolve_position(
    const CommandPosition& position,
    std::optional<Vec3f> anchor);

} // namespace arv::command
