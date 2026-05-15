#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/message_writer.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace arv::protocol {

inline constexpr usize MaxTextVariableUpdateEntries = 512U;

enum class TextVariableScope : u8 {
  Global,
  Player,
};

enum class TextVariableBinding : u8 {
  Resolved,
  Live,
};

struct TextVariableUpdateEntry {
  TextVariableScope scope{TextVariableScope::Global};
  std::string name{};
  std::optional<std::string> value{};
  TextVariableBinding binding{TextVariableBinding::Resolved};
};

struct ClientboundTextVariableUpdate {
  bool reset{};
  std::vector<TextVariableUpdateEntry> entries{};
};

[[nodiscard]] std::vector<u8> serialize_clientbound_text_variable_update(
    const ClientboundTextVariableUpdate& update);
[[nodiscard]] Expected<ClientboundTextVariableUpdate, ProtocolError>
deserialize_clientbound_text_variable_update(std::span<const u8> bytes);

} // namespace arv::protocol
