#pragma once

#include "arv/result.h"
#include "arv/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ArvJobCallback)(void* user_data);
typedef ArvJobCallback ArvJobFn;

typedef struct ArvSchedulerAPI {
  ArvResult (*sync_now)(
      void* ctx,
      ArvJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvResult (*sync_delayed)(
      void* ctx,
      ArvMilliseconds delay_milliseconds,
      ArvJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvResult (*sync_repeating)(
      void* ctx,
      ArvMilliseconds start_delay_milliseconds,
      ArvMilliseconds interval_milliseconds,
      ArvJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvResult (*async_now)(
      void* ctx,
      ArvJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvResult (*async_delayed)(
      void* ctx,
      ArvMilliseconds delay_milliseconds,
      ArvJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvResult (*async_repeating)(
      void* ctx,
      ArvMilliseconds start_delay_milliseconds,
      ArvMilliseconds interval_milliseconds,
      ArvJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvResult (*cancel)(void* ctx, ArvScheduledJobHandle handle);
  ArvResult (*sync_delayed_tick)(
      void* ctx,
      ArvTickIndex delay_ticks,
      ArvJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvResult (*sync_repeating_tick)(
      void* ctx,
      ArvTickIndex start_delay_ticks,
      ArvTickIndex interval_ticks,
      ArvJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvResult (*async_delayed_tick)(
      void* ctx,
      ArvTickIndex delay_ticks,
      ArvJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvResult (*async_repeating_tick)(
      void* ctx,
      ArvTickIndex start_delay_ticks,
      ArvTickIndex interval_ticks,
      ArvJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
} ArvSchedulerAPI;

#ifdef __cplusplus
}
#endif
