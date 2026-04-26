set(ARV_PHYSICS_HEADERS
  include/arv/physics/collision.hpp
)

add_library(arv_physics STATIC
  src/collision.cpp
)
target_include_directories(arv_physics PUBLIC include)
target_link_libraries(arv_physics PUBLIC arv_core arv_voxel)
