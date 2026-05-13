#pragma once

#include "arvane/plugin/resource_id.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvPluginMetricsAPI {
  void (*counter_add)(void* ctx, ArvResourceID metric_id, int64_t amount);
} ArvPluginMetricsAPI;

#ifdef __cplusplus
}
#endif
