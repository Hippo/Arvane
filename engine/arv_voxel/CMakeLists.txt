set(ARV_VOXEL_HEADERS
  include/arv/voxel/block.hpp
  include/arv/voxel/fluid.hpp
  include/arv/voxel/light.hpp
  include/arv/voxel/storage.hpp
)

add_library(arv_voxel INTERFACE)
target_include_directories(arv_voxel INTERFACE include)
target_link_libraries(arv_voxel INTERFACE arv_core nlohmann_json::nlohmann_json)
