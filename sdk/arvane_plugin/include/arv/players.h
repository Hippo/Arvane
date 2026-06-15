#pragma once

#include "arv/result.h"
#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  ARV_PLAYER_SELECTION_HAS_WORLD = 1u << 0u,
  ARV_PLAYER_SELECTION_HAS_A = 1u << 1u,
  ARV_PLAYER_SELECTION_HAS_B = 1u << 2u,
};

typedef struct ArvPlayerSelection {
  uint32_t flags;
  ArvWorldHandle world;
  ArvBlockPos a;
  ArvBlockPos b;
} ArvPlayerSelection;

typedef struct ArvPlayersAPI {
  ArvResult (*current_selection)(
      void* ctx,
      ArvPlayerID player,
      ArvPlayerSelection* out_selection);
  uint64_t (*online_players)(
      void* ctx,
      ArvPlayerID* out_players,
      uint64_t max_players);
  ArvStringView (*profile_id)(
      void* ctx,
      ArvPlayerID player);
  ArvStringView (*display_name)(
      void* ctx,
      ArvPlayerID player);
  bool (*find_by_profile_id)(
      void* ctx,
      ArvStringView profile_id,
      ArvPlayerID* out_player);
} ArvPlayersAPI;

#ifdef __cplusplus
}
#endif
