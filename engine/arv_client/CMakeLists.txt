set(ARV_CLIENT_HEADERS
  include/arv/client/camera_shell.hpp
  include/arv/client/client_runtime.hpp
)

add_library(arv_client STATIC
  src/camera_shell.cpp
  src/client_runtime.cpp
)
target_include_directories(arv_client PUBLIC include)
target_link_libraries(arv_client PUBLIC arv_core arv_net arv_protocol arv_render)
