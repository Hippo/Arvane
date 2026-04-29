set(ARV_PLAYER_HEADERS
  include/arv/player/inventory.hpp
  include/arv/player/item.hpp
  include/arv/player/permissions.hpp
  include/arv/player/player_constants.hpp
  include/arv/player/player_state.hpp
)

add_library(arv_player STATIC
  src/inventory.cpp
  src/item.cpp
  src/permissions.cpp
)
target_include_directories(arv_player PUBLIC include)
target_link_libraries(arv_player PUBLIC arv_core arv_voxel)
target_link_libraries(arv_player PRIVATE nlohmann_json::nlohmann_json)
