#pragma once

#include "arv/data.h"
#include "arv/resource_id.h"
#include "arv/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvConfigAPI {
  ArvResult (*load_config)(
      void* ctx,
      ArvResourceID key,
      ArvDataDocumentHandle* out_document);
  ArvResult (*save_config)(
      void* ctx,
      ArvResourceID key,
      ArvDataDocumentHandle document);
  bool (*config_exists)(
      void* ctx,
      ArvResourceID key);
  ArvResult (*remove_config)(
      void* ctx,
      ArvResourceID key);

  ArvResult (*load_data)(
      void* ctx,
      ArvResourceID key,
      ArvDataDocumentHandle* out_document);
  ArvResult (*save_data)(
      void* ctx,
      ArvResourceID key,
      ArvDataDocumentHandle document);
  bool (*data_exists)(
      void* ctx,
      ArvResourceID key);
  ArvResult (*remove_data)(
      void* ctx,
      ArvResourceID key);

  ArvResult (*load_player_data)(
      void* ctx,
      ArvStringView profile_id,
      ArvResourceID key,
      ArvDataDocumentHandle* out_document);
  ArvResult (*save_player_data)(
      void* ctx,
      ArvStringView profile_id,
      ArvResourceID key,
      ArvDataDocumentHandle document);
  bool (*player_data_exists)(
      void* ctx,
      ArvStringView profile_id,
      ArvResourceID key);
  ArvResult (*remove_player_data)(
      void* ctx,
      ArvStringView profile_id,
      ArvResourceID key);
} ArvConfigAPI;

#ifdef __cplusplus
}
#endif
