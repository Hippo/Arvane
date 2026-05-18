set(ARV_CORE_HEADERS
  include/arv/core/arena.hpp
  include/arv/core/atomic.hpp
  include/arv/core/completion_queue.hpp
  include/arv/core/coordinates.hpp
  include/arv/core/domain_ids.hpp
  include/arv/core/expected.hpp
  include/arv/core/fixed_timestep.hpp
  include/arv/core/id.hpp
  include/arv/core/location.hpp
  include/arv/core/log.hpp
  include/arv/core/math.hpp
  include/arv/core/perf_stats.hpp
  include/arv/core/profiling.hpp
  include/arv/core/rate_limiter.hpp
  include/arv/core/string.hpp
  include/arv/core/text_edit.hpp
  include/arv/core/time.hpp
  include/arv/core/types.hpp
)

add_library(arv_core STATIC
  src/log.cpp
  src/perf_stats.cpp
)
target_include_directories(arv_core PUBLIC include)
target_link_libraries(arv_core PUBLIC unordered_dense spdlog::spdlog concurrentqueue readerwriterqueue)
target_compile_definitions(arv_core PUBLIC
  ARVANE_ENABLE_PROFILING=$<BOOL:${ARVANE_ENABLE_PROFILING}>
  $<$<CONFIG:Debug>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE>
  $<$<NOT:$<CONFIG:Debug>>:SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO>
)

if(ARVANE_ENABLE_PROFILING AND ARVANE_ENABLE_TRACY AND TARGET Tracy::TracyClient)
  target_link_libraries(arv_core PUBLIC Tracy::TracyClient)
  target_compile_definitions(arv_core PUBLIC ARVANE_ENABLE_TRACY=1)
elseif(ARVANE_ENABLE_PROFILING AND ARVANE_ENABLE_TRACY AND TARGET TracyClient)
  target_link_libraries(arv_core PUBLIC TracyClient)
  target_compile_definitions(arv_core PUBLIC ARVANE_ENABLE_TRACY=1)
else()
  target_compile_definitions(arv_core PUBLIC ARVANE_ENABLE_TRACY=0)
endif()
