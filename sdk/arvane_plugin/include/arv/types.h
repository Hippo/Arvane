#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArvStringView {
  const char* data;
  size_t size;
} ArvStringView;

typedef struct ArvBytesView {
  const uint8_t* data;
  size_t size;
} ArvBytesView;

typedef struct ArvByteBuffer {
  uint8_t* data;
  size_t size;
} ArvByteBuffer;

typedef struct ArvVec3 {
  float x;
  float y;
  float z;
} ArvVec3;

typedef uint64_t ArvTickIndex;
typedef uint64_t ArvMilliseconds;
typedef uint64_t ArvWorldID;
typedef uint64_t ArvEntityID;
typedef uint64_t ArvPlayerID;
typedef uint64_t ArvScheduledJobID;

typedef struct ArvLocation {
  float x;
  float y;
  float z;
  float yaw_radians;
  float pitch_radians;
  bool has_world;
  ArvWorldID world;
} ArvLocation;

typedef struct ArvBlockPos {
  int32_t x;
  int32_t y;
  int32_t z;
} ArvBlockPos;

typedef struct ArvChunkPos {
  int32_t x;
  int32_t z;
} ArvChunkPos;

typedef struct ArvScheduledJobHandle {
  ArvScheduledJobID value;
} ArvScheduledJobHandle;

typedef struct ArvServiceHandle {
  uint64_t value;
} ArvServiceHandle;

typedef struct ArvEventSubscriptionHandle {
  uint64_t value;
} ArvEventSubscriptionHandle;

typedef ArvWorldID ArvWorldHandle;
typedef ArvEntityID ArvEntityHandle;

ArvStringView arv_string_view(const char* data, size_t size);
ArvStringView arv_cstr(const char* text);

#ifdef __cplusplus
}
#endif
