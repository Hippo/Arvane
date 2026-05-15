#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

[[nodiscard]] const text::ChatMessageTemplate* require_chat_message(
    PluginCallContext* ctx,
    ArvChatMessageHandle handle,
    ArvResult& out_error) {
  text::ChatMessageTemplate* message = chat_message_from_handle(ctx, handle);
  if (message == nullptr) {
    out_error = make_result(ctx, ARV_RESULT_NOT_FOUND, "chat message handle not found");
    return nullptr;
  }
  out_error = ok(ctx);
  return message;
}

[[nodiscard]] text::TextVariableResolver resolver_for(
    PluginCallContext* ctx,
    std::optional<PlayerID> player_id = std::nullopt) {
  return {
      ctx == nullptr ? nullptr : ctx->impl->runtime.text_variables,
      player_id,
  };
}

[[nodiscard]] ArvResult validate_chat_channel_and_kind(
    PluginCallContext* ctx,
    ArvStringView channel_view,
    ArvChatMessageKind kind,
    std::string& out_channel,
    protocol::ChatMessageKind& out_kind) {
  out_channel = abi_string(channel_view);
  if (out_channel.empty()) {
    out_channel = std::string{protocol::ChatChannelSystem};
  }
  if (out_channel.size() > protocol::MaxChatChannelBytes) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "chat channel is too long");
  }
  if (std::optional<std::string> error = validate_resource_id(out_channel);
      error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, *error);
  }
  std::optional<protocol::ChatMessageKind> parsed_kind =
      chat_message_kind_from_abi(kind);
  if (!parsed_kind.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "invalid chat message kind");
  }
  out_kind = *parsed_kind;
  return ok(ctx);
}

[[nodiscard]] ArvResult send_chat_message_request(
    PluginCallContext* ctx,
    PluginChatMessageSendRequest request) {
  ArvResult sync = require_sync_context(ctx, "ChatMessageAPI");
  if (sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (ctx->impl->runtime.chat_message_send == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_STATE,
        "chat message sender is unavailable");
  }
  if (request.message == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "chat message handle not found");
  }
  PluginNetworkSendResult result = ctx->impl->runtime.chat_message_send(request);
  if (!result) {
    return make_result(ctx, result.error().code, result.error().message);
  }
  return ok(ctx);
}

ArvResult api_chat_message_parse(
    void* context,
    ArvStringView source,
    ArvChatMessageHandle* out_message) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_message == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_message is null");
  }
  *out_message =
      store_chat_message(ctx, text::ChatMessageTemplate::parse(abi_string(source)));
  return ok(ctx);
}

ArvResult api_chat_message_from_data(
    void* context,
    ArvDataDocumentHandle document,
    ArvStringView path,
    ArvChatMessageHandle* out_message) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_message == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_message is null");
  }
  *out_message = {};
  const io::DataDocument* data = document_from_handle(ctx, document);
  if (data == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "data document handle not found");
  }
  auto message = data->value_as<text::ChatMessageTemplate>(abi_view(path));
  if (!message) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, message.error().describe());
  }
  *out_message = store_chat_message(ctx, std::move(message.value()));
  return ok(ctx);
}

ArvResult api_chat_message_to_string(
    void* context,
    ArvChatMessageHandle message_handle,
    ArvByteBuffer* out_buffer) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_buffer == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_buffer is null");
  }
  *out_buffer = {};
  ArvResult error{};
  const text::ChatMessageTemplate* message =
      require_chat_message(ctx, message_handle, error);
  if (message == nullptr) {
    return error;
  }
  *out_buffer = byte_buffer_from_string(message->to_string(resolver_for(ctx)));
  return ok(ctx);
}

ArvResult api_chat_message_to_string_for(
    void* context,
    ArvChatMessageHandle message_handle,
    ArvPlayerID player,
    ArvByteBuffer* out_buffer) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_buffer == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_buffer is null");
  }
  *out_buffer = {};
  ArvResult error{};
  const text::ChatMessageTemplate* message =
      require_chat_message(ctx, message_handle, error);
  if (message == nullptr) {
    return error;
  }
  *out_buffer =
      byte_buffer_from_string(message->to_string(resolver_for(ctx, PlayerID{player})));
  return ok(ctx);
}

ArvResult api_chat_message_as_text(
    void* context,
    ArvChatMessageHandle message_handle,
    ArvTextHandle* out_text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_text == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_text is null");
  }
  *out_text = {};
  ArvResult error{};
  const text::ChatMessageTemplate* message =
      require_chat_message(ctx, message_handle, error);
  if (message == nullptr) {
    return error;
  }
  *out_text = store_text(ctx, message->to_rich_text(resolver_for(ctx)));
  return ok(ctx);
}

ArvResult api_chat_message_as_text_for(
    void* context,
    ArvChatMessageHandle message_handle,
    ArvPlayerID player,
    ArvTextHandle* out_text) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (out_text == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_text is null");
  }
  *out_text = {};
  ArvResult error{};
  const text::ChatMessageTemplate* message =
      require_chat_message(ctx, message_handle, error);
  if (message == nullptr) {
    return error;
  }
  *out_text = store_text(ctx, message->to_rich_text(resolver_for(ctx, PlayerID{player})));
  return ok(ctx);
}

