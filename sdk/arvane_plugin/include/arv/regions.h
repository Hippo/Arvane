#pragma once

#include "arv/permissions.h"
#include "arv/resource_id.h"
#include "arv/result.h"
#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvRegionHandle {
  uint64_t value;
} ArvRegionHandle;

typedef enum ArvRegionRuleValue {
  ARV_REGION_RULE_UNSET = 0,
  ARV_REGION_RULE_ALLOW = 1,
  ARV_REGION_RULE_DENY = 2
} ArvRegionRuleValue;

typedef enum ArvRegionShapeKind {
  ARV_REGION_SHAPE_AABB = 0
} ArvRegionShapeKind;

typedef struct ArvRegionAabb {
  ArvVec3 min;
  ArvVec3 max;
} ArvRegionAabb;

typedef struct ArvRegionShape {
  ArvRegionShapeKind kind;
  ArvRegionAabb aabb;
} ArvRegionShape;

typedef struct ArvRegionQuery {
  ArvWorldHandle world;
  ArvVec3 position;
} ArvRegionQuery;

typedef struct ArvRegionDefinition {
  ArvResourceID id;
  ArvWorldHandle world;
  int32_t priority;
  ArvRegionShape shape;
  const ArvStringView* zones;
  size_t zone_count;
  ArvCorePermissionSet core;
  const ArvCustomPermissionEntry* custom_permissions;
  size_t custom_permission_count;
  bool persistent;
} ArvRegionDefinition;

typedef struct ArvZoneDefinition {
  ArvResourceID id;
  int32_t priority;
  ArvCorePermissionSet core;
  const ArvCustomPermissionEntry* custom_permissions;
  size_t custom_permission_count;
} ArvZoneDefinition;

typedef struct ArvRegionFlagDefinition {
  ArvResourceID id;
  ArvStringView description;
  ArvRegionRuleValue default_value;
} ArvRegionFlagDefinition;

typedef struct ArvRegionInfo {
  ArvRegionHandle handle;
  ArvStringView id;
  ArvWorldHandle world;
  int32_t priority;
} ArvRegionInfo;

typedef struct ArvRegionsAPI {
  ArvResult (*register_region_flag)(
      void* ctx,
      const ArvRegionFlagDefinition* definition);
  ArvResult (*create_runtime_region)(
      void* ctx,
      const ArvRegionDefinition* definition,
      ArvRegionHandle* out_handle);
  ArvResult (*remove_runtime_region)(
      void* ctx,
      ArvRegionHandle handle);
  ArvResult (*register_runtime_zone)(
      void* ctx,
      const ArvZoneDefinition* definition,
      ArvRegionHandle* out_handle);
  ArvResult (*remove_runtime_zone)(
      void* ctx,
      ArvRegionHandle handle);
  uint64_t (*find_regions_at)(
      void* ctx,
      const ArvRegionQuery* query,
      ArvRegionInfo* out_regions,
      uint64_t max_regions);
  uint64_t (*active_regions)(
      void* ctx,
      ArvPlayerID player,
      ArvRegionInfo* out_regions,
      uint64_t max_regions);
  bool (*is_player_in_region)(
      void* ctx,
      ArvPlayerID player,
      ArvResourceID region_id);
  ArvResult (*region_flag)(
      void* ctx,
      ArvResourceID region_id,
      ArvResourceID flag,
      ArvRegionRuleValue* out_value);
  ArvResult (*query_flag)(
      void* ctx,
      const ArvRegionQuery* query,
      ArvResourceID flag,
      ArvRegionRuleValue* out_value);
} ArvRegionsAPI;

#ifdef __cplusplus
}
#endif
