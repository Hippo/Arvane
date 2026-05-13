#pragma once

#include "arv/data.h"
#include "arv/result.h"
#include "arv/resource_id.h"
#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArvPersistentScopeKind {
  ARV_PERSISTENT_SCOPE_SERVER = 0,
  ARV_PERSISTENT_SCOPE_WORLD = 1,
  ARV_PERSISTENT_SCOPE_PLAYER = 2,
  ARV_PERSISTENT_SCOPE_ENTITY = 3,
  ARV_PERSISTENT_SCOPE_BLOCK = 4,
  ARV_PERSISTENT_SCOPE_CHUNK = 5
} ArvPersistentScopeKind;

typedef struct ArvPersistentScope {
  ArvPersistentScopeKind kind;
  ArvWorldID world;
  ArvPlayerID player;
  ArvEntityID entity;
  ArvBlockPos block_pos;
  ArvChunkPos chunk_pos;
} ArvPersistentScope;

typedef struct ArvPersistentDataAPI {
  ArvResult (*load)(
      void* ctx,
      const ArvPersistentScope* scope,
      ArvResourceID key,
      ArvDataDocumentHandle* out_document);
  ArvResult (*save)(
      void* ctx,
      const ArvPersistentScope* scope,
      ArvResourceID key,
      ArvDataDocumentHandle document);
  bool (*exists)(
      void* ctx,
      const ArvPersistentScope* scope,
      ArvResourceID key);
  ArvResult (*remove)(
      void* ctx,
      const ArvPersistentScope* scope,
      ArvResourceID key);
} ArvPersistentDataAPI;

#ifdef __cplusplus
}
#endif
