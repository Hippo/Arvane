set(ARV_DATA_HEADERS
  include/arv/data/codecs.hpp
  include/arv/data/document.hpp
  include/arv/data/storage.hpp
)

add_library(arv_data STATIC
  src/storage.cpp
)

target_include_directories(arv_data PUBLIC include)
target_link_libraries(arv_data
  PUBLIC
    arv_asset
    arv_core
    arv_io
)
