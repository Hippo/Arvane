#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#  if defined(ARV_PLUGIN_BUILD)
#    define ARV_PLUGIN_EXPORT __declspec(dllexport)
#  else
#    define ARV_PLUGIN_EXPORT __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define ARV_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#  define ARV_PLUGIN_EXPORT
#endif

#define ARV_PLUGIN_API_VERSION 1u

typedef enum ArvPluginResultCode {
  ARV_PLUGIN_RESULT_OK = 0,
  ARV_PLUGIN_RESULT_INVALID_ARGUMENT = 1,
  ARV_PLUGIN_RESULT_INVALID_STATE = 2,
  ARV_PLUGIN_RESULT_PERMISSION_DENIED = 3,
  ARV_PLUGIN_RESULT_NOT_FOUND = 4,
  ARV_PLUGIN_RESULT_DUPLICATE_ID = 5,
  ARV_PLUGIN_RESULT_UNSUPPORTED_VERSION = 6,
  ARV_PLUGIN_RESULT_THREAD_VIOLATION = 7,
  ARV_PLUGIN_RESULT_UNSUPPORTED = 8,
  ARV_PLUGIN_RESULT_INTERNAL_ERROR = 9
} ArvPluginResultCode;

typedef struct ArvPluginResult {
  ArvPluginResultCode code;
  const char* message;
} ArvPluginResult;

typedef struct ArvScheduledJobHandle {
  uint64_t value;
} ArvScheduledJobHandle;

typedef struct ArvServiceHandle {
  uint64_t value;
} ArvServiceHandle;

typedef struct ArvEventSubscriptionHandle {
  uint64_t value;
} ArvEventSubscriptionHandle;

typedef uint32_t ArvWorldHandle;
typedef uint64_t ArvEntityHandle;

typedef struct ArvPluginVec3 {
  float x;
  float y;
  float z;
} ArvPluginVec3;

typedef struct ArvPluginBlockPos {
  int32_t x;
  int32_t y;
  int32_t z;
} ArvPluginBlockPos;

typedef struct ArvPluginByteBuffer {
  uint8_t* data;
  uint64_t size;
} ArvPluginByteBuffer;

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
    ArvPluginCommandContext* command_context,
    ArvPluginCommandMessageKind kind,
    const char* message);
typedef ArvPluginResult (*ArvPluginCommandCallback)(
    ArvPluginCommandContext* command_context,
    void* user_data);

struct ArvPluginCommandContext {
  uint32_t arg_count;
  const char* const* args;
  void* engine_context;
  ArvPluginCommandReplyFn reply;
};

typedef struct ArvPluginCommandDescriptor {
  const char* executor_id;
  const char* command_name;
  ArvPluginCommandDomain domain;
  const char* permission;
  const char* description;
  const char* usage;
  bool public_to_players;
  bool console_only;
  ArvPluginCommandCallback execute;
  void* user_data;
} ArvPluginCommandDescriptor;

typedef void (*ArvPluginJobCallback)(void* user_data);

typedef enum ArvPluginEventPriority {
  ARV_PLUGIN_EVENT_PRIORITY_LOW = 0,
  ARV_PLUGIN_EVENT_PRIORITY_NORMAL = 1,
  ARV_PLUGIN_EVENT_PRIORITY_HIGH = 2,
  ARV_PLUGIN_EVENT_PRIORITY_MONITOR = 3
} ArvPluginEventPriority;

typedef struct ArvPluginEvent {
  const char* event_id;
  uint64_t tick;
  ArvWorldHandle world;
  const void* payload;
  uint64_t payload_size;
} ArvPluginEvent;

typedef void (*ArvPluginEventCallback)(
    const ArvPluginEvent* event,
    void* user_data);

typedef enum ArvPluginServiceThreading {
  ARV_PLUGIN_SERVICE_SYNC_ONLY = 0,
  ARV_PLUGIN_SERVICE_ASYNC_SAFE = 1,
  ARV_PLUGIN_SERVICE_ANY_THREAD = 2
} ArvPluginServiceThreading;

typedef struct ArvPluginServiceDescriptor {
  const char* service_id;
  uint32_t api_version;
  int32_t priority;
  ArvPluginServiceThreading threading;
  const char* display_name;
  const void* api_table;
  void* user_data;
} ArvPluginServiceDescriptor;

typedef struct ArvPluginServiceRef {
  ArvServiceHandle handle;
  const char* service_id;
  uint32_t api_version;
  const char* provider_pack_id;
  const void* api_table;
  void* user_data;
} ArvPluginServiceRef;

