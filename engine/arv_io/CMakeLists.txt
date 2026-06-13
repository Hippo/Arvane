set(ARV_IO_HEADERS
  include/arv/io/codec.hpp
  include/arv/io/config.hpp
  include/arv/io/data_codecs.hpp
  include/arv/io/data_document.hpp
  include/arv/io/files.hpp
  include/arv/io/input_bindings.hpp
  include/arv/io/storage.hpp
)

add_library(arv_io_headers INTERFACE)
target_include_directories(arv_io_headers INTERFACE include)
target_link_libraries(arv_io_headers INTERFACE arv_core_headers)

add_library(arv_io_data STATIC
  src/codec_binary.cpp
  src/codec_json.cpp
  src/codec_yaml.cpp
  src/data_codecs.cpp
  src/data_document.cpp
  src/files.cpp
  src/storage.cpp
)
target_include_directories(arv_io_data PUBLIC include)
target_link_libraries(arv_io_data PUBLIC arv_io_headers arv_core)
target_link_libraries(arv_io_data PRIVATE yaml-cpp nlohmann_json::nlohmann_json)

if(EMSCRIPTEN AND ARVANE_BUILD_WEB)
  return()
endif()

add_library(arv_io STATIC
  src/config.cpp
  src/config_common.cpp
  src/config_documents.cpp
  src/config_gameplay.cpp
  src/config_network.cpp
  src/config_plugins.cpp
  src/config_render_ui.cpp
  src/config_runtime.cpp
  src/config_server_list.cpp
  src/config_validation.cpp
  src/input_bindings.cpp
)
target_include_directories(arv_io PUBLIC include)
target_link_libraries(arv_io PUBLIC arv_io_data arv_core arv_platform)
target_link_libraries(arv_io PRIVATE yaml-cpp nlohmann_json::nlohmann_json)
target_compile_definitions(arv_io PRIVATE
  "ARVANE_PROJECT_ROOT=\"${PROJECT_SOURCE_DIR}\""
)
