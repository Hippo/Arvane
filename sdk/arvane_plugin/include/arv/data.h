#pragma once

#include "arv/result.h"
#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t ArvDataDocumentHandle;

typedef enum ArvDataKind {
  ARV_DATA_NULL = 0,
  ARV_DATA_BOOL = 1,
  ARV_DATA_I64 = 2,
  ARV_DATA_U64 = 3,
  ARV_DATA_F64 = 4,
  ARV_DATA_STRING = 5,
  ARV_DATA_BYTES = 6,
  ARV_DATA_ARRAY = 7,
  ARV_DATA_OBJECT = 8,
  ARV_DATA_INVALID = 255
} ArvDataKind;

typedef struct ArvDataAPI {
  ArvDataDocumentHandle (*create_document)(void* ctx, uint32_t schema_version);
  void (*release_document)(void* ctx, ArvDataDocumentHandle document);
  uint32_t (*schema_version)(void* ctx, ArvDataDocumentHandle document);

  ArvDataKind (*kind)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path);

  ArvResult (*set_null)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path);
  ArvResult (*set_bool)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      bool value);
  ArvResult (*set_i64)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      int64_t value);
  ArvResult (*set_u64)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      uint64_t value);
  ArvResult (*set_f64)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      double value);
  ArvResult (*set_string)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      ArvStringView value);
  ArvResult (*set_bytes)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      ArvBytesView value);
  ArvResult (*set_array)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path);
  ArvResult (*set_object)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path);

  ArvResult (*array_push_null)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path);
  ArvResult (*array_push_bool)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      bool value);
  ArvResult (*array_push_i64)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      int64_t value);
  ArvResult (*array_push_u64)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      uint64_t value);
  ArvResult (*array_push_f64)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      double value);
  ArvResult (*array_push_string)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      ArvStringView value);

  size_t (*size)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path);

  ArvResult (*get_bool)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      bool* out);
  ArvResult (*get_i64)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      int64_t* out);
  ArvResult (*get_u64)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      uint64_t* out);
  ArvResult (*get_f64)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      double* out);
  ArvResult (*get_string)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      ArvStringView* out);
  ArvResult (*get_bytes)(
      void* ctx,
      ArvDataDocumentHandle document,
      ArvStringView path,
      ArvBytesView* out);
} ArvDataAPI;

#ifdef __cplusplus
}
#endif
