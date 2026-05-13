#pragma once

#include "arvane/plugin/resource_id.h"
#include "arvane/plugin/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArvPluginCommandDomain {
  ARV_PLUGIN_COMMAND_CLIENT_BOUND = 0,
  ARV_PLUGIN_COMMAND_SERVER_BOUND = 1
} ArvPluginCommandDomain;

typedef enum ArvPluginCommandMessageKind {
  ARV_PLUGIN_COMMAND_MESSAGE_INFO = 0,
  ARV_PLUGIN_COMMAND_MESSAGE_SUCCESS = 1,
  ARV_PLUGIN_COMMAND_MESSAGE_WARNING = 2,
  ARV_PLUGIN_COMMAND_MESSAGE_ERROR = 3
} ArvPluginCommandMessageKind;

typedef struct ArvPluginCommandContext ArvPluginCommandContext;
typedef void (*ArvPluginCommandReplyFn)(
    ArvPluginCommandContext* command_ctx,
    ArvPluginCommandMessageKind kind,
    ArvStringView message);
typedef ArvPluginResult (*ArvPluginCommandCallback)(
    ArvPluginCommandContext* command_ctx,
    void* user_data);

struct ArvPluginCommandContext {
  uint32_t arg_count;
  const ArvStringView* args;
  void* engine_context;
  ArvPluginCommandReplyFn reply;
};

typedef struct ArvPluginCommandDescriptor {
  ArvResourceID executor_id;
  ArvStringView command_name;
  ArvPluginCommandDomain domain;
  ArvStringView permission;
  ArvStringView description;
  ArvStringView usage;
  bool public_to_players;
  bool console_only;
  ArvPluginCommandCallback execute;
  void* user_data;
} ArvPluginCommandDescriptor;

typedef struct ArvCommandAPI {
  ArvPluginResult (*register_executor)(
      void* ctx,
      const ArvPluginCommandDescriptor* descriptor);
} ArvCommandAPI;

typedef ArvCommandAPI ArvPluginCommandsAPI;

#ifdef __cplusplus
}
#endif
