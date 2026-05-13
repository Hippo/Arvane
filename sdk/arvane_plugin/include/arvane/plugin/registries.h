#pragma once

#include "arvane/plugin/resource_id.h"
#include "arvane/plugin/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvRegistryAPI {
  ArvPluginResult (*register_block_behavior)(void* ctx, ArvResourceID id);
  ArvPluginResult (*register_item_behavior)(void* ctx, ArvResourceID id);
  ArvPluginResult (*register_entity_goal)(void* ctx, ArvResourceID id);
  ArvPluginResult (*register_entity_behavior)(void* ctx, ArvResourceID id);
  ArvPluginResult (*register_component_codec)(void* ctx, ArvResourceID id);
  ArvPluginResult (*register_loot_function)(void* ctx, ArvResourceID id);
  ArvPluginResult (*register_recipe_type)(void* ctx, ArvResourceID id);
} ArvRegistryAPI;

typedef ArvRegistryAPI ArvPluginRegistriesAPI;

#ifdef __cplusplus
}
#endif
