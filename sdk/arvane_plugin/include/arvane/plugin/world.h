#pragma once

#include "arvane/plugin/result.h"
#include "arvane/plugin/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvWorldAPI {
  ArvPluginResult (*get_block)(
      void* ctx,
      ArvWorldHandle world,
      ArvBlockPos pos,
      uint32_t* out_block_variant);
  ArvPluginResult (*set_block)(
      void* ctx,
      ArvWorldHandle world,
      ArvBlockPos pos,
      uint32_t block_variant);
  ArvPluginResult (*current_tick)(
      void* ctx,
      ArvWorldHandle world,
      ArvTickIndex* out_tick);
  ArvPluginResult (*world_seed)(
      void* ctx,
      ArvWorldHandle world,
      uint64_t* out_seed);
} ArvWorldAPI;

typedef ArvWorldAPI ArvPluginWorldsAPI;

#ifdef __cplusplus
}
#endif
