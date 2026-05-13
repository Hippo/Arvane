#pragma once

#include "arv/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvTextAPI {
  ArvResult (*unsupported)(void* ctx);
} ArvTextAPI;

#ifdef __cplusplus
}
#endif
