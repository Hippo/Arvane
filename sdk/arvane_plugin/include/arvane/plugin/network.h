#pragma once

#include "arvane/plugin/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvPluginNetworkAPI {
  ArvPluginResult (*unsupported)(void* ctx);
} ArvPluginNetworkAPI;

#ifdef __cplusplus
}
#endif
