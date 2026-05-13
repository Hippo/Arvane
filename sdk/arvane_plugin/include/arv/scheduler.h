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
      ArvTickIndex delay_ticks,
      ArvJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvResult (*sync_repeating)(
      void* ctx,
      ArvTickIndex start_delay_ticks,
      ArvTickIndex interval_ticks,
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
      ArvTickIndex delay_ticks,
      ArvJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvResult (*async_repeating)(
      void* ctx,
      ArvTickIndex start_delay_ticks,
      ArvTickIndex interval_ticks,
      ArvJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle);
  ArvResult (*cancel)(void* ctx, ArvScheduledJobHandle handle);
} ArvSchedulerAPI;

#ifdef __cplusplus
}
#endif
