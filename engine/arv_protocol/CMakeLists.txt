set(ARV_PROTOCOL_HEADERS
  include/arv/protocol/chunk_streaming.hpp
  include/arv/protocol/handshake.hpp
  include/arv/protocol/message_envelope.hpp
  include/arv/protocol/message_registry.hpp
  include/arv/protocol/message_type.hpp
  include/arv/protocol/message_writer.hpp
  include/arv/protocol/player_input.hpp
  include/arv/protocol/protocol_version.hpp
)

add_library(arv_protocol STATIC
  src/chunk_streaming.cpp
  src/handshake.cpp
  src/message_envelope.cpp
  src/message_registry.cpp
  src/player_input.cpp
)
target_include_directories(arv_protocol PUBLIC include)
target_link_libraries(arv_protocol PUBLIC arv_core arv_player arv_voxel)
