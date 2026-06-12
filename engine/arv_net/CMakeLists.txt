set(ARV_NET_HEADERS
  include/arv/net/connection_state.hpp
  include/arv/net/in_process_transport.hpp
  include/arv/net/multiplex_transport.hpp
  include/arv/net/transport.hpp
  include/arv/net/websocket_client_transport.hpp
  include/arv/net/websocket_server_transport.hpp
)

add_library(arv_net STATIC
  src/transport.cpp
)
target_include_directories(arv_net PUBLIC include)
target_link_libraries(arv_net PUBLIC
  arv_protocol_headers
)

if(EMSCRIPTEN)
  target_sources(arv_net PRIVATE
    src/websocket_client_transport.cpp
  )
else()
  target_sources(arv_net PRIVATE
    src/in_process_transport.cpp
    src/multiplex_transport.cpp
    src/websocket_server_transport.cpp
  )
  target_link_libraries(arv_net PUBLIC
    concurrentqueue
    readerwriterqueue
    unordered_dense
  )
endif()

if(NOT EMSCRIPTEN)
  add_library(arv_net_gns STATIC
    src/gns_transport.cpp
  )
  target_include_directories(arv_net_gns PUBLIC include)
  target_link_libraries(arv_net_gns PUBLIC
    arv_net
    GameNetworkingSockets::static
    unordered_dense
  )
endif()
