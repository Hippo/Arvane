set(ARV_CLIENT_HEADERS
  include/arv/client/camera_shell.hpp
  include/arv/client/client_runtime.hpp
)

add_library(arv_client INTERFACE)
target_include_directories(arv_client INTERFACE include)
target_link_libraries(arv_client INTERFACE arv_core arv_net arv_protocol arv_render)
