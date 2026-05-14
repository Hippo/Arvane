#pragma once

#include "arv/commands.h"
#include "arv/config.h"
#include "arv/context.h"
#include "arv/data.h"
#include "arv/entities.h"
#include "arv/events.h"
#include "arv/export.h"
#include "arv/gamemode.h"
#include "arv/logger.h"
#include "arv/metrics.h"
#include "arv/network.h"
#include "arv/permissions.h"
#include "arv/persistent_data.h"
#include "arv/players.h"
#include "arv/registries.h"
#include "arv/regions.h"
#include "arv/resources.h"
#include "arv/scheduler.h"
#include "arv/services.h"
#include "arv/text.h"
#include "arv/version.h"
#include "arv/world.h"
#include "arv/worldgen.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvAPI {
  uint32_t api_version;

  void* context_ctx;
  const ArvContextAPI* context;

  void* registries_ctx;
  const ArvRegistryAPI* registries;

  void* resources_ctx;
  const ArvResourceAPI* resources;

  void* worlds_ctx;
  const ArvWorldAPI* worlds;

  void* entities_ctx;
  const ArvEntityAPI* entities;

  void* events_ctx;
  const ArvEventAPI* events;

  void* scheduler_ctx;
  const ArvSchedulerAPI* scheduler;

  void* commands_ctx;
  const ArvCommandAPI* commands;

  void* permissions_ctx;
  const ArvPermissionsAPI* permissions;

  void* services_ctx;
  const ArvServiceAPI* services;

  void* data_ctx;
  const ArvDataAPI* data;

  void* config_ctx;
  const ArvConfigAPI* config;

  void* persistent_data_ctx;
  const ArvPersistentDataAPI* persistent_data;

  void* worldgen_ctx;
  const ArvWorldGenAPI* worldgen;

  void* network_ctx;
  const ArvNetworkAPI* network;

  void* text_ctx;
  const ArvTextAPI* text;

  void* metrics_ctx;
  const ArvMetricsAPI* metrics;

  void* logger_ctx;
  const ArvLoggerAPI* logger;

  void* regions_ctx;
  const ArvRegionsAPI* regions;

  void* gamemodes_ctx;
  const ArvGameModeAPI* gamemodes;

  void* players_ctx;
  const ArvPlayersAPI* players;
} ArvAPI;

typedef bool (*ArvEntrypoint)(const ArvAPI* api);
typedef ArvEntrypoint ArvMainFn;

#ifdef __cplusplus
}
#endif
