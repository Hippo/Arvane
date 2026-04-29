set(ARV_WORLD_HEADERS
  include/arv/world/fluid_solver.hpp
  include/arv/world/regions.hpp
  include/arv/world/world_lighting.hpp
  include/arv/world/world_generation.hpp
  include/arv/world/world_runtime.hpp
  include/arv/world/world_queries.hpp
)

add_library(arv_world STATIC
  src/fluid_solver.cpp
  src/regions.cpp
  src/world_lighting.cpp
  src/world_generation.cpp
  src/world_runtime.cpp
)
target_include_directories(arv_world PUBLIC include)
target_link_libraries(arv_world PUBLIC arv_core arv_physics arv_player arv_voxel)
target_link_libraries(arv_world PRIVATE yaml-cpp FastNoise2)
