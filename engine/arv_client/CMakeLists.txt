set(ARV_CLIENT_HEADERS
  include/arv/client/camera_shell.hpp
  include/arv/client/chat_history.hpp
  include/arv/client/chunk_interest.hpp
  include/arv/client/client_pack_sync.hpp
  include/arv/client/client_session.hpp
  include/arv/client/command_chat.hpp
  include/arv/client/client_runtime.hpp
  include/arv/client/item_animation.hpp
  include/arv/client/local_identity.hpp
  include/arv/client/local_player_controller.hpp
  include/arv/client/particles.hpp
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
    arv_net
    arv_protocol_entity_replication
    arv_protocol_player_movement
    arv_protocol_session
    arv_protocol_stream
  PRIVATE
    arv_asset_bundle
    arv_client_pack_sync
)

if(EMSCRIPTEN AND ARVANE_BUILD_WEB)
  return()
endif()

add_library(arv_client STATIC
  src/camera_shell.cpp
  src/chunk_interest.cpp
  src/command_chat.cpp
  src/client_chunk_inbox.cpp
  src/client_connection.cpp
  src/client_dropped_item_mirror.cpp
  src/client_entity_mirror.cpp
  src/client_frame_builder.cpp
  src/client_input_runtime.cpp
  src/client_inventory_runtime.cpp
  src/client_network_messages.cpp
  src/client_protocol_send.cpp
  src/client_runtime.cpp
  src/client_skin_runtime.cpp
  src/client_ui_state.cpp
  src/item_animation.cpp
  src/local_identity.cpp
  src/local_player_controller.cpp
  src/particles.cpp
)
target_include_directories(arv_client PUBLIC include)
target_link_libraries(arv_client PUBLIC arv_core arv_crypto arv_net arv_physics arv_player arv_protocol arv_render)
target_link_libraries(arv_client PRIVATE arv_asset arv_client_pack_sync arv_client_session arv_command arv_entity arv_io arv_text)
