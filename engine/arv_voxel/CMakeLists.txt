set(ARV_VOXEL_HEADERS
  include/arv/voxel/block.hpp
  include/arv/voxel/fluid.hpp
  include/arv/voxel/light.hpp
  include/arv/voxel/storage.hpp
)

add_library(arv_voxel STATIC
  src/block.cpp
  src/storage.cpp
)
target_include_directories(arv_voxel PUBLIC include)
target_link_libraries(arv_voxel PUBLIC arv_core)
target_link_libraries(arv_voxel PRIVATE nlohmann_json::nlohmann_json)
