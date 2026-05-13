#pragma once

#include "arv/data.h"
#include "arv/resource_id.h"
#include "arv/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArvConfigScopeKind {
  ARV_CONFIG_SCOPE_GLOBAL = 0,
  ARV_CONFIG_SCOPE_WORLD = 1
} ArvConfigScopeKind;

typedef struct ArvConfigScope {
  ArvConfigScopeKind kind;
  ArvWorldID world;
} ArvConfigScope;

typedef struct ArvConfigAPI {
  ArvResult (*load)(
      void* ctx,
      const ArvConfigScope* scope,
      ArvResourceID key,
      ArvDataDocumentHandle* out_document);
  ArvResult (*save)(
      void* ctx,
      const ArvConfigScope* scope,
      ArvResourceID key,
      ArvDataDocumentHandle document);
  bool (*exists)(
      void* ctx,
      const ArvConfigScope* scope,
      ArvResourceID key);
  ArvResult (*remove)(
      void* ctx,
      const ArvConfigScope* scope,
      ArvResourceID key);
} ArvConfigAPI;

#ifdef __cplusplus
}
#endif