ArvResult api_chat_message_send_to(
    void* context,
    ArvChatMessageHandle message_handle,
    ArvPlayerID player,
    ArvStringView channel,
    ArvChatMessageKind kind) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  ArvResult error{};
  const text::ChatMessageTemplate* message =
      require_chat_message(ctx, message_handle, error);
  if (message == nullptr) {
    return error;
  }
  std::string channel_id;
  protocol::ChatMessageKind message_kind{};
  ArvResult channel_result =
      validate_chat_channel_and_kind(ctx, channel, kind, channel_id, message_kind);
  if (channel_result.code != ARV_RESULT_OK) {
    return channel_result;
  }
  PluginChatMessageSendRequest request;
  request.target_kind = PluginNetworkTargetKind::Player;
  request.player = PlayerID{player};
  request.message = message;
  request.channel = std::move(channel_id);
  request.kind = message_kind;
  return send_chat_message_request(ctx, request);
}

ArvResult api_chat_message_send_to_players(
    void* context,
    ArvChatMessageHandle message_handle,
    const ArvPlayerID* players,
    size_t player_count,
    ArvStringView channel,
    ArvChatMessageKind kind) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (player_count > 0U && players == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "players is null");
  }
  ArvResult error{};
  const text::ChatMessageTemplate* message =
      require_chat_message(ctx, message_handle, error);
  if (message == nullptr) {
    return error;
  }
  std::string channel_id;
  protocol::ChatMessageKind message_kind{};
  ArvResult channel_result =
      validate_chat_channel_and_kind(ctx, channel, kind, channel_id, message_kind);
  if (channel_result.code != ARV_RESULT_OK) {
    return channel_result;
  }
  std::vector<PlayerID> player_ids;
  player_ids.reserve(player_count);
  for (size_t index = 0; index < player_count; ++index) {
    player_ids.push_back(PlayerID{players[index]});
  }
  PluginChatMessageSendRequest request;
  request.target_kind = PluginNetworkTargetKind::Players;
  request.players = std::span<const PlayerID>{player_ids.data(), player_ids.size()};
  request.message = message;
  request.channel = std::move(channel_id);
  request.kind = message_kind;
  return send_chat_message_request(ctx, request);
}

ArvResult api_chat_message_broadcast(
    void* context,
    ArvChatMessageHandle message_handle,
    ArvStringView channel,
    ArvChatMessageKind kind) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  ArvResult error{};
  const text::ChatMessageTemplate* message =
      require_chat_message(ctx, message_handle, error);
  if (message == nullptr) {
    return error;
  }
  std::string channel_id;
  protocol::ChatMessageKind message_kind{};
  ArvResult channel_result =
      validate_chat_channel_and_kind(ctx, channel, kind, channel_id, message_kind);
  if (channel_result.code != ARV_RESULT_OK) {
    return channel_result;
  }
  PluginChatMessageSendRequest request;
  request.target_kind = PluginNetworkTargetKind::Broadcast;
  request.message = message;
  request.channel = std::move(channel_id);
  request.kind = message_kind;
  return send_chat_message_request(ctx, request);
}

ArvResult api_chat_message_broadcast_world(
    void* context,
    ArvChatMessageHandle message_handle,
    ArvWorldHandle world,
    ArvStringView channel,
    ArvChatMessageKind kind) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(ARV_RESULT_INVALID_STATE, "plugin API context is null");
  }
  if (world > static_cast<ArvWorldHandle>(
                  std::numeric_limits<WorldID::value_type>::max())) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "world handle is out of range");
  }
  ArvResult error{};
  const text::ChatMessageTemplate* message =
      require_chat_message(ctx, message_handle, error);
  if (message == nullptr) {
    return error;
  }
  std::string channel_id;
  protocol::ChatMessageKind message_kind{};
  ArvResult channel_result =
      validate_chat_channel_and_kind(ctx, channel, kind, channel_id, message_kind);
  if (channel_result.code != ARV_RESULT_OK) {
    return channel_result;
  }
  PluginChatMessageSendRequest request;
  request.target_kind = PluginNetworkTargetKind::World;
  request.world = WorldID{static_cast<WorldID::value_type>(world)};
  request.message = message;
  request.channel = std::move(channel_id);
  request.kind = message_kind;
  return send_chat_message_request(ctx, request);
}

void api_chat_message_release(void* context, ArvChatMessageHandle message) {
  PluginCallContext* ctx = call_context(context);
  if (ctx != nullptr && message.value != 0U) {
    ctx->chat_messages.erase(message.value);
  }
}

} // namespace

void fill_chat_message_api(PluginAPIStorage& storage) {
  storage.chat_message_api = {
      &api_chat_message_parse,
      &api_chat_message_from_data,
      &api_chat_message_to_string,
      &api_chat_message_to_string_for,
      &api_chat_message_as_text,
      &api_chat_message_as_text_for,
      &api_chat_message_send_to,
      &api_chat_message_send_to_players,
      &api_chat_message_broadcast,
      &api_chat_message_broadcast_world,
      &api_chat_message_release,
  };
}

} // namespace arv::plugin
