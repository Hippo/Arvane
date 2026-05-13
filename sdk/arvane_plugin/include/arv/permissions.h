#pragma once

#include "arv/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvPermissionsAPI {
  ArvResult (*unsupported)(void* ctx);
} ArvPermissionsAPI;

#ifdef __cplusplus
}
#endif
