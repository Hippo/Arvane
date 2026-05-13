#pragma once

#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvLoggerAPI {
  void (*trace)(void* ctx, ArvStringView message);
  void (*debug)(void* ctx, ArvStringView message);
  void (*info)(void* ctx, ArvStringView message);
  void (*warn)(void* ctx, ArvStringView message);
  void (*error)(void* ctx, ArvStringView message);
} ArvLoggerAPI;

#ifdef __cplusplus
}
#endif
