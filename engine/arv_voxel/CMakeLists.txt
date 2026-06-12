set(ARV_VOXEL_HEADERS
  include/arv/voxel/block.hpp
  include/arv/voxel/fluid.hpp
  include/arv/voxel/light.hpp
  include/arv/voxel/storage.hpp
)

add_library(arv_voxel_headers INTERFACE)
target_include_directories(arv_voxel_headers INTERFACE include)
target_link_libraries(arv_voxel_headers INTERFACE arv_core_headers)

if(EMSCRIPTEN AND ARVANE_BUILD_WEB)
  return()
endif()

add_library(arv_voxel STATIC
  src/block.cpp
  src/storage.cpp
)
target_include_directories(arv_voxel PUBLIC include)
target_link_libraries(arv_voxel PUBLIC arv_voxel_headers arv_core)
target_link_libraries(arv_voxel PRIVATE arv_io)
