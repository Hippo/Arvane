#include "arv/command/command.hpp"

#include "arv/core/string.hpp"

#include <charconv>
#include <cmath>
#include <string>

namespace arv::command {
namespace {

[[nodiscard]] std::string quoted(std::string_view token) {
  std::string out{"\""};
  out.append(token);
  out.push_back('"');
  return out;
}

[[nodiscard]] CommandResult expected_message(std::string_view label, std::string_view token) {
  std::string text{"Expected "};
  text.append(label);
  text.append(", got ");
  text.append(quoted(token));
  text.push_back('.');
  return CommandResult::usage_error(std::move(text));
}

template<typename T>
[[nodiscard]] Expected<T, CommandResult> parse_number(
    std::string_view token,
    std::string_view label) {
  T value{};
  const char* begin = token.data();
  const char* end = begin + token.size();
  const std::from_chars_result result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    return Expected<T, CommandResult>::failure(expected_message(label, token));
  }
  if constexpr (std::is_floating_point_v<T>) {
    if (!std::isfinite(value)) {
      return Expected<T, CommandResult>::failure(expected_message(label, token));
    }
  }
  return Expected<T, CommandResult>::success(value);
}

[[nodiscard]] f32 resolve_coord(RelativeCoord coord, f32 anchor) noexcept {
  return coord.relative
             ? static_cast<f32>(static_cast<f64>(anchor) + coord.value)
             : static_cast<f32>(coord.value);
}

} // namespace

CommandArgReader::CommandArgReader(const CommandArgs& args) noexcept
    : args_(&args) {
}

usize CommandArgReader::cursor() const noexcept {
  return cursor_;
}

usize CommandArgReader::remaining() const noexcept {
  if (args_ == nullptr || cursor_ >= args_->size()) {
    return 0U;
  }
  return args_->size() - cursor_;
}

bool CommandArgReader::exhausted() const noexcept {
  return remaining() == 0U;
}

Expected<std::string_view, CommandResult> CommandArgReader::read_token(std::string_view label) {
  if (args_ == nullptr || cursor_ >= args_->size()) {
    std::string text{"Expected "};
    text.append(label);
    text.push_back('.');
    return Expected<std::string_view, CommandResult>::failure(
        CommandResult::usage_error(std::move(text)));
  }
  return Expected<std::string_view, CommandResult>::success((*args_)[cursor_++]);
}

Expected<std::string, CommandResult> CommandArgReader::read_string(std::string_view label) {
  auto token = read_token(label);
  if (!token) {
    return Expected<std::string, CommandResult>::failure(std::move(token.error()));
  }
  return Expected<std::string, CommandResult>::success(std::string{token.value()});
}

Expected<i32, CommandResult> CommandArgReader::read_i32(std::string_view label) {
  auto token = read_token(label);
  if (!token) {
    return Expected<i32, CommandResult>::failure(std::move(token.error()));
  }
  return parse_number<i32>(token.value(), label);
}

Expected<u32, CommandResult> CommandArgReader::read_u32(std::string_view label) {
  auto token = read_token(label);
  if (!token) {
    return Expected<u32, CommandResult>::failure(std::move(token.error()));
  }
  return parse_number<u32>(token.value(), label);
}

Expected<f32, CommandResult> CommandArgReader::read_f32(std::string_view label) {
  auto token = read_token(label);
  if (!token) {
    return Expected<f32, CommandResult>::failure(std::move(token.error()));
  }
  return parse_number<f32>(token.value(), label);
}

Expected<f64, CommandResult> CommandArgReader::read_f64(std::string_view label) {
  auto token = read_token(label);
  if (!token) {
    return Expected<f64, CommandResult>::failure(std::move(token.error()));
  }
  return parse_number<f64>(token.value(), label);
}

Expected<bool, CommandResult> CommandArgReader::read_bool(std::string_view label) {
  auto token = read_token(label);
  if (!token) {
    return Expected<bool, CommandResult>::failure(std::move(token.error()));
  }

  const std::string lowered = lower_ascii_copy(token.value());
  if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on") {
    return Expected<bool, CommandResult>::success(true);
  }
  if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off") {
    return Expected<bool, CommandResult>::success(false);
  }
  return Expected<bool, CommandResult>::failure(expected_message(label, token.value()));
}

Expected<RelativeCoord, CommandResult> CommandArgReader::read_relative_coord(
    std::string_view label) {
  auto token = read_token(label);
  if (!token) {
    return Expected<RelativeCoord, CommandResult>::failure(std::move(token.error()));
  }

  const std::string_view text = token.value();
  if (!text.empty() && text.front() == '~') {
    if (text.size() == 1U) {
      return Expected<RelativeCoord, CommandResult>::success(RelativeCoord{true, 0.0});
    }

    const std::string_view offset = text.substr(1U);
    auto value = parse_number<f64>(offset, label);
    if (!value) {
      return Expected<RelativeCoord, CommandResult>::failure(expected_message(label, text));
    }
    return Expected<RelativeCoord, CommandResult>::success(RelativeCoord{true, value.value()});
  }

  auto value = parse_number<f64>(text, label);
  if (!value) {
    return Expected<RelativeCoord, CommandResult>::failure(std::move(value.error()));
  }
  return Expected<RelativeCoord, CommandResult>::success(RelativeCoord{false, value.value()});
}

Expected<CommandPosition, CommandResult> CommandArgReader::read_position() {
  auto x = read_relative_coord("x coordinate");
  if (!x) {
    return Expected<CommandPosition, CommandResult>::failure(std::move(x.error()));
  }
  auto y = read_relative_coord("y coordinate");
  if (!y) {
    return Expected<CommandPosition, CommandResult>::failure(std::move(y.error()));
  }
  auto z = read_relative_coord("z coordinate");
  if (!z) {
    return Expected<CommandPosition, CommandResult>::failure(std::move(z.error()));
  }
  return Expected<CommandPosition, CommandResult>::success(
      CommandPosition{x.value(), y.value(), z.value()});
}

Expected<void, CommandResult> CommandArgReader::expect_end() {
  if (args_ == nullptr || cursor_ >= args_->size()) {
    return Expected<void, CommandResult>::success();
  }

  std::string text{"Unexpected argument "};
  text.append(std::to_string(cursor_ + 1U));
  text.append(": ");
  text.append(quoted((*args_)[cursor_]));
  text.push_back('.');
  return Expected<void, CommandResult>::failure(CommandResult::usage_error(std::move(text)));
}

bool position_requires_anchor(const CommandPosition& position) noexcept {
  return position.x.relative || position.y.relative || position.z.relative;
}

Expected<Vec3f, CommandResult> resolve_position(
    const CommandPosition& position,
    std::optional<Vec3f> anchor) {
  if (position_requires_anchor(position) && !anchor.has_value()) {
    return Expected<Vec3f, CommandResult>::failure(
        CommandResult::usage_error("Relative coordinate requires an anchor."));
  }

  const Vec3f base = anchor.value_or(Vec3f{});
  return Expected<Vec3f, CommandResult>::success(
      Vec3f{
          resolve_coord(position.x, base.x),
          resolve_coord(position.y, base.y),
          resolve_coord(position.z, base.z),
      });
}

} // namespace arv::command
