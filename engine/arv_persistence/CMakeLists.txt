set(ARV_PERSISTENCE_HEADERS
  include/arv/persistence/region_file.hpp
  include/arv/persistence/save.hpp
)

add_library(arv_persistence STATIC
  src/region_file.cpp
  src/save.cpp
)
target_include_directories(arv_persistence PUBLIC include)
target_link_libraries(arv_persistence PUBLIC arv_asset arv_core arv_crypto arv_player arv_voxel arv_world)
target_link_libraries(arv_persistence PRIVATE arv_data arv_io)
