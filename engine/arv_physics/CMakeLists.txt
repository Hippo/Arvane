set(ARV_PHYSICS_HEADERS
  include/arv/physics/collision.hpp
  include/arv/physics/collision_query.hpp
  include/arv/physics/collision_resolution.hpp
  include/arv/physics/kinematics.hpp
  include/arv/physics/player_movement.hpp
  include/arv/physics/raycast.hpp
)

add_library(arv_physics STATIC
  src/collision_resolution.cpp
  src/kinematics.cpp
  src/player_movement.cpp
  src/raycast.cpp
)
target_include_directories(arv_physics PUBLIC include)
target_link_libraries(arv_physics PUBLIC arv_core_headers arv_player_headers arv_voxel_headers)
if(TARGET arv_core)
  target_link_libraries(arv_physics PUBLIC arv_core)
endif()
if(TARGET arv_voxel)
  target_link_libraries(arv_physics PUBLIC arv_voxel)
endif()
