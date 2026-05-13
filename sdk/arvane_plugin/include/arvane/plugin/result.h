#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArvPluginResultCode {
  ARV_PLUGIN_RESULT_OK = 0,
  ARV_PLUGIN_RESULT_INVALID_ARGUMENT = 1,
  ARV_PLUGIN_RESULT_INVALID_STATE = 2,
  ARV_PLUGIN_RESULT_PERMISSION_DENIED = 3,
  ARV_PLUGIN_RESULT_NOT_FOUND = 4,
  ARV_PLUGIN_RESULT_DUPLICATE_ID = 5,
  ARV_PLUGIN_RESULT_UNSUPPORTED_VERSION = 6,
  ARV_PLUGIN_RESULT_THREAD_VIOLATION = 7,
  ARV_PLUGIN_RESULT_UNSUPPORTED = 8,
  ARV_PLUGIN_RESULT_INTERNAL_ERROR = 9
} ArvPluginResultCode;

typedef struct ArvPluginResult {
  ArvPluginResultCode code;
  const char* message;
} ArvPluginResult;

#ifdef __cplusplus
}
#endif
