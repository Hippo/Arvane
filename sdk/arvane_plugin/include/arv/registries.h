#pragma once

#include "arv/resource_id.h"
#include "arv/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvRegistryAPI {
  ArvResult (*register_block_behavior)(void* ctx, ArvResourceID id);
  ArvResult (*register_item_behavior)(void* ctx, ArvResourceID id);
  ArvResult (*register_entity_goal)(void* ctx, ArvResourceID id);
  ArvResult (*register_entity_behavior)(void* ctx, ArvResourceID id);
  ArvResult (*register_component_codec)(void* ctx, ArvResourceID id);
  ArvResult (*register_loot_function)(void* ctx, ArvResourceID id);
  ArvResult (*register_recipe_type)(void* ctx, ArvResourceID id);
  ArvResult (*find_block_variant)(
      void* ctx,
      ArvResourceID id,
      uint32_t* out_block_variant);
  ArvResult (*block_variant_name)(
      void* ctx,
      uint32_t block_variant,
      ArvStringView* out_name);
  ArvResult (*air_block_variant)(void* ctx, uint32_t* out_block_variant);
  bool (*is_air_block_variant)(void* ctx, uint32_t block_variant);
} ArvRegistryAPI;

#ifdef __cplusplus
}
#endif
