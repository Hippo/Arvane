#pragma once

#include "arv/resource_id.h"
#include "arv/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArvServiceThreading {
  ARV_SERVICE_SYNC_ONLY = 0,
  ARV_SERVICE_ASYNC_SAFE = 1,
  ARV_SERVICE_ANY_THREAD = 2
} ArvServiceThreading;

typedef struct ArvServiceDescriptor {
  ArvResourceID service_id;
  uint32_t api_version;
  int32_t priority;
  ArvServiceThreading threading;
  ArvStringView display_name;
  const void* api_table;
  void* user_data;
} ArvServiceDescriptor;

typedef struct ArvServiceRef {
  ArvServiceHandle handle;
  ArvResourceID service_id;
  uint32_t api_version;
  ArvStringView provider_pack_id;
  const void* api_table;
  void* user_data;
  bool valid;
} ArvServiceRef;

typedef struct ArvServiceAPI {
  ArvResult (*publish)(
      void* ctx,
      const ArvServiceDescriptor* descriptor,
      ArvServiceHandle* out_handle);
  ArvResult (*unpublish)(void* ctx, ArvServiceHandle handle);
  ArvResult (*get)(
      void* ctx,
      ArvResourceID service_id,
      uint32_t min_api_version,
      uint32_t max_api_version,
      ArvServiceRef* out_service);
  uint64_t (*list)(
      void* ctx,
      ArvResourceID service_id,
      uint32_t min_api_version,
      uint32_t max_api_version,
      ArvServiceRef* out_services,
      uint64_t max_services);
  void (*release)(void* ctx, ArvServiceRef service);
} ArvServiceAPI;

#ifdef __cplusplus
}
#endif