typedef enum ArvPluginResourceKind {
  ARV_PLUGIN_RESOURCE_DATA = 0,
  ARV_PLUGIN_RESOURCE_ASSET = 1,
  ARV_PLUGIN_RESOURCE_CONFIG = 2
} ArvPluginResourceKind;

typedef struct ArvPluginContextAPI {
  void* context;
  const char* (*pack_id)(void* context);
  const char* (*pack_version)(void* context);
  const char* (*side)(void* context);
  uint32_t (*namespace_count)(void* context);
  const char* (*namespace_at)(void* context, uint32_t index);
  bool (*owns_namespace)(void* context, const char* name_space);
} ArvPluginContextAPI;

typedef struct ArvPluginLoggerAPI {
  void* context;
  void (*trace)(void* context, const char* message);
  void (*debug)(void* context, const char* message);
  void (*info)(void* context, const char* message);
  void (*warn)(void* context, const char* message);
  void (*error)(void* context, const char* message);
} ArvPluginLoggerAPI;

typedef struct ArvPluginRegistriesAPI {
  void* context;
  ArvPluginResult (*register_block_behavior)(void* context, const char* id);
  ArvPluginResult (*register_item_behavior)(void* context, const char* id);
  ArvPluginResult (*register_entity_goal)(void* context, const char* id);
  ArvPluginResult (*register_entity_behavior)(void* context, const char* id);
  ArvPluginResult (*register_component_codec)(void* context, const char* id);
  ArvPluginResult (*register_loot_function)(void* context, const char* id);
  ArvPluginResult (*register_recipe_type)(void* context, const char* id);
} ArvPluginRegistriesAPI;

typedef struct ArvPluginCommandsAPI {
  void* context;
  ArvPluginResult (*register_executor)(
      void* context,
      const ArvPluginCommandDescriptor* descriptor);
} ArvPluginCommandsAPI;

typedef struct ArvPluginEventsAPI {
  void* context;
  ArvPluginResult (*subscribe)(
      void* context,
      const char* event_id,
      ArvPluginEventPriority priority,
      ArvPluginEventCallback callback,
      void* user_data,
      ArvEventSubscriptionHandle* out_handle);
  ArvPluginResult (*unsubscribe)(
      void* context,
      ArvEventSubscriptionHandle handle);
} ArvPluginEventsAPI;

