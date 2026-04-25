set(ARV_SERVER_HEADERS
  include/arv/server/dedicated_lifetime.hpp
  include/arv/server/session_registry.hpp
  include/arv/server/server_runtime.hpp
  include/arv/server/world_registry.hpp
)

add_library(arv_server INTERFACE)
target_include_directories(arv_server INTERFACE include)
target_link_libraries(arv_server INTERFACE arv_core arv_net arv_protocol arv_sched arv_world)
