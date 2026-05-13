#pragma once

#include "arv/result.h"
#include "arv/resource_id.h"
#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvPersistentDataAPI {
  ArvResult (*unsupported)(void* ctx);
  ArvResult (*set_world_data)(
      void* ctx,
      ArvWorldHandle world,
      ArvResourceID key,
      ArvBytesView bytes);
  ArvResult (*get_world_data)(
      void* ctx,
      ArvWorldHandle world,
      ArvResourceID key,
      ArvByteBuffer* out_buffer);
  ArvResult (*remove_world_data)(
      void* ctx,
      ArvWorldHandle world,
      ArvResourceID key);
  ArvResult (*set_entity_data)(
      void* ctx,
      ArvEntityHandle entity,
      ArvResourceID key,
      ArvBytesView bytes);
  ArvResult (*get_entity_data)(
      void* ctx,
      ArvEntityHandle entity,
      ArvResourceID key,
      ArvByteBuffer* out_buffer);
  ArvResult (*remove_entity_data)(
      void* ctx,
      ArvEntityHandle entity,
      ArvResourceID key);
  ArvResult (*set_block_data)(
      void* ctx,
      ArvWorldHandle world,
      ArvBlockPos pos,
      ArvResourceID key,
      ArvBytesView bytes);
  ArvResult (*get_block_data)(
      void* ctx,
      ArvWorldHandle world,
      ArvBlockPos pos,
      ArvResourceID key,
      ArvByteBuffer* out_buffer);
  ArvResult (*remove_block_data)(
      void* ctx,
      ArvWorldHandle world,
      ArvBlockPos pos,
      ArvResourceID key);
} ArvPersistentDataAPI;

#ifdef __cplusplus
}
#endif
