set(ARV_WORLD_HEADERS
  include/arv/world/world_generation.hpp
  include/arv/world/world_runtime.hpp
)

add_library(arv_world INTERFACE)
target_include_directories(arv_world INTERFACE include)
target_link_libraries(arv_world INTERFACE arv_core arv_voxel yaml-cpp FastNoise2)
