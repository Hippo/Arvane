#pragma once

#include "arvane/plugin/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvPluginPersistentDataAPI {
  ArvPluginResult (*unsupported)(void* ctx);
} ArvPluginPersistentDataAPI;

#ifdef __cplusplus
}
#endif
