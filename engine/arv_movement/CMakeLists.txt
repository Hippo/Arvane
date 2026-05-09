set(ARV_MOVEMENT_HEADERS
  include/arv/movement/player_movement.hpp
)

add_library(arv_movement STATIC
  src/player_movement.cpp
)
target_include_directories(arv_movement PUBLIC include)
target_link_libraries(arv_movement PUBLIC arv_core arv_physics arv_player)
