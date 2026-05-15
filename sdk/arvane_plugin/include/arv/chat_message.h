#pragma once

#include "arv/data.h"
#include "arv/network.h"
#include "arv/result.h"
#include "arv/text.h"
#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvChatMessageHandle {
  uint64_t value;
} ArvChatMessageHandle;

typedef struct ArvChatMessageAPI {
  ArvResult (*parse)(
      void* ctx,
      ArvStringView source,
      ArvChatMessageHandle* out_message);
  ArvResult (*from_data)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      ArvChatMessageHandle* out_message);
  ArvResult (*to_string)(
      void* ctx,
      ArvChatMessageHandle message,
      ArvByteBuffer* out_buffer);
  ArvResult (*to_string_for)(
      void* ctx,
      ArvChatMessageHandle message,
      ArvPlayerID player,
      ArvByteBuffer* out_buffer);
  ArvResult (*as_text)(
      void* ctx,
      ArvChatMessageHandle message,
      ArvTextHandle* out_text);
  ArvResult (*as_text_for)(
      void* ctx,
      ArvChatMessageHandle message,
      ArvPlayerID player,
      ArvTextHandle* out_text);
  ArvResult (*send_to)(
      void* ctx,
      ArvChatMessageHandle message,
      ArvPlayerID player,
      ArvStringView channel,
      ArvChatMessageKind kind);
  ArvResult (*send_to_players)(
      void* ctx,
      ArvChatMessageHandle message,
      const ArvPlayerID* players,
      size_t player_count,
      ArvStringView channel,
      ArvChatMessageKind kind);
  ArvResult (*broadcast)(
      void* ctx,
      ArvChatMessageHandle message,
      ArvStringView channel,
      ArvChatMessageKind kind);
  ArvResult (*broadcast_world)(
      void* ctx,
      ArvChatMessageHandle message,
      ArvWorldHandle world,
      ArvStringView channel,
      ArvChatMessageKind kind);
  void (*release)(
      void* ctx,
      ArvChatMessageHandle message);
} ArvChatMessageAPI;

#ifdef __cplusplus
}
#endif
