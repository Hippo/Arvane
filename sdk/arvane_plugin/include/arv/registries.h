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
} ArvRegistryAPI;

#ifdef __cplusplus
}
#endif
