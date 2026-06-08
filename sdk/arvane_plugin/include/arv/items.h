#pragma once

#include "arv/data.h"
#include "arv/result.h"
#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArvItemStackLocationKind {
  ARV_ITEM_STACK_SELECTED_HOTBAR = 0,
  ARV_ITEM_STACK_INVENTORY_SLOT = 1,
  ARV_ITEM_STACK_CURSOR = 2
} ArvItemStackLocationKind;

typedef struct ArvItemStackLocation {
  ArvPlayerID player;
  ArvItemStackLocationKind kind;
  uint32_t slot_index;
} ArvItemStackLocation;

typedef struct ArvItemStackInfo {
  bool present;
  uint32_t item_id;
  ArvStringView item_name;
  uint16_t count;
  bool has_data;
} ArvItemStackInfo;

typedef struct ArvItemsAPI {
  ArvResult (*stack_info)(
      void* ctx,
      ArvItemStackLocation location,
      ArvItemStackInfo* out_info);
  ArvResult (*load_stack_data)(
      void* ctx,
      ArvItemStackLocation location,
      ArvDataDocumentHandle* out_document);
  ArvResult (*load_stack_namespace_data)(
      void* ctx,
      ArvItemStackLocation location,
      ArvStringView name_space,
      ArvDataDocumentHandle* out_document);
  ArvResult (*save_stack_namespace_data)(
      void* ctx,
      ArvItemStackLocation location,
      ArvStringView name_space,
      ArvDataDocumentHandle document);
  ArvResult (*clear_stack_namespace_data)(
      void* ctx,
      ArvItemStackLocation location,
      ArvStringView name_space);
  ArvResult (*give_stack)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView item_name,
      uint16_t count,
      ArvStringView name_space,
      ArvDataDocumentHandle namespace_document);
} ArvItemsAPI;

#ifdef __cplusplus
}
#endif
