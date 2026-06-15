#pragma once

#include "arv/resource_id.h"
#include "arv/result.h"
#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArvPermissionValue {
  ARV_PERMISSION_UNSET = 0,
  ARV_PERMISSION_ALLOW = 1,
  ARV_PERMISSION_DENY = 2
} ArvPermissionValue;

typedef enum ArvCorePermission {
  ARV_CORE_PERMISSION_CAN_TAKE_DAMAGE = 0,
  ARV_CORE_PERMISSION_CAN_MODIFY_WORLD = 1,
  ARV_CORE_PERMISSION_CAN_INTERACT = 2,
  ARV_CORE_PERMISSION_CAN_OPEN_INVENTORY = 3,
  ARV_CORE_PERMISSION_HAS_CREATIVE_INVENTORY = 4,
  ARV_CORE_PERMISSION_CAN_FLY = 5,
  ARV_CORE_PERMISSION_HAS_COLLISION = 6,
  ARV_CORE_PERMISSION_IS_VISIBLE_TO_OTHERS = 7
} ArvCorePermission;

typedef struct ArvCorePermissionSet {
  ArvPermissionValue can_take_damage;
  ArvPermissionValue can_modify_world;
  ArvPermissionValue can_interact;
  ArvPermissionValue can_open_inventory;
  ArvPermissionValue has_creative_inventory;
  ArvPermissionValue can_fly;
  ArvPermissionValue has_collision;
  ArvPermissionValue is_visible_to_others;
} ArvCorePermissionSet;

typedef struct ArvResolvedCorePermissions {
  bool can_take_damage;
  bool can_modify_world;
  bool can_interact;
  bool can_open_inventory;
  bool has_creative_inventory;
  bool can_fly;
  bool has_collision;
  bool is_visible_to_others;
} ArvResolvedCorePermissions;

typedef struct ArvCustomPermissionEntry {
  ArvStringView node;
  ArvPermissionValue value;
} ArvCustomPermissionEntry;

typedef struct ArvPermissionNodeDefinition {
  ArvStringView node;
  ArvStringView description;
  ArvPermissionValue default_value;
} ArvPermissionNodeDefinition;

typedef struct ArvPermissionGroupDefinition {
  ArvStringView id;
  int32_t priority;
  const ArvStringView* inherits;
  size_t inherit_count;
  ArvCorePermissionSet core;
  const ArvCustomPermissionEntry* custom_permissions;
  size_t custom_permission_count;
} ArvPermissionGroupDefinition;

typedef struct ArvPermissionsAPI {
  ArvResult (*unsupported)(void* ctx);
  ArvResult (*register_permission_node)(
      void* ctx,
      const ArvPermissionNodeDefinition* definition);
  ArvResult (*check_core)(
      void* ctx,
      ArvPlayerID player,
      ArvCorePermission permission,
      bool* out_allowed);
  ArvResult (*check_custom)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView node,
      ArvPermissionValue* out_value);
  ArvResult (*resolved_core)(
      void* ctx,
      ArvPlayerID player,
      ArvResolvedCorePermissions* out_permissions);
  ArvResult (*register_group)(
      void* ctx,
      const ArvPermissionGroupDefinition* definition);
  ArvResult (*has_group)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView group_id,
      bool* out_has_group);
  ArvResult (*add_group)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView group_id);
  ArvResult (*remove_group)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView group_id);
  uint64_t (*groups)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView* out_groups,
      uint64_t max_groups);
  ArvResult (*upsert_group)(
      void* ctx,
      const ArvPermissionGroupDefinition* definition);
  ArvResult (*remove_registered_group)(
      void* ctx,
      ArvStringView group_id);
  ArvResult (*set_groups)(
      void* ctx,
      ArvPlayerID player,
      const ArvStringView* groups,
      size_t group_count);
  ArvResult (*set_core_override)(
      void* ctx,
      ArvPlayerID player,
      ArvCorePermission permission,
      ArvPermissionValue value);
  ArvResult (*set_custom_override)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView node,
      ArvPermissionValue value);
  ArvResult (*set_overrides)(
      void* ctx,
      ArvPlayerID player,
      ArvCorePermissionSet core,
      const ArvCustomPermissionEntry* custom_permissions,
      size_t custom_permission_count);
} ArvPermissionsAPI;

#ifdef __cplusplus
}
#endif
