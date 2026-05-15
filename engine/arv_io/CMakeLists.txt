set(ARV_IO_HEADERS
  include/arv/io/codec.hpp
  include/arv/io/config.hpp
  include/arv/io/data_codecs.hpp
  include/arv/io/data_document.hpp
  include/arv/io/files.hpp
  include/arv/io/input_bindings.hpp
  include/arv/io/storage.hpp
)

add_library(arv_io STATIC
  src/codec_binary.cpp
  src/codec_json.cpp
  src/codec_yaml.cpp
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
  src/data_codecs.cpp
  src/data_document.cpp
  src/files.cpp
  src/input_bindings.cpp
  src/storage.cpp
)
target_include_directories(arv_io PUBLIC include)
target_link_libraries(arv_io PUBLIC arv_core arv_platform)
target_link_libraries(arv_io PRIVATE yaml-cpp nlohmann_json::nlohmann_json)
target_compile_definitions(arv_io PRIVATE
  "ARVANE_PROJECT_ROOT=\"${PROJECT_SOURCE_DIR}\""
)
