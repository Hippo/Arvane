set(ARV_SCHED_HEADERS
  include/arv/sched/completion_queue.hpp
  include/arv/sched/scheduler.hpp
)

add_library(arv_sched INTERFACE)
target_include_directories(arv_sched INTERFACE include)
target_link_libraries(arv_sched INTERFACE arv_core Taskflow)
