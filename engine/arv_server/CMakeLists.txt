set(ARV_SERVER_HEADERS
  include/arv/server/chat_service.hpp
  include/arv/server/chunk_streamer.hpp
  include/arv/server/dedicated_lifetime.hpp
  include/arv/server/permission_config.hpp
  include/arv/server/player_movement_validation.hpp
  include/arv/server/player_profile.hpp
  include/arv/server/server_combat.hpp
  include/arv/server/server_commands.hpp
  include/arv/server/session_registry.hpp
  include/arv/server/skin_runtime.hpp
  include/arv/server/server_runtime.hpp
  include/arv/server/vehicle_movement_validation.hpp
  include/arv/server/world_registry.hpp
)

add_library(arv_server STATIC
  src/block_break_behavior_registry.cpp
  src/chat_service.cpp
  src/chunk_streamer.cpp
  src/permission_config.cpp
  src/player_movement_validation.cpp
  src/player_profile.cpp
  src/server_commands.cpp
  src/server_chunk_jobs.cpp
  src/server_chunk_streaming_runtime.cpp
  src/server_combat.cpp
  src/server_dropped_items.cpp
  src/server_explosion.cpp
  src/server_inventory_runtime.cpp
  src/server_network_messages.cpp
  src/server_player_interaction.cpp
  src/server_player_save_state.cpp
  src/server_player_simulation.cpp
  src/server_replication.cpp
  src/server_runtime.cpp
  src/server_session_lifecycle.cpp
  src/session_registry.cpp
  src/skin_runtime.cpp
  src/vehicle_movement_validation.cpp
  src/world_registry.cpp
)
target_include_directories(arv_server PUBLIC include)
target_link_libraries(arv_server PUBLIC arv_command arv_core arv_crypto arv_io arv_net arv_physics arv_plugin arv_protocol arv_sched arv_world)
target_link_libraries(arv_server PRIVATE arv_asset arv_persistence arv_text)
