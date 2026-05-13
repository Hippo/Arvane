#pragma once

#include "arvane/plugin/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvPluginPermissionsAPI {
  ArvPluginResult (*unsupported)(void* ctx);
} ArvPluginPermissionsAPI;

#ifdef __cplusplus
}
#endif
