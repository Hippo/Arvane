#include "arv/protocol/text_variables.hpp"

#include "arv/protocol/message_primitives.hpp"
#include "arv/text/rich_text.hpp"
#include "arv/text/variables.hpp"

#include <string_view>
#include <utility>

namespace arv::protocol {
namespace {

[[nodiscard]] bool valid_text_variable_scope(TextVariableScope scope) noexcept {
  switch (scope) {
    case TextVariableScope::Global:
    case TextVariableScope::Player:
      return true;
  }
  return false;
}

[[nodiscard]] bool valid_text_variable_binding(TextVariableBinding binding) noexcept {
  switch (binding) {
    case TextVariableBinding::Resolved:
    case TextVariableBinding::Live:
      return true;
  }
  return false;
}

[[nodiscard]] Expected<std::string, ProtocolError> read_bounded_string(
    MessageReader& reader,
    usize max_bytes,
    std::string_view label) {
  auto text = reader.read_string();
  if (!text) {
    return Expected<std::string, ProtocolError>::failure(text.error());
  }
  if (text.value().size() > max_bytes) {
    std::string message{label};
    message.append(" exceeds maximum length");
    return Expected<std::string, ProtocolError>::failure(ProtocolError{std::move(message)});
  }
  if (!arv::text::is_valid_utf8(text.value())) {
    std::string message{label};
    message.append(" is not valid UTF-8");
    return Expected<std::string, ProtocolError>::failure(ProtocolError{std::move(message)});
  }
  return Expected<std::string, ProtocolError>::success(std::move(text.value()));
}

} // namespace

std::vector<u8> serialize_clientbound_text_variable_update(
    const ClientboundTextVariableUpdate& update) {
  MessageWriter writer;
  writer.write_bool(update.reset);
  writer.write<u16>(static_cast<u16>(update.entries.size()));
  for (const TextVariableUpdateEntry& entry : update.entries) {
    writer.write<TextVariableScope>(entry.scope);
    writer.write<TextVariableBinding>(entry.binding);
    writer.write_string(entry.name);
    writer.write_bool(entry.value.has_value());
    if (entry.value.has_value()) {
      writer.write_string(entry.value.value());
    }
  }
  return std::move(writer).take_bytes();
}

Expected<ClientboundTextVariableUpdate, ProtocolError>
deserialize_clientbound_text_variable_update(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto reset = reader.read_bool();
  auto count = reader.read<u16>();
  if (!reset || !count) {
    return Expected<ClientboundTextVariableUpdate, ProtocolError>::failure(
        ProtocolError{"failed to decode text variable update header"});
  }
  if (count.value() > MaxTextVariableUpdateEntries) {
    return Expected<ClientboundTextVariableUpdate, ProtocolError>::failure(
        ProtocolError{"text variable update has too many entries"});
  }

  ClientboundTextVariableUpdate update;
  update.reset = reset.value();
  update.entries.reserve(count.value());
  for (u16 index = 0U; index < count.value(); ++index) {
    auto scope = reader.read<TextVariableScope>();
    if (!scope || !valid_text_variable_scope(scope.value())) {
      return Expected<ClientboundTextVariableUpdate, ProtocolError>::failure(
          ProtocolError{"invalid text variable scope"});
    }
    auto binding = reader.read<TextVariableBinding>();
    if (!binding || !valid_text_variable_binding(binding.value())) {
      return Expected<ClientboundTextVariableUpdate, ProtocolError>::failure(
          ProtocolError{"invalid text variable binding"});
    }
    auto name = read_bounded_string(
        reader,
        arv::text::MaxTextVariableNameBytes,
        "text variable name");
    if (!name) {
      return Expected<ClientboundTextVariableUpdate, ProtocolError>::failure(name.error());
    }
    if (!arv::text::valid_text_variable_name(name.value())) {
      return Expected<ClientboundTextVariableUpdate, ProtocolError>::failure(
          ProtocolError{"invalid text variable name"});
    }
    auto has_value = reader.read_bool();
    if (!has_value) {
      return Expected<ClientboundTextVariableUpdate, ProtocolError>::failure(
          has_value.error());
    }

    TextVariableUpdateEntry entry;
    entry.scope = scope.value();
    entry.binding = binding.value();
    entry.name = std::move(name.value());
    if (has_value.value()) {
      auto value = read_bounded_string(
          reader,
          arv::text::MaxTextVariableValueBytes,
          "text variable value");
      if (!value) {
        return Expected<ClientboundTextVariableUpdate, ProtocolError>::failure(
            value.error());
      }
      entry.value = std::move(value.value());
    }
    update.entries.push_back(std::move(entry));
  }

  auto trailing = require_no_trailing_bytes(reader, "text variable update");
  if (!trailing) {
    return Expected<ClientboundTextVariableUpdate, ProtocolError>::failure(
        trailing.error());
  }
  return Expected<ClientboundTextVariableUpdate, ProtocolError>::success(
      std::move(update));
}

} // namespace arv::protocol
