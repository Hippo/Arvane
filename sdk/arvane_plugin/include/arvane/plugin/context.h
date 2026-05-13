#pragma once

#include "arvane/plugin/resource_id.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvPluginContextAPI {
  ArvStringView (*pack_id)(void* ctx);
  ArvStringView (*pack_version)(void* ctx);
  ArvStringView (*side)(void* ctx);
  uint32_t (*namespace_count)(void* ctx);
  ArvStringView (*namespace_at)(void* ctx, uint32_t index);
  bool (*owns_namespace)(void* ctx, ArvStringView name_space);
} ArvPluginContextAPI;

#ifdef __cplusplus
}
#endif
