#pragma once

#if defined(_WIN32)
#define ARV_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define ARV_EXPORT __attribute__((visibility("default")))
#else
#define ARV_EXPORT
#endif
