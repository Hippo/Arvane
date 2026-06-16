set(ARV_PLAYER_HEADERS
  include/arv/player/container.hpp
  include/arv/player/inventory.hpp
  include/arv/player/item.hpp
  include/arv/player/permissions.hpp
  include/arv/player/player_constants.hpp
  include/arv/player/player_state.hpp
  include/arv/player/selection.hpp
)

add_library(arv_player_headers INTERFACE)
target_include_directories(arv_player_headers INTERFACE include)
target_link_libraries(arv_player_headers INTERFACE arv_core_headers arv_io_headers arv_voxel_headers)

add_library(arv_player STATIC
  src/container.cpp
  src/inventory.cpp
  src/item.cpp
  src/permissions.cpp
  src/selection.cpp
)
target_include_directories(arv_player PUBLIC include)
target_link_libraries(arv_player PUBLIC arv_player_headers arv_core arv_io_data arv_voxel)
