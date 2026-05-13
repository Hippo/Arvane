#pragma once

#include "arv/resource_id.h"
#include "arv/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArvEventPriority {
  ARV_EVENT_PRIORITY_LOW = 0,
  ARV_EVENT_PRIORITY_NORMAL = 1,
  ARV_EVENT_PRIORITY_HIGH = 2,
  ARV_EVENT_PRIORITY_MONITOR = 3
} ArvEventPriority;

typedef struct ArvEvent {
  ArvResourceID event_id;
  ArvTickIndex tick;
  ArvWorldHandle world;
  ArvBytesView payload;
} ArvEvent;

typedef void (*ArvEventCallback)(
    const ArvEvent* event,
    void* user_data);

typedef struct ArvEventAPI {
  ArvResult (*subscribe)(
      void* ctx,
      ArvResourceID event_id,
      ArvEventPriority priority,
      ArvEventCallback callback,
      void* user_data,
      ArvEventSubscriptionHandle* out_handle);
  ArvResult (*unsubscribe)(
      void* ctx,
      ArvEventSubscriptionHandle handle);
} ArvEventAPI;

#ifdef __cplusplus
}
#endif
