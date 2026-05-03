set(ARV_CLIENT_HEADERS
  include/arv/client/camera_shell.hpp
  include/arv/client/chunk_interest.hpp
  include/arv/client/client_runtime.hpp
  include/arv/client/item_animation.hpp
  include/arv/client/local_player_controller.hpp
)

add_library(arv_client STATIC
  src/camera_shell.cpp
  src/chunk_interest.cpp
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
  src/client_ui_state.cpp
  src/item_animation.cpp
  src/local_player_controller.cpp
)
target_include_directories(arv_client PUBLIC include)
target_link_libraries(arv_client PUBLIC arv_core arv_net arv_player arv_protocol arv_render)
target_link_libraries(arv_client PRIVATE nlohmann_json::nlohmann_json)
