#pragma once

#include "arvane/plugin/resource_id.h"
#include "arvane/plugin/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvPluginConfigAPI {
  ArvPluginResult (*load_plugin_config)(
      void* ctx,
      ArvResourceID config_id,
      ArvPluginByteBuffer* out_buffer);
  ArvPluginResult (*save_plugin_config)(
      void* ctx,
      ArvResourceID config_id,
      ArvBytesView bytes);
} ArvPluginConfigAPI;

#ifdef __cplusplus
}
#endif
