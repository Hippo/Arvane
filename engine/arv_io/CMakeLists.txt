set(ARV_IO_HEADERS
  include/arv/io/config.hpp
)

add_library(arv_io INTERFACE)
target_include_directories(arv_io INTERFACE include)
target_link_libraries(arv_io INTERFACE arv_core yaml-cpp nlohmann_json::nlohmann_json)
