#pragma once

#include "arvane/plugin/commands.h"
#include "arvane/plugin/config.h"
#include "arvane/plugin/context.h"
#include "arvane/plugin/entities.h"
#include "arvane/plugin/events.h"
#include "arvane/plugin/export.h"
#include "arvane/plugin/logger.h"
#include "arvane/plugin/metrics.h"
#include "arvane/plugin/network.h"
#include "arvane/plugin/permissions.h"
#include "arvane/plugin/persistent_data.h"
#include "arvane/plugin/registries.h"
#include "arvane/plugin/resources.h"
#include "arvane/plugin/scheduler.h"
#include "arvane/plugin/services.h"
#include "arvane/plugin/text.h"
#include "arvane/plugin/version.h"
#include "arvane/plugin/world.h"
#include "arvane/plugin/worldgen.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvPluginAPI {
  uint32_t api_version;

  void* context_ctx;
  const ArvPluginContextAPI* context;

  void* registries_ctx;
  const ArvPluginRegistriesAPI* registries;

  void* resources_ctx;
  const ArvPluginResourcesAPI* resources;

  void* worlds_ctx;
  const ArvPluginWorldsAPI* worlds;

  void* entities_ctx;
  const ArvPluginEntitiesAPI* entities;

  void* events_ctx;
  const ArvPluginEventsAPI* events;

  void* scheduler_ctx;
  const ArvPluginSchedulerAPI* scheduler;

  void* commands_ctx;
  const ArvPluginCommandsAPI* commands;

  void* permissions_ctx;
  const ArvPluginPermissionsAPI* permissions;

  void* services_ctx;
  const ArvPluginServicesAPI* services;

  void* config_ctx;
  const ArvPluginConfigAPI* config;

  void* persistent_data_ctx;
  const ArvPluginPersistentDataAPI* persistent_data;

  void* worldgen_ctx;
  const ArvPluginWorldGenAPI* worldgen;

  void* network_ctx;
  const ArvPluginNetworkAPI* network;

  void* text_ctx;
  const ArvPluginTextAPI* text;

  void* metrics_ctx;
  const ArvPluginMetricsAPI* metrics;

  void* logger_ctx;
  const ArvPluginLoggerAPI* logger;
} ArvPluginAPI;

typedef bool (*ArvPluginEntrypoint)(const ArvPluginAPI* api);
typedef ArvPluginEntrypoint ArvPluginMainFn;

#ifdef __cplusplus
}
#endif
