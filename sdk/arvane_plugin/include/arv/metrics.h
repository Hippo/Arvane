#pragma once

#include "arv/resource_id.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvMetricsAPI {
  void (*counter_add)(void* ctx, ArvResourceID metric_id, int64_t amount);
} ArvMetricsAPI;

#ifdef __cplusplus
}
#endif
