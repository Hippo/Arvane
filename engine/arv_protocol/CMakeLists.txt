set(ARV_PROTOCOL_HEADERS
  include/arv/protocol/auth.hpp
  include/arv/protocol/block_updates.hpp
  include/arv/protocol/chat.hpp
  include/arv/protocol/chunk_streaming.hpp
  include/arv/protocol/entity_interaction.hpp
  include/arv/protocol/entity_replication.hpp
  include/arv/protocol/handshake.hpp
  include/arv/protocol/inventory_sync.hpp
  include/arv/protocol/message_envelope.hpp
  include/arv/protocol/message_registry.hpp
  include/arv/protocol/message_type.hpp
  include/arv/protocol/message_writer.hpp
  include/arv/protocol/player_input.hpp
  include/arv/protocol/protocol_version.hpp
  include/arv/protocol/session_control.hpp
  include/arv/protocol/skins.hpp
  include/arv/protocol/world_join.hpp
)

add_library(arv_protocol STATIC
  src/auth.cpp
  src/block_updates.cpp
  src/chat.cpp
  src/chunk_streaming.cpp
  src/entity_interaction.cpp
  src/entity_replication.cpp
  src/handshake.cpp
  src/inventory_sync.cpp
  src/message_envelope.cpp
  src/message_registry.cpp
  src/player_input.cpp
  src/session_control.cpp
  src/skins.cpp
  src/world_join.cpp
)
target_include_directories(arv_protocol PUBLIC include)
target_link_libraries(arv_protocol PUBLIC arv_core arv_crypto arv_player arv_voxel)
