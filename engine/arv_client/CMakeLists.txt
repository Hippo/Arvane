set(ARV_CLIENT_HEADERS
  include/arv/client/block_render_assets.hpp
  include/arv/client/block_outline_target.hpp
  include/arv/client/camera_shell.hpp
  include/arv/client/chat_history.hpp
  include/arv/client/chunk_collision_query.hpp
  include/arv/client/chunk_interest.hpp
  include/arv/client/chunk_snapshot.hpp
  include/arv/client/client_pack_sync.hpp
  include/arv/client/client_session.hpp
  include/arv/client/command_chat.hpp
  include/arv/client/client_runtime.hpp
  include/arv/client/dropped_item_mirror.hpp
  include/arv/client/entity_render_assets.hpp
  include/arv/client/gameplay_interactions.hpp
  include/arv/client/gameplay_ui_model.hpp
  include/arv/client/item_animation.hpp
  include/arv/client/item_rendering.hpp
  include/arv/client/item_ui_model.hpp
  include/arv/client/local_identity.hpp
  include/arv/client/local_player_controller.hpp
  include/arv/client/player_skin_rendering.hpp
  include/arv/client/particles.hpp
  include/arv/client/render_asset_upload.hpp
  include/arv/client/sky_rendering.hpp
  include/arv/client/ui_interactions.hpp
)

add_library(arv_client_pack_sync STATIC
  src/client_pack_sync.cpp
)
target_include_directories(arv_client_pack_sync PUBLIC include)
target_link_libraries(arv_client_pack_sync
  PUBLIC
    arv_core_headers
    arv_protocol_headers
  PRIVATE
    arv_asset_bundle
)

add_library(arv_client_session STATIC
  src/client_session.cpp
)
target_include_directories(arv_client_session PUBLIC include)
target_link_libraries(arv_client_session
  PUBLIC
    arv_core_headers
    arv_crypto
    arv_client_dropped_items
    arv_net
    arv_player
    arv_protocol_block_updates
    arv_protocol_container_sync
    arv_protocol_dropped_items
    arv_protocol_entity_replication
    arv_protocol_inventory_sync
    arv_protocol_player_input
    arv_protocol_player_movement
    arv_protocol_session
    arv_protocol_stream
  PRIVATE
    arv_asset_bundle
    arv_client_pack_sync
)

add_library(arv_client_local_player STATIC
  src/camera_shell.cpp
  src/local_player_controller.cpp
)
target_include_directories(arv_client_local_player PUBLIC include)
target_link_libraries(arv_client_local_player
  PUBLIC
    arv_core_headers
    arv_physics
    arv_player_headers
    arv_protocol_headers
    arv_protocol_player_input
    arv_protocol_player_movement
    arv_render_headers
)

add_library(arv_client_chunk_snapshot STATIC
  src/chunk_snapshot.cpp
)
target_include_directories(arv_client_chunk_snapshot PUBLIC include)
target_link_libraries(arv_client_chunk_snapshot
  PUBLIC
    arv_protocol_stream
    arv_voxel
)

add_library(arv_client_chunk_collision STATIC
  src/block_outline_target.cpp
  src/chunk_collision_query.cpp
)
target_include_directories(arv_client_chunk_collision PUBLIC include)
target_link_libraries(arv_client_chunk_collision
  PUBLIC
    arv_physics
    arv_protocol_stream
    arv_voxel
)

add_library(arv_client_render_assets STATIC
  src/block_render_assets.cpp
  src/entity_render_assets.cpp
  src/render_asset_upload.cpp
  src/sky_rendering.cpp
)
target_include_directories(arv_client_render_assets PUBLIC include)
target_link_libraries(arv_client_render_assets
  PUBLIC
    arv_asset_core
    arv_entity
    arv_io_headers
    arv_player
    arv_render_mesh
    arv_render_headers
    arv_world_time
)

add_library(arv_client_item_ui STATIC
  src/item_ui_model.cpp
)
target_include_directories(arv_client_item_ui PUBLIC include)
target_link_libraries(arv_client_item_ui
  PUBLIC
    arv_asset_core
    arv_base_ui
    arv_player
    arv_render_mesh
    arv_render_headers
    arv_voxel
  PRIVATE
    arv_io_data
)

add_library(arv_client_item_animation STATIC
  src/item_animation.cpp
)
target_include_directories(arv_client_item_animation PUBLIC include)
target_link_libraries(arv_client_item_animation
  PUBLIC
    arv_core_headers
    arv_player
  PRIVATE
    arv_io_data
)

add_library(arv_client_dropped_items STATIC
  src/client_dropped_item_mirror.cpp
)
target_include_directories(arv_client_dropped_items PUBLIC include)
target_link_libraries(arv_client_dropped_items
  PUBLIC
    arv_protocol_dropped_items
)

add_library(arv_client_item_rendering STATIC
  src/item_rendering.cpp
)
target_include_directories(arv_client_item_rendering PUBLIC include)
target_link_libraries(arv_client_item_rendering
  PUBLIC
    arv_client_dropped_items
    arv_client_item_animation
    arv_client_render_assets
    arv_render_headers
)

add_library(arv_client_runtime STATIC
  src/chunk_interest.cpp
  src/client_chunk_inbox.cpp
  src/client_connection.cpp
  src/client_entity_mirror.cpp
  src/client_frame_builder.cpp
  src/client_input_runtime.cpp
  src/client_inventory_runtime.cpp
  src/client_network_messages.cpp
  src/client_protocol_send.cpp
  src/client_runtime.cpp
  src/client_skin_runtime.cpp
  src/client_ui_state.cpp
  src/gameplay_interactions.cpp
  src/gameplay_ui_model.cpp
  src/local_identity.cpp
  src/player_skin_rendering.cpp
  src/particles.cpp
  src/ui_interactions.cpp
)
target_include_directories(arv_client_runtime PUBLIC include)
target_link_libraries(arv_client_runtime
  PUBLIC
    arv_asset_core
    arv_base_ui
    arv_client_chunk_collision
    arv_client_chunk_snapshot
    arv_client_dropped_items
    arv_client_local_player
    arv_core
    arv_crypto
    arv_entity
    arv_io_headers
    arv_net
    arv_physics
    arv_player
    arv_protocol_block_updates
    arv_protocol_chat
    arv_protocol_container_sync
    arv_protocol_dropped_items
    arv_protocol_entity_interaction
    arv_protocol_entity_replication
    arv_protocol_explosion
    arv_protocol_inventory_sync
    arv_protocol_particles
    arv_protocol_player_input
    arv_protocol_player_movement
    arv_protocol_session
    arv_protocol_skins
    arv_protocol_stream
    arv_protocol_text_variables
    arv_protocol_vehicles
    arv_protocol_world_time
    arv_render_mesh
    arv_text
  PRIVATE
    arv_asset_bundle
    arv_client_pack_sync
    arv_io_data
)

if(EMSCRIPTEN AND ARVANE_BUILD_WEB)
  return()
endif()

add_library(arv_client STATIC
  src/command_chat.cpp
)
target_include_directories(arv_client PUBLIC include)
target_link_libraries(arv_client PUBLIC arv_client_runtime arv_client_chunk_collision arv_client_chunk_snapshot arv_client_dropped_items arv_client_item_animation arv_client_item_rendering arv_client_item_ui arv_client_local_player arv_client_render_assets arv_core arv_crypto arv_net arv_physics arv_player arv_protocol arv_render)
target_link_libraries(arv_client PRIVATE arv_asset arv_client_pack_sync arv_client_session arv_command arv_entity arv_io arv_text)
