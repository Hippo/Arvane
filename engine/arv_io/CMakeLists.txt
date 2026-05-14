set(ARV_IO_HEADERS
  include/arv/io/config.hpp
  include/arv/io/files.hpp
  include/arv/io/input_bindings.hpp
)

add_library(arv_io STATIC
  src/config.cpp
  src/files.cpp
  src/input_bindings.cpp
)
target_include_directories(arv_io PUBLIC include)
target_link_libraries(arv_io PUBLIC arv_core arv_platform)
target_link_libraries(arv_io PRIVATE yaml-cpp nlohmann_json::nlohmann_json)
target_compile_definitions(arv_io PRIVATE
  "ARVANE_PROJECT_ROOT=\"${PROJECT_SOURCE_DIR}\""
)
