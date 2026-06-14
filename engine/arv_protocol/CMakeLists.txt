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

add_library(arv_protocol_block_updates STATIC
  src/block_updates.cpp
)
target_include_directories(arv_protocol_block_updates PUBLIC include)
target_link_libraries(arv_protocol_block_updates PUBLIC arv_protocol_headers arv_voxel_headers)

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

add_library(arv_protocol_chat STATIC
  src/chat.cpp
)
target_include_directories(arv_protocol_chat PUBLIC include)
target_link_libraries(arv_protocol_chat PUBLIC arv_protocol_headers arv_text)

add_library(arv_protocol_entity_interaction STATIC
  src/entity_interaction.cpp
)
target_include_directories(arv_protocol_entity_interaction PUBLIC include)
target_link_libraries(arv_protocol_entity_interaction PUBLIC arv_protocol_headers)

add_library(arv_protocol_explosion STATIC
  src/explosion.cpp
)
target_include_directories(arv_protocol_explosion PUBLIC include)
target_link_libraries(arv_protocol_explosion PUBLIC arv_protocol_headers)

add_library(arv_protocol_particles STATIC
  src/particles.cpp
)
target_include_directories(arv_protocol_particles PUBLIC include)
target_link_libraries(arv_protocol_particles PUBLIC arv_protocol_headers)

add_library(arv_protocol_skins STATIC
  src/skins.cpp
)
target_include_directories(arv_protocol_skins PUBLIC include)
target_link_libraries(arv_protocol_skins PUBLIC arv_protocol_headers)
target_link_libraries(arv_protocol_skins PRIVATE arv_crypto)

add_library(arv_protocol_text_variables STATIC
  src/text_variables.cpp
)
target_include_directories(arv_protocol_text_variables PUBLIC include)
target_link_libraries(arv_protocol_text_variables PUBLIC arv_protocol_headers)
target_link_libraries(arv_protocol_text_variables PRIVATE arv_text)

add_library(arv_protocol_vehicles STATIC
  src/vehicles.cpp
)
target_include_directories(arv_protocol_vehicles PUBLIC include)
target_link_libraries(arv_protocol_vehicles PUBLIC arv_protocol_headers)

add_library(arv_protocol_world_time STATIC
  src/world_time.cpp
)
target_include_directories(arv_protocol_world_time PUBLIC include)
target_link_libraries(arv_protocol_world_time PUBLIC arv_protocol_headers)

if(EMSCRIPTEN AND ARVANE_BUILD_WEB)
  return()
endif()

add_library(arv_protocol STATIC
  src/message_registry.cpp
)
target_include_directories(arv_protocol PUBLIC include)
target_link_libraries(arv_protocol PUBLIC arv_protocol_headers)
target_link_libraries(arv_protocol PUBLIC arv_protocol_session arv_protocol_stream arv_protocol_block_updates arv_protocol_player_movement arv_protocol_player_input arv_protocol_entity_replication arv_protocol_dropped_items arv_protocol_inventory_sync arv_protocol_chat arv_protocol_entity_interaction arv_protocol_explosion arv_protocol_particles arv_protocol_skins arv_protocol_text_variables arv_protocol_vehicles arv_protocol_world_time arv_core arv_crypto arv_player arv_text arv_voxel)
