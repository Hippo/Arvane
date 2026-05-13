#pragma once

#include "arvane/plugin/resource_id.h"
#include "arvane/plugin/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArvPluginEventPriority {
  ARV_PLUGIN_EVENT_PRIORITY_LOW = 0,
  ARV_PLUGIN_EVENT_PRIORITY_NORMAL = 1,
  ARV_PLUGIN_EVENT_PRIORITY_HIGH = 2,
  ARV_PLUGIN_EVENT_PRIORITY_MONITOR = 3
} ArvPluginEventPriority;

typedef struct ArvPluginEvent {
  ArvResourceID event_id;
  ArvTickIndex tick;
  ArvWorldHandle world;
  ArvBytesView payload;
} ArvPluginEvent;

typedef void (*ArvPluginEventCallback)(
    const ArvPluginEvent* event,
    void* user_data);

typedef struct ArvEventAPI {
  ArvPluginResult (*subscribe)(
      void* ctx,
      ArvResourceID event_id,
      ArvPluginEventPriority priority,
      ArvPluginEventCallback callback,
      void* user_data,
      ArvEventSubscriptionHandle* out_handle);
  ArvPluginResult (*unsubscribe)(
      void* ctx,
      ArvEventSubscriptionHandle handle);
} ArvEventAPI;

typedef ArvEventAPI ArvPluginEventsAPI;

#ifdef __cplusplus
}
#endif
