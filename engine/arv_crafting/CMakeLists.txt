set(ARV_CRAFTING_HEADERS
  include/arv/crafting/crafting.hpp
)

add_library(arv_crafting STATIC
  ${ARV_CRAFTING_HEADERS}
  src/crafting.cpp
)
target_include_directories(arv_crafting PUBLIC include)
target_link_libraries(arv_crafting
  PUBLIC
    arv_asset_core
    arv_core
    arv_player
  PRIVATE
    arv_io_data
)