typedef struct ArvPluginSchedulerAPI {
  void* context;
  ArvPluginResult (*sync_now)(
      void* context,
      ArvPluginJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvPluginResult (*sync_delayed)(
      void* context,
      uint64_t delay_ticks,
      ArvPluginJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvPluginResult (*sync_repeating)(
      void* context,
      uint64_t start_delay_ticks,
      uint64_t interval_ticks,
      ArvPluginJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvPluginResult (*async_now)(
      void* context,
      ArvPluginJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvPluginResult (*async_delayed)(
      void* context,
      uint64_t delay_ticks,
      ArvPluginJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvPluginResult (*async_repeating)(
      void* context,
      uint64_t start_delay_ticks,
      uint64_t interval_ticks,
      ArvPluginJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvPluginResult (*cancel)(void* context, ArvScheduledJobHandle handle);
} ArvPluginSchedulerAPI;

typedef struct ArvPluginServicesAPI {
  void* context;
  ArvPluginResult (*publish)(
      void* context,
      const ArvPluginServiceDescriptor* descriptor,
      ArvServiceHandle* out_handle);
  ArvPluginResult (*unpublish)(void* context, ArvServiceHandle handle);
  ArvPluginResult (*get)(
      void* context,
      const char* service_id,
      uint32_t min_api_version,
      uint32_t max_api_version,
      ArvPluginServiceRef* out_service);
  uint64_t (*list)(
      void* context,
      const char* service_id,
      uint32_t min_api_version,
      uint32_t max_api_version,
      ArvPluginServiceRef* out_services,
      uint64_t max_services);
  void (*release)(void* context, ArvPluginServiceRef service);
} ArvPluginServicesAPI;

typedef struct ArvPluginResourcesAPI {
  void* context;
  bool (*exists)(
      void* context,
      ArvPluginResourceKind kind,
      const char* resource_id,
      const char* extension);
  ArvPluginResult (*read)(
      void* context,
      ArvPluginResourceKind kind,
      const char* resource_id,
      const char* extension,
      ArvPluginByteBuffer* out_buffer);
  ArvPluginResult (*debug_resolved_path)(
      void* context,
      ArvPluginResourceKind kind,
      const char* resource_id,
      const char* extension,
      ArvPluginByteBuffer* out_buffer);
  void (*release_buffer)(void* context, ArvPluginByteBuffer buffer);
} ArvPluginResourcesAPI;

typedef struct ArvPluginWorldsAPI {
  void* context;
  ArvPluginResult (*get_block)(
      void* context,
      ArvWorldHandle world,
      ArvPluginBlockPos pos,
      uint32_t* out_block_variant);
  ArvPluginResult (*set_block)(
      void* context,
      ArvWorldHandle world,
      ArvPluginBlockPos pos,
      uint32_t block_variant);
  ArvPluginResult (*current_tick)(
      void* context,
      ArvWorldHandle world,
      uint64_t* out_tick);
  ArvPluginResult (*world_seed)(
      void* context,
      ArvWorldHandle world,
      uint64_t* out_seed);
} ArvPluginWorldsAPI;

typedef struct ArvPluginEntitiesAPI {
  void* context;
  ArvPluginResult (*spawn)(
      void* context,
      ArvWorldHandle world,
      const char* entity_type,
      ArvPluginVec3 position,
      ArvEntityHandle* out_entity);
  ArvPluginResult (*destroy)(void* context, ArvEntityHandle entity);
  bool (*exists)(void* context, ArvEntityHandle entity);
  ArvPluginResult (*world)(
      void* context,
      ArvEntityHandle entity,
      ArvWorldHandle* out_world);
  ArvPluginResult (*position)(
      void* context,
      ArvEntityHandle entity,
      ArvPluginVec3* out_position);
  ArvPluginResult (*set_position)(
      void* context,
      ArvEntityHandle entity,
      ArvPluginVec3 position);
  ArvPluginResult (*add_tag)(void* context, ArvEntityHandle entity, const char* tag);
  ArvPluginResult (*remove_tag)(void* context, ArvEntityHandle entity, const char* tag);
  ArvPluginResult (*has_tag)(
      void* context,
      ArvEntityHandle entity,
      const char* tag,
      bool* out_has_tag);
} ArvPluginEntitiesAPI;

typedef struct ArvPluginConfigAPI {
  void* context;
  ArvPluginResult (*load_plugin_config)(
      void* context,
      const char* config_id,
      ArvPluginByteBuffer* out_buffer);
  ArvPluginResult (*save_plugin_config)(
      void* context,
      const char* config_id,
      const uint8_t* bytes,
      uint64_t size);
} ArvPluginConfigAPI;

typedef struct ArvPluginPersistentDataAPI {
  void* context;
  ArvPluginResult (*unsupported)(void* context);
} ArvPluginPersistentDataAPI;

typedef struct ArvPluginWorldGenAPI {
  void* context;
  ArvPluginResult (*register_feature_type)(void* context, const char* id);
  ArvPluginResult (*register_noise_type)(void* context, const char* id);
  ArvPluginResult (*register_density_function_type)(void* context, const char* id);
} ArvPluginWorldGenAPI;

typedef struct ArvPluginNetworkAPI {
  void* context;
  ArvPluginResult (*unsupported)(void* context);
} ArvPluginNetworkAPI;

typedef struct ArvPluginTextAPI {
  void* context;
  ArvPluginResult (*unsupported)(void* context);
} ArvPluginTextAPI;

typedef struct ArvPluginMetricsAPI {
  void* context;
  void (*counter_add)(void* context, const char* metric_id, int64_t amount);
} ArvPluginMetricsAPI;

typedef struct ArvPluginPermissionsAPI {
  void* context;
  ArvPluginResult (*unsupported)(void* context);
} ArvPluginPermissionsAPI;

typedef struct ArvPluginAPI {
  uint32_t api_version;
  const ArvPluginContextAPI* context;
  const ArvPluginRegistriesAPI* registries;
  const ArvPluginResourcesAPI* resources;
  const ArvPluginWorldsAPI* worlds;
  const ArvPluginEntitiesAPI* entities;
  const ArvPluginEventsAPI* events;
  const ArvPluginSchedulerAPI* scheduler;
  const ArvPluginCommandsAPI* commands;
  const ArvPluginPermissionsAPI* permissions;
  const ArvPluginServicesAPI* services;
  const ArvPluginConfigAPI* config;
  const ArvPluginPersistentDataAPI* persistent_data;
  const ArvPluginWorldGenAPI* worldgen;
  const ArvPluginNetworkAPI* network;
  const ArvPluginTextAPI* text;
  const ArvPluginMetricsAPI* metrics;
  const ArvPluginLoggerAPI* logger;
} ArvPluginAPI;

typedef bool (*ArvPluginEntrypoint)(const ArvPluginAPI* api);

#ifdef __cplusplus
}
#endif
