#pragma once

#include "arvane/plugin/resource_id.h"
#include "arvane/plugin/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArvPluginServiceThreading {
  ARV_PLUGIN_SERVICE_SYNC_ONLY = 0,
  ARV_PLUGIN_SERVICE_ASYNC_SAFE = 1,
  ARV_PLUGIN_SERVICE_ANY_THREAD = 2
} ArvPluginServiceThreading;

typedef ArvPluginServiceThreading ArvServiceThreading;

typedef struct ArvPluginServiceDescriptor {
  ArvResourceID service_id;
  uint32_t api_version;
  int32_t priority;
  ArvPluginServiceThreading threading;
  ArvStringView display_name;
  const void* api_table;
  void* user_data;
} ArvPluginServiceDescriptor;

typedef struct ArvPluginServiceRef {
  ArvServiceHandle handle;
  ArvResourceID service_id;
  uint32_t api_version;
  ArvStringView provider_pack_id;
  const void* api_table;
  void* user_data;
  bool valid;
} ArvPluginServiceRef;

typedef struct ArvServiceAPI {
  ArvPluginResult (*publish)(
      void* ctx,
      const ArvPluginServiceDescriptor* descriptor,
      ArvServiceHandle* out_handle);
  ArvPluginResult (*unpublish)(void* ctx, ArvServiceHandle handle);
  ArvPluginResult (*get)(
      void* ctx,
      ArvResourceID service_id,
      uint32_t min_api_version,
      uint32_t max_api_version,
      ArvPluginServiceRef* out_service);
  uint64_t (*list)(
      void* ctx,
      ArvResourceID service_id,
      uint32_t min_api_version,
      uint32_t max_api_version,
      ArvPluginServiceRef* out_services,
      uint64_t max_services);
  void (*release)(void* ctx, ArvPluginServiceRef service);
} ArvServiceAPI;

typedef ArvServiceAPI ArvPluginServicesAPI;

#ifdef __cplusplus
}
#endif
