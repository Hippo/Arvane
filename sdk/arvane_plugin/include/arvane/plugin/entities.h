#pragma once

#include "arvane/plugin/resource_id.h"
#include "arvane/plugin/result.h"
#include "arvane/plugin/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvEntityAPI {
  ArvPluginResult (*spawn)(
      void* ctx,
      ArvWorldHandle world,
      ArvResourceID entity_type,
      ArvVec3 position,
      ArvEntityHandle* out_entity);
  ArvPluginResult (*destroy)(void* ctx, ArvEntityHandle entity);
  bool (*exists)(void* ctx, ArvEntityHandle entity);
  ArvPluginResult (*world)(
      void* ctx,
      ArvEntityHandle entity,
      ArvWorldHandle* out_world);
  ArvPluginResult (*position)(
      void* ctx,
      ArvEntityHandle entity,
      ArvVec3* out_position);
  ArvPluginResult (*set_position)(
      void* ctx,
      ArvEntityHandle entity,
      ArvVec3 position);
  ArvPluginResult (*add_tag)(void* ctx, ArvEntityHandle entity, ArvResourceID tag);
  ArvPluginResult (*remove_tag)(void* ctx, ArvEntityHandle entity, ArvResourceID tag);
  ArvPluginResult (*has_tag)(
      void* ctx,
      ArvEntityHandle entity,
      ArvResourceID tag,
      bool* out_has_tag);
} ArvEntityAPI;

typedef ArvEntityAPI ArvPluginEntitiesAPI;

#ifdef __cplusplus
}
#endif
