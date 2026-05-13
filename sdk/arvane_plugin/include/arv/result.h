#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArvResultCode {
  ARV_RESULT_OK = 0,
  ARV_RESULT_INVALID_ARGUMENT = 1,
  ARV_RESULT_INVALID_STATE = 2,
  ARV_RESULT_PERMISSION_DENIED = 3,
  ARV_RESULT_NOT_FOUND = 4,
  ARV_RESULT_DUPLICATE_ID = 5,
  ARV_RESULT_UNSUPPORTED_VERSION = 6,
  ARV_RESULT_THREAD_VIOLATION = 7,
  ARV_RESULT_UNSUPPORTED = 8,
  ARV_RESULT_INTERNAL_ERROR = 9
} ArvResultCode;

typedef struct ArvResult {
  ArvResultCode code;
  const char* message;
} ArvResult;

#ifdef __cplusplus
}
#endif
