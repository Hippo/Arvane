#pragma once

#include "arv/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvNetworkAPI {
  ArvResult (*unsupported)(void* ctx);
} ArvNetworkAPI;

#ifdef __cplusplus
}
#endif
