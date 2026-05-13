#pragma once

#include "arvane/plugin/resource_id.h"
#include "arvane/plugin/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvPluginWorldGenAPI {
  ArvPluginResult (*register_feature_type)(void* ctx, ArvResourceID id);
  ArvPluginResult (*register_noise_type)(void* ctx, ArvResourceID id);
  ArvPluginResult (*register_density_function_type)(void* ctx, ArvResourceID id);
} ArvPluginWorldGenAPI;

#ifdef __cplusplus
}
#endif
