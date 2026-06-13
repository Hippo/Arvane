set(ARV_PROTOCOL_HEADERS
  include/arv/protocol/auth.hpp
  include/arv/protocol/block_updates.hpp
  include/arv/protocol/chat.hpp
  include/arv/protocol/chunk_streaming.hpp
  include/arv/protocol/entity_interaction.hpp
  include/arv/protocol/entity_replication.hpp
  include/arv/protocol/entity_updates.hpp
  include/arv/protocol/explosion.hpp
  include/arv/protocol/handshake.hpp
  include/arv/protocol/inventory_sync.hpp
  include/arv/protocol/item_stack_wire.hpp
  include/arv/protocol/message_envelope.hpp
  include/arv/protocol/message_primitives.hpp
  include/arv/protocol/message_registry.hpp
  include/arv/protocol/message_type.hpp
  include/arv/protocol/message_writer.hpp
  include/arv/protocol/pack_sync.hpp
  include/arv/protocol/particles.hpp
  include/arv/protocol/player_input.hpp
  include/arv/protocol/player_movement.hpp
  include/arv/protocol/protocol_version.hpp
  include/arv/protocol/session_control.hpp
  include/arv/protocol/skins.hpp
  include/arv/protocol/text_variables.hpp
  include/arv/protocol/vehicles.hpp
  include/arv/protocol/world_join.hpp
  include/arv/protocol/world_time.hpp
)

add_library(arv_protocol_headers INTERFACE)
target_include_directories(arv_protocol_headers INTERFACE include)
target_link_libraries(arv_protocol_headers INTERFACE arv_core_headers)

add_library(arv_protocol_session STATIC
  src/auth.cpp
  src/handshake.cpp
  src/message_envelope.cpp
  src/pack_sync.cpp
  src/session_control.cpp
  src/world_join.cpp
)
target_include_directories(arv_protocol_session PUBLIC include)
target_link_libraries(arv_protocol_session PUBLIC arv_protocol_headers arv_crypto)

add_library(arv_protocol_stream STATIC
  src/chunk_streaming.cpp
)
target_include_directories(arv_protocol_stream PUBLIC include)
target_link_libraries(arv_protocol_stream PUBLIC arv_protocol_headers arv_voxel_headers)

add_library(arv_protocol_player_movement STATIC
  src/player_movement.cpp
)
target_include_directories(arv_protocol_player_movement PUBLIC include)
target_link_libraries(arv_protocol_player_movement PUBLIC arv_protocol_headers)

add_library(arv_protocol_player_input STATIC
  src/player_input.cpp
)
target_include_directories(arv_protocol_player_input PUBLIC include)
target_link_libraries(arv_protocol_player_input PUBLIC arv_protocol_headers arv_player_headers arv_voxel_headers)

add_library(arv_protocol_entity_replication STATIC
  src/entity_updates.cpp
)
target_include_directories(arv_protocol_entity_replication PUBLIC include)
target_link_libraries(arv_protocol_entity_replication PUBLIC arv_protocol_headers)

add_library(arv_protocol_item_stack_wire STATIC
  src/item_stack_wire.cpp
)
target_include_directories(arv_protocol_item_stack_wire PUBLIC include)
target_link_libraries(arv_protocol_item_stack_wire PUBLIC arv_protocol_headers arv_player_headers)
target_link_libraries(arv_protocol_item_stack_wire PRIVATE arv_io_data)

add_library(arv_protocol_dropped_items STATIC
  src/entity_replication.cpp
)
target_include_directories(arv_protocol_dropped_items PUBLIC include)
target_link_libraries(arv_protocol_dropped_items PUBLIC arv_protocol_entity_replication arv_protocol_item_stack_wire)

add_library(arv_protocol_inventory_sync STATIC
  src/inventory_sync.cpp
)
target_include_directories(arv_protocol_inventory_sync PUBLIC include)
target_link_libraries(arv_protocol_inventory_sync PUBLIC arv_protocol_headers arv_player_headers arv_protocol_item_stack_wire)

if(EMSCRIPTEN AND ARVANE_BUILD_WEB)
  return()
endif()

add_library(arv_protocol STATIC
  src/block_updates.cpp
  src/chat.cpp
  src/entity_interaction.cpp
  src/explosion.cpp
  src/message_registry.cpp
  src/particles.cpp
  src/skins.cpp
  src/text_variables.cpp
  src/vehicles.cpp
  src/world_time.cpp
)
target_include_directories(arv_protocol PUBLIC include)
target_link_libraries(arv_protocol PUBLIC arv_protocol_headers)
target_link_libraries(arv_protocol PUBLIC arv_protocol_session arv_protocol_stream arv_protocol_player_movement arv_protocol_player_input arv_protocol_entity_replication arv_protocol_dropped_items arv_protocol_inventory_sync arv_core arv_crypto arv_player arv_text arv_voxel)
