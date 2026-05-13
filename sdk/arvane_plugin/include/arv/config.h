#pragma once

#include "arv/resource_id.h"
#include "arv/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvConfigAPI {
  ArvResult (*load_plugin_config)(
      void* ctx,
      ArvResourceID config_id,
      ArvByteBuffer* out_buffer);
  ArvResult (*save_plugin_config)(
      void* ctx,
      ArvResourceID config_id,
      ArvBytesView bytes);
} ArvConfigAPI;

#ifdef __cplusplus
}
#endif
