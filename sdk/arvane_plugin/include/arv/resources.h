#pragma once

#include "arv/resource_id.h"
#include "arv/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArvResourceKind {
  ARV_RESOURCE_DATA = 0,
  ARV_RESOURCE_ASSET = 1,
  ARV_RESOURCE_CONFIG = 2
} ArvResourceKind;

typedef struct ArvResourceAPI {
  bool (*exists)(
      void* ctx,
      ArvResourceKind kind,
      ArvResourceID resource_id,
      ArvStringView extension);
  ArvResult (*read)(
      void* ctx,
      ArvResourceKind kind,
      ArvResourceID resource_id,
      ArvStringView extension,
      ArvByteBuffer* out_buffer);
  ArvResult (*debug_resolved_path)(
      void* ctx,
      ArvResourceKind kind,
      ArvResourceID resource_id,
      ArvStringView extension,
      ArvByteBuffer* out_buffer);
  void (*release_buffer)(void* ctx, ArvByteBuffer buffer);
} ArvResourceAPI;

#ifdef __cplusplus
}
#endif
