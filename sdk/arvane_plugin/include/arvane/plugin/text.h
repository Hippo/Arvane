#pragma once

#include "arvane/plugin/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvPluginTextAPI {
  ArvPluginResult (*unsupported)(void* ctx);
} ArvPluginTextAPI;

#ifdef __cplusplus
}
#endif