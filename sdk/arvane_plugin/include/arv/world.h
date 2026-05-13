#pragma once

#include "arv/result.h"
#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvWorldAPI {
  ArvResult (*get_block)(
      void* ctx,
      ArvWorldHandle world,
      ArvBlockPos pos,
      uint32_t* out_block_variant);
  ArvResult (*set_block)(
      void* ctx,
      ArvWorldHandle world,
      ArvBlockPos pos,
      uint32_t block_variant);
  ArvResult (*current_tick)(
      void* ctx,
      ArvWorldHandle world,
      ArvTickIndex* out_tick);
  ArvResult (*world_seed)(
      void* ctx,
      ArvWorldHandle world,
      uint64_t* out_seed);
} ArvWorldAPI;

#ifdef __cplusplus
}
#endif
