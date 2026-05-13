#pragma once

#include "arv/resource_id.h"
#include "arv/result.h"
#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvEntityAPI {
  ArvResult (*spawn)(
      void* ctx,
      ArvWorldHandle world,
      ArvResourceID entity_type,
      ArvVec3 position,
      ArvEntityHandle* out_entity);
  ArvResult (*destroy)(void* ctx, ArvEntityHandle entity);
  bool (*exists)(void* ctx, ArvEntityHandle entity);
  ArvResult (*world)(
      void* ctx,
      ArvEntityHandle entity,
      ArvWorldHandle* out_world);
  ArvResult (*position)(
      void* ctx,
      ArvEntityHandle entity,
      ArvVec3* out_position);
  ArvResult (*set_position)(
      void* ctx,
      ArvEntityHandle entity,
      ArvVec3 position);
  ArvResult (*add_tag)(void* ctx, ArvEntityHandle entity, ArvResourceID tag);
  ArvResult (*remove_tag)(void* ctx, ArvEntityHandle entity, ArvResourceID tag);
  ArvResult (*has_tag)(
      void* ctx,
      ArvEntityHandle entity,
      ArvResourceID tag,
      bool* out_has_tag);
} ArvEntityAPI;

#ifdef __cplusplus
}
#endif
