#pragma once

#include "arv/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvPersistentDataAPI {
  ArvResult (*unsupported)(void* ctx);
} ArvPersistentDataAPI;

#ifdef __cplusplus
}
#endif
