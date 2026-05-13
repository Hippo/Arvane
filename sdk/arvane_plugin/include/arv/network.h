#pragma once

#include "arv/result.h"
#include "arv/text.h"
#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvPacketHandle {
  uint64_t value;
} ArvPacketHandle;

typedef enum ArvChatMessageKind {
  ARV_CHAT_MESSAGE_CHAT = 0,
  ARV_CHAT_MESSAGE_SYSTEM = 1,
  ARV_CHAT_MESSAGE_ANNOUNCEMENT = 2,
  ARV_CHAT_MESSAGE_PRIVATE = 3,
  ARV_CHAT_MESSAGE_ERROR = 4
} ArvChatMessageKind;

typedef struct ArvNetworkAPI {
  ArvResult (*unsupported)(void* ctx);
  ArvResult (*create_clientbound_chat_message)(
      void* ctx,
      ArvTextHandle message,
      ArvStringView channel,
      ArvChatMessageKind kind,
      ArvPacketHandle* out_packet);
  ArvResult (*create_clientbound_kick_player)(
      void* ctx,
      ArvTextHandle reason,
      ArvPacketHandle* out_packet);
  ArvResult (*send_to_player)(
      void* ctx,
      ArvPlayerID player,
      ArvPacketHandle packet);
  ArvResult (*send_to_players)(
      void* ctx,
      const ArvPlayerID* players,
      size_t player_count,
      ArvPacketHandle packet);
  ArvResult (*broadcast)(
      void* ctx,
      ArvPacketHandle packet);
  ArvResult (*broadcast_world)(
      void* ctx,
      ArvWorldHandle world,
      ArvPacketHandle packet);
  void (*release_packet)(
      void* ctx,
      ArvPacketHandle packet);
} ArvNetworkAPI;

#ifdef __cplusplus
}
#endif
