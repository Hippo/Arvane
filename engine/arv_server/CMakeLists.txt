set(ARV_SERVER_HEADERS
  include/arv/server/dedicated_lifetime.hpp
  include/arv/server/session_registry.hpp
  include/arv/server/server_runtime.hpp
  include/arv/server/world_registry.hpp
)

add_library(arv_server STATIC
  src/server_runtime.cpp
  src/session_registry.cpp
  src/world_registry.cpp
)
target_include_directories(arv_server PUBLIC include)
target_link_libraries(arv_server PUBLIC arv_core arv_net arv_protocol arv_sched arv_world)
