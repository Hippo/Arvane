#pragma once

#include "arv/result.h"
#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvTextHandle {
  uint64_t value;
} ArvTextHandle;

typedef struct ArvTextColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} ArvTextColor;

typedef enum ArvTextClickAction {
  ARV_TEXT_CLICK_OPEN_URL = 0,
  ARV_TEXT_CLICK_SUGGEST_COMMAND = 1,
  ARV_TEXT_CLICK_RUN_COMMAND = 2,
  ARV_TEXT_CLICK_COPY_TO_CLIPBOARD = 3
} ArvTextClickAction;

typedef struct ArvTextStylePatch {
  bool has_color;
  ArvTextColor color;
  bool has_bold;
  bool bold;
  bool has_italic;
  bool italic;
  bool has_underlined;
  bool underlined;
  bool has_strikethrough;
  bool strikethrough;
  bool has_obfuscated;
  bool obfuscated;
  bool has_font;
  ArvStringView font;
} ArvTextStylePatch;

typedef struct ArvTextLimits {
  uint64_t max_serialized_size_bytes;
  uint64_t max_text_bytes;
  uint64_t max_nodes;
  uint64_t max_depth;
  uint64_t max_hover_text_bytes;
  uint64_t max_click_value_bytes;
} ArvTextLimits;

typedef struct ArvTextAPI {
  ArvResult (*unsupported)(void* ctx);
  ArvResult (*literal)(
      void* ctx,
      ArvStringView text,
      ArvTextHandle* out_text);
  ArvResult (*translatable)(
      void* ctx,
      ArvStringView translation_key,
      ArvTextHandle* out_text);
  ArvResult (*append)(
      void* ctx,
      ArvTextHandle parent,
      ArvTextHandle child,
      ArvTextHandle* out_text);
  ArvResult (*style)(
      void* ctx,
      ArvTextHandle text,
      ArvTextStylePatch style,
      ArvTextHandle* out_text);
  ArvResult (*color)(
      void* ctx,
      ArvTextHandle text,
      ArvTextColor color,
      ArvTextHandle* out_text);
  ArvResult (*hover_text)(
      void* ctx,
      ArvTextHandle text,
      ArvTextHandle hover_text,
      ArvTextHandle* out_text);
  ArvResult (*click)(
      void* ctx,
      ArvTextHandle text,
      ArvTextClickAction action,
      ArvStringView value,
      ArvTextHandle* out_text);
  ArvResult (*plain_text)(
      void* ctx,
      ArvTextHandle text,
      ArvByteBuffer* out_buffer);
  ArvResult (*serialize)(
      void* ctx,
      ArvTextHandle text,
      ArvByteBuffer* out_buffer);
  ArvResult (*validate)(
      void* ctx,
      ArvTextHandle text,
      const ArvTextLimits* limits);
  void (*release)(
      void* ctx,
      ArvTextHandle text);
} ArvTextAPI;

#ifdef __cplusplus
}
#endif
