#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

ArvResult api_network_create_chat_message(
    void* context,
    ArvTextHandle message,
    ArvStringView channel,
    ArvChatMessageKind kind,
    ArvPacketHandle* out_packet) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_packet == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_packet is null");
  }
  *out_packet = {};
  arv::text::RichText* rich_text = text_from_handle(ctx, message);
  if (rich_text == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  const arv::text::RichTextValidation text_validation =
      arv::text::validate_rich_text(*rich_text);
  if (!text_validation.valid) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, text_validation.message);
  }
  std::string channel_id = abi_string(channel);
  if (channel_id.empty()) {
    channel_id = std::string{protocol::ChatChannelSystem};
  }
  if (channel_id.size() > protocol::MaxChatChannelBytes) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "chat channel is too long");
  }
  if (std::optional<std::string> error = validate_resource_id(channel_id);
      error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, *error);
  }
  std::optional<protocol::ChatMessageKind> message_kind =
      chat_message_kind_from_abi(kind);
  if (!message_kind.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid chat message kind");
  }

  protocol::ClientboundChatMessage packet{
      0U,
      std::move(channel_id),
      *message_kind,
      *rich_text};
  *out_packet = store_packet(
      ctx,
      {
          protocol::MessageType::ClientboundChatMessage,
          protocol::ReliabilityClass::ReliableOrdered,
          protocol::NetworkLane::Gameplay,
          protocol::serialize_clientbound_chat_message(packet),
      });
  return ok(ctx);
}

ArvResult api_network_create_kick_player(
    void* context,
    ArvTextHandle reason,
    ArvPacketHandle* out_packet) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_packet == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_packet is null");
  }
  *out_packet = {};
  arv::text::RichText* rich_text = text_from_handle(ctx, reason);
  if (rich_text == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "text handle not found");
  }
  const arv::text::RichTextValidation text_validation =
      arv::text::validate_rich_text(*rich_text);
  if (!text_validation.valid) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, text_validation.message);
  }
  const protocol::ClientboundKickPlayer packet{
      protocol::clamp_kick_reason(arv::text::to_plain_text(*rich_text))};
  *out_packet = store_packet(
      ctx,
      {
          protocol::MessageType::ClientboundKickPlayer,
          protocol::ReliabilityClass::ReliableImmediate,
          protocol::NetworkLane::Session,
          protocol::serialize_clientbound_kick_player(packet),
      });
  return ok(ctx);
}

[[nodiscard]] ArvResult send_network_request(
    PluginCallContext* ctx,
    PluginNetworkSendRequest request) {
  ArvResult sync = require_sync_context(ctx, "NetworkAPI");
  if (sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (ctx->impl->runtime.network_send == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_STATE,
        "network sender is unavailable");
  }
  if (request.packet == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "packet handle not found");
  }
  PluginNetworkSendResult result = ctx->impl->runtime.network_send(request);
  if (!result) {
    return make_result(ctx, result.error().code, result.error().message);
  }
  return ok(ctx);
}

ArvResult api_network_send_to_player(
    void* context,
    ArvPlayerID player,
    ArvPacketHandle packet) {
  PluginCallContext* ctx = call_context(context);
  ArvResult sync = require_sync_context(ctx, "NetworkAPI");
  if (sync.code != ARV_RESULT_OK) {
    return sync;
  }
  PluginNetworkPacket* network_packet = packet_from_handle(ctx, packet);
  PluginNetworkSendRequest request;
  request.target_kind = PluginNetworkTargetKind::Player;
  request.player = PlayerID{player};
  request.packet = network_packet;
  return send_network_request(ctx, request);
}

ArvResult api_network_send_to_players(
    void* context,
    const ArvPlayerID* players,
    size_t player_count,
    ArvPacketHandle packet) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (player_count > 0U && players == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "players is null");
  }
  ArvResult sync = require_sync_context(ctx, "NetworkAPI");
  if (sync.code != ARV_RESULT_OK) {
    return sync;
  }
  std::vector<PlayerID> player_ids;
  player_ids.reserve(player_count);
  for (size_t index = 0; index < player_count; ++index) {
    player_ids.push_back(PlayerID{players[index]});
  }
  PluginNetworkSendRequest request;
  request.target_kind = PluginNetworkTargetKind::Players;
  request.players = std::span<const PlayerID>{player_ids.data(), player_ids.size()};
  request.packet = packet_from_handle(ctx, packet);
  return send_network_request(ctx, request);
}

ArvResult api_network_broadcast(void* context, ArvPacketHandle packet) {
  PluginCallContext* ctx = call_context(context);
  ArvResult sync = require_sync_context(ctx, "NetworkAPI");
  if (sync.code != ARV_RESULT_OK) {
    return sync;
  }
  PluginNetworkSendRequest request;
  request.target_kind = PluginNetworkTargetKind::Broadcast;
  request.packet = packet_from_handle(ctx, packet);
  return send_network_request(ctx, request);
}

ArvResult api_network_broadcast_world(
    void* context,
    ArvWorldHandle world,
    ArvPacketHandle packet) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (world > static_cast<ArvWorldHandle>(
                  std::numeric_limits<WorldID::value_type>::max())) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "world handle is out of range");
  }
  ArvResult sync = require_sync_context(ctx, "NetworkAPI");
  if (sync.code != ARV_RESULT_OK) {
    return sync;
  }
  PluginNetworkSendRequest request;
  request.target_kind = PluginNetworkTargetKind::World;
  request.world = WorldID{static_cast<WorldID::value_type>(world)};
  request.packet = packet_from_handle(ctx, packet);
  return send_network_request(ctx, request);
}

void api_network_release_packet(void* context, ArvPacketHandle packet) {
  PluginCallContext* ctx = call_context(context);
  if (ctx != nullptr && packet.value != 0U) {
    ctx->network_packets.erase(packet.value);
  }
}

} // namespace

void fill_network_api(PluginAPIStorage& storage) {
  storage.network_api = {
      &unsupported_no_arg,
      &api_network_create_chat_message,
      &api_network_create_kick_player,
      &api_network_send_to_player,
      &api_network_send_to_players,
      &api_network_broadcast,
      &api_network_broadcast_world,
      &api_network_release_packet,
  };
}

} // namespace arv::plugin
