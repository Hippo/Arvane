set(ARV_SERVER_HEADERS
  include/arv/server/chunk_streamer.hpp
  include/arv/server/dedicated_lifetime.hpp
  include/arv/server/session_registry.hpp
  include/arv/server/server_runtime.hpp
  include/arv/server/world_registry.hpp
)

add_library(arv_server STATIC
  src/chunk_streamer.cpp
  src/server_chunk_jobs.cpp
  src/server_chunk_streaming_runtime.cpp
  src/server_dropped_items.cpp
  src/server_inventory_runtime.cpp
  src/server_network_messages.cpp
  src/server_player_interaction.cpp
  src/server_player_save_state.cpp
  src/server_player_simulation.cpp
  src/server_replication.cpp
  src/server_runtime.cpp
  src/server_session_lifecycle.cpp
  src/session_registry.cpp
  src/world_registry.cpp
)
target_include_directories(arv_server PUBLIC include)
target_link_libraries(arv_server PUBLIC arv_core arv_net arv_protocol arv_sched arv_world)
