#pragma once

#ifndef ARVANE_ENABLE_PROFILING
#define ARVANE_ENABLE_PROFILING 0
#endif

#ifndef ARVANE_ENABLE_TRACY
#define ARVANE_ENABLE_TRACY 0
#endif

#if ARVANE_ENABLE_PROFILING && ARVANE_ENABLE_TRACY
#include <tracy/Tracy.hpp>
#endif

namespace arv::core {

[[nodiscard]] constexpr bool profiling_enabled() noexcept {
  return ARVANE_ENABLE_PROFILING != 0;
}

[[nodiscard]] constexpr bool tracy_profiling_enabled() noexcept {
  return ARVANE_ENABLE_PROFILING != 0 && ARVANE_ENABLE_TRACY != 0;
}

inline void set_profile_thread_name([[maybe_unused]] const char* name) noexcept {
#if ARVANE_ENABLE_PROFILING && ARVANE_ENABLE_TRACY
  tracy::SetThreadName(name);
#endif
}

} // namespace arv::core

#if ARVANE_ENABLE_PROFILING && ARVANE_ENABLE_TRACY
#define ARV_PROFILE_ZONE(name) ZoneScopedN(name)
#define ARV_PROFILE_ZONE_DYNAMIC(name, size) \
  ZoneScoped;                                \
  ZoneName((name), (size))
#define ARV_PROFILE_TEXT(text, size) ZoneText((text), (size))
#define ARV_PROFILE_FRAME_MARK(name) FrameMarkNamed(name)
#define ARV_PROFILE_PLOT(name, value) TracyPlot((name), (value))
#define ARV_PROFILE_PLOT_CONFIG_NUMBER(name, step, fill, color) \
  TracyPlotConfig((name), tracy::PlotFormatType::Number, (step), (fill), (color))
#else
#define ARV_PROFILE_ZONE(name) ((void)0)
#define ARV_PROFILE_ZONE_DYNAMIC(name, size) ((void)0)
#define ARV_PROFILE_TEXT(text, size) ((void)0)
#define ARV_PROFILE_FRAME_MARK(name) ((void)0)
#define ARV_PROFILE_PLOT(name, value) ((void)0)
#define ARV_PROFILE_PLOT_CONFIG_NUMBER(name, step, fill, color) ((void)0)
#endif
