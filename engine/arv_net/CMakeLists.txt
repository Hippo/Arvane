set(ARV_NET_HEADERS
  include/arv/net/connection_state.hpp
  include/arv/net/gns_transport.hpp
  include/arv/net/in_process_transport.hpp
  include/arv/net/transport.hpp
)

add_library(arv_net INTERFACE)
target_include_directories(arv_net INTERFACE include)
target_link_libraries(arv_net INTERFACE arv_core arv_protocol GameNetworkingSockets::static)
