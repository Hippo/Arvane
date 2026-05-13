set(ARV_DATA_HEADERS
  include/arv/data/codecs.hpp
  include/arv/data/document.hpp
  include/arv/data/storage.hpp
)

add_library(arv_data STATIC
  src/codecs.cpp
  src/document.cpp
  src/storage.cpp
)

target_include_directories(arv_data PUBLIC include)
target_link_libraries(arv_data
  PUBLIC
    arv_asset
    arv_core
  PRIVATE
    yaml-cpp::yaml-cpp
    nlohmann_json::nlohmann_json
)
