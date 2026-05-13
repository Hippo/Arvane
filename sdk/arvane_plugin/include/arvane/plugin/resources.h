#pragma once

#include "arvane/plugin/resource_id.h"
#include "arvane/plugin/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArvPluginResourceKind {
  ARV_PLUGIN_RESOURCE_DATA = 0,
  ARV_PLUGIN_RESOURCE_ASSET = 1,
  ARV_PLUGIN_RESOURCE_CONFIG = 2
} ArvPluginResourceKind;

typedef struct ArvResourceAPI {
  bool (*exists)(
      void* ctx,
      ArvPluginResourceKind kind,
      ArvResourceID resource_id,
      ArvStringView extension);
  ArvPluginResult (*read)(
      void* ctx,
      ArvPluginResourceKind kind,
      ArvResourceID resource_id,
      ArvStringView extension,
      ArvPluginByteBuffer* out_buffer);
  ArvPluginResult (*debug_resolved_path)(
      void* ctx,
      ArvPluginResourceKind kind,
      ArvResourceID resource_id,
      ArvStringView extension,
      ArvPluginByteBuffer* out_buffer);
  void (*release_buffer)(void* ctx, ArvPluginByteBuffer buffer);
} ArvResourceAPI;

typedef ArvResourceAPI ArvPluginResourcesAPI;

#ifdef __cplusplus
}
#endif
