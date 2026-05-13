#pragma once

#include "arv/resource_id.h"
#include "arv/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvWorldGenAPI {
  ArvResult (*register_feature_type)(void* ctx, ArvResourceID id);
  ArvResult (*register_noise_type)(void* ctx, ArvResourceID id);
  ArvResult (*register_density_function_type)(void* ctx, ArvResourceID id);
} ArvWorldGenAPI;

#ifdef __cplusplus
}
#endif
