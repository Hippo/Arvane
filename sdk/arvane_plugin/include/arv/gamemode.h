#pragma once

#include "arv/permissions.h"
#include "arv/resource_id.h"
#include "arv/result.h"
#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvGameModeDefinition {
  ArvStringView id;
  ArvStringView base_id;
  ArvStringView display_name;
  ArvStringView description;
  ArvCorePermissionSet core;
  const ArvCustomPermissionEntry* custom_permissions;
  size_t custom_permission_count;
} ArvGameModeDefinition;

typedef struct ArvGameModeInfo {
  ArvStringView id;
  ArvStringView display_name;
  bool builtin;
  ArvResolvedCorePermissions core_permissions;
} ArvGameModeInfo;

typedef struct ArvGameModeAPI {
  ArvResult (*register_mode)(
      void* ctx,
      const ArvGameModeDefinition* definition);
  bool (*exists)(
      void* ctx,
      ArvStringView mode_id);
  ArvResult (*info)(
      void* ctx,
      ArvStringView mode_id,
      ArvGameModeInfo* out_info);
  ArvResult (*current)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView* out_mode_id);
  ArvResult (*set)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView mode_id,
      ArvResourceID reason);
  ArvResult (*base_core_permissions)(
      void* ctx,
      ArvStringView mode_id,
      ArvResolvedCorePermissions* out_permissions);
  uint64_t (*list_modes)(
      void* ctx,
      ArvGameModeInfo* out_modes,
      uint64_t max_modes);
} ArvGameModeAPI;

#ifdef __cplusplus
}
#endif
