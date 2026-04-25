set(ARV_PROTOCOL_HEADERS
  include/arv/protocol/handshake.hpp
  include/arv/protocol/message_envelope.hpp
  include/arv/protocol/message_registry.hpp
  include/arv/protocol/message_type.hpp
  include/arv/protocol/message_writer.hpp
  include/arv/protocol/protocol_version.hpp
)

add_library(arv_protocol INTERFACE)
target_include_directories(arv_protocol INTERFACE include)
target_link_libraries(arv_protocol INTERFACE arv_core)
