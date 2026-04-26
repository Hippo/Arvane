set(ARV_IO_HEADERS
  include/arv/io/config.hpp
)

add_library(arv_io STATIC
  src/config.cpp
)
target_include_directories(arv_io PUBLIC include)
target_link_libraries(arv_io PUBLIC arv_core)
target_link_libraries(arv_io PRIVATE yaml-cpp nlohmann_json::nlohmann_json)
