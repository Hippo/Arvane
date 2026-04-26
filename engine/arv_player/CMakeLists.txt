set(ARV_PLAYER_HEADERS
  include/arv/player/inventory.hpp
  include/arv/player/item.hpp
  include/arv/player/player_constants.hpp
  include/arv/player/player_state.hpp
)

add_library(arv_player STATIC
  src/inventory.cpp
  src/item.cpp
)
target_include_directories(arv_player PUBLIC include)
target_link_libraries(arv_player PUBLIC arv_core arv_voxel)
