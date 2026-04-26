set(ARV_NET_HEADERS
  include/arv/net/connection_state.hpp
  include/arv/net/gns_transport.hpp
  include/arv/net/in_process_transport.hpp
  include/arv/net/transport.hpp
)

add_library(arv_net STATIC
  src/gns_transport.cpp
  src/in_process_transport.cpp
  src/transport.cpp
)
target_include_directories(arv_net PUBLIC include)
target_link_libraries(arv_net PUBLIC arv_core arv_protocol GameNetworkingSockets::static)
