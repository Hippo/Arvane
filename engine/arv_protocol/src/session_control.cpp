#include "arv/protocol/session_control.hpp"

#include <utility>

namespace arv::protocol {
namespace {

[[nodiscard]] Expected<void, ProtocolError> reject_trailing_bytes(
    MessageReader& reader) {
  if (reader.remaining() != 0U) {
    return Expected<void, ProtocolError>::failure(
        ProtocolError{"trailing bytes in session control packet"});
  }
  return Expected<void, ProtocolError>::success();
}

[[nodiscard]] Expected<std::string, ProtocolError> read_bounded_kick_reason(
    MessageReader& reader) {
  auto reason = reader.read_string();
  if (!reason) {
    return Expected<std::string, ProtocolError>::failure(reason.error());
  }
  if (reason.value().size() > MaxKickReasonBytes) {
    return Expected<std::string, ProtocolError>::failure(
        ProtocolError{"kick reason exceeds maximum length"});
  }
  return Expected<std::string, ProtocolError>::success(std::move(reason.value()));
}

} // namespace

std::string clamp_kick_reason(std::string_view reason) {
  if (reason.size() <= MaxKickReasonBytes) {
    return std::string{reason};
  }
  return std::string{reason.substr(0U, MaxKickReasonBytes)};
}

std::vector<u8> serialize_clientbound_kick_player(
    const ClientboundKickPlayer& kick) {
  MessageWriter writer(MaxKickReasonBytes + sizeof(u16));
  writer.write_string(clamp_kick_reason(kick.reason));
  return std::move(writer).take_bytes();
}

Expected<ClientboundKickPlayer, ProtocolError> deserialize_clientbound_kick_player(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto reason = read_bounded_kick_reason(reader);
  if (!reason) {
    return Expected<ClientboundKickPlayer, ProtocolError>::failure(reason.error());
  }
  auto trailing = reject_trailing_bytes(reader);
  if (!trailing) {
    return Expected<ClientboundKickPlayer, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundKickPlayer, ProtocolError>::success({std::move(reason.value())});
}

std::vector<u8> serialize_clientbound_keep_alive(
    const ClientboundKeepAlive& keep_alive) {
  MessageWriter writer(sizeof(u64));
  writer.write<u64>(keep_alive.id);
  return std::move(writer).take_bytes();
}

Expected<ClientboundKeepAlive, ProtocolError> deserialize_clientbound_keep_alive(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto id = reader.read<u64>();
  if (!id) {
    return Expected<ClientboundKeepAlive, ProtocolError>::failure(id.error());
  }
  auto trailing = reject_trailing_bytes(reader);
  if (!trailing) {
    return Expected<ClientboundKeepAlive, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundKeepAlive, ProtocolError>::success({id.value()});
}

std::vector<u8> serialize_serverbound_keep_alive(
    const ServerboundKeepAlive& keep_alive) {
  MessageWriter writer(sizeof(u64));
  writer.write<u64>(keep_alive.id);
  return std::move(writer).take_bytes();
}

Expected<ServerboundKeepAlive, ProtocolError> deserialize_serverbound_keep_alive(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto id = reader.read<u64>();
  if (!id) {
    return Expected<ServerboundKeepAlive, ProtocolError>::failure(id.error());
  }
  auto trailing = reject_trailing_bytes(reader);
  if (!trailing) {
    return Expected<ServerboundKeepAlive, ProtocolError>::failure(trailing.error());
  }
  return Expected<ServerboundKeepAlive, ProtocolError>::success({id.value()});
}

} // namespace arv::protocol
