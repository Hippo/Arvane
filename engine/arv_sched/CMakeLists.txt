set(ARV_SCHED_HEADERS
  include/arv/sched/completion_queue.hpp
  include/arv/sched/job_system.hpp
  include/arv/sched/owner_thread.hpp
)

add_library(arv_sched STATIC
  src/job_system.cpp
  src/owner_thread.cpp
)
target_include_directories(arv_sched PUBLIC include)
target_link_libraries(arv_sched PUBLIC arv_core Taskflow)
