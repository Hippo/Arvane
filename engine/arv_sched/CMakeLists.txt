set(ARV_SCHED_HEADERS
  include/arv/sched/completion_queue.hpp
  include/arv/sched/scheduler.hpp
)

add_library(arv_sched STATIC
  src/scheduler.cpp
)
target_include_directories(arv_sched PUBLIC include)
target_link_libraries(arv_sched PUBLIC arv_core Taskflow concurrentqueue readerwriterqueue)
