#pragma once

#include "arvane/plugin/result.h"
#include "arvane/plugin/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ArvPluginJobCallback)(void* user_data);
typedef ArvPluginJobCallback ArvPluginJobFn;

typedef struct ArvSchedulerAPI {
  ArvPluginResult (*sync_now)(
      void* ctx,
      ArvPluginJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvPluginResult (*sync_delayed)(
      void* ctx,
      ArvTickIndex delay_ticks,
      ArvPluginJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvPluginResult (*sync_repeating)(
      void* ctx,
      ArvTickIndex start_delay_ticks,
      ArvTickIndex interval_ticks,
      ArvPluginJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvPluginResult (*async_now)(
      void* ctx,
      ArvPluginJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvPluginResult (*async_delayed)(
      void* ctx,
      ArvTickIndex delay_ticks,
      ArvPluginJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvPluginResult (*async_repeating)(
      void* ctx,
      ArvTickIndex start_delay_ticks,
      ArvTickIndex interval_ticks,
      ArvPluginJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvPluginResult (*cancel)(void* ctx, ArvScheduledJobHandle handle);
} ArvSchedulerAPI;

typedef ArvSchedulerAPI ArvPluginSchedulerAPI;

#ifdef __cplusplus
}
#endif
