#pragma once

#include "arv/resource_id.h"
#include "arv/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArvCommandDomain {
  ARV_COMMAND_CLIENT_BOUND = 0,
  ARV_COMMAND_SERVER_BOUND = 1
} ArvCommandDomain;

typedef enum ArvCommandMessageKind {
  ARV_COMMAND_MESSAGE_INFO = 0,
  ARV_COMMAND_MESSAGE_SUCCESS = 1,
  ARV_COMMAND_MESSAGE_WARNING = 2,
  ARV_COMMAND_MESSAGE_ERROR = 3
} ArvCommandMessageKind;

typedef enum ArvCommandSourceKind {
  ARV_COMMAND_SOURCE_PLAYER = 0,
  ARV_COMMAND_SOURCE_SERVER_CONSOLE = 1
} ArvCommandSourceKind;

typedef struct ArvCommandContext ArvCommandContext;
typedef void (*ArvCommandReplyFn)(
    ArvCommandContext* command_ctx,
    ArvCommandMessageKind kind,
    ArvStringView message);
typedef ArvResult (*ArvCommandCallback)(
    ArvCommandContext* command_ctx,
    void* user_data);

struct ArvCommandContext {
  uint32_t arg_count;
  const ArvStringView* args;
  void* engine_context;
  ArvCommandReplyFn reply;
  ArvCommandSourceKind source_kind;
  bool has_player;
  bool has_entity;
  bool has_world;
  bool has_position;
  ArvPlayerID player;
  ArvEntityID entity;
  ArvWorldHandle world;
  ArvVec3 position;
};

typedef struct ArvCommandDescriptor {
  ArvResourceID executor_id;
  ArvStringView command_name;
  ArvCommandDomain domain;
  ArvStringView permission;
  ArvStringView description;
  ArvStringView usage;
  bool public_to_players;
  bool console_only;
  ArvCommandCallback execute;
  void* user_data;
} ArvCommandDescriptor;

typedef struct ArvCommandAPI {
  ArvResult (*register_executor)(
      void* ctx,
      const ArvCommandDescriptor* descriptor);
} ArvCommandAPI;

#ifdef __cplusplus
}
#endif
