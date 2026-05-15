#pragma once

#include "arv/result.h"
#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArvVariableBinding {
  ARV_VARIABLE_BINDING_RESOLVED = 0,
  ARV_VARIABLE_BINDING_LIVE = 1
} ArvVariableBinding;

typedef struct ArvVariablesAPI {
  ArvResult (*set_global_string)(
      void* ctx,
      ArvStringView name,
      ArvStringView value);
  ArvResult (*set_global_bool)(
      void* ctx,
      ArvStringView name,
      bool value);
  ArvResult (*set_global_i64)(
      void* ctx,
      ArvStringView name,
      int64_t value);
  ArvResult (*set_global_u64)(
      void* ctx,
      ArvStringView name,
      uint64_t value);
  ArvResult (*set_global_f64)(
      void* ctx,
      ArvStringView name,
      double value);
  ArvResult (*clear_global)(
      void* ctx,
      ArvStringView name);

  ArvResult (*set_player_string)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView name,
      ArvStringView value);
  ArvResult (*set_player_bool)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView name,
      bool value);
  ArvResult (*set_player_i64)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView name,
      int64_t value);
  ArvResult (*set_player_u64)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView name,
      uint64_t value);
  ArvResult (*set_player_f64)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView name,
      double value);
  ArvResult (*clear_player)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView name);

  ArvResult (*set_global_string_bound)(
      void* ctx,
      ArvStringView name,
      ArvStringView value,
      ArvVariableBinding binding);
  ArvResult (*set_global_bool_bound)(
      void* ctx,
      ArvStringView name,
      bool value,
      ArvVariableBinding binding);
  ArvResult (*set_global_i64_bound)(
      void* ctx,
      ArvStringView name,
      int64_t value,
      ArvVariableBinding binding);
  ArvResult (*set_global_u64_bound)(
      void* ctx,
      ArvStringView name,
      uint64_t value,
      ArvVariableBinding binding);
  ArvResult (*set_global_f64_bound)(
      void* ctx,
      ArvStringView name,
      double value,
      ArvVariableBinding binding);

  ArvResult (*set_player_string_bound)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView name,
      ArvStringView value,
      ArvVariableBinding binding);
  ArvResult (*set_player_bool_bound)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView name,
      bool value,
      ArvVariableBinding binding);
  ArvResult (*set_player_i64_bound)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView name,
      int64_t value,
      ArvVariableBinding binding);
  ArvResult (*set_player_u64_bound)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView name,
      uint64_t value,
      ArvVariableBinding binding);
  ArvResult (*set_player_f64_bound)(
      void* ctx,
      ArvPlayerID player,
      ArvStringView name,
      double value,
      ArvVariableBinding binding);
} ArvVariablesAPI;

#ifdef __cplusplus
}
#endif
