set(ARV_PLUGIN_HEADERS
  include/arv/plugin/plugin_manager.hpp
)

add_library(arv_plugin STATIC
  src/dynamic_library.cpp
  src/plugin_manager.cpp
)

target_include_directories(arv_plugin PUBLIC include)
target_link_libraries(arv_plugin
  PUBLIC
    Arvane::SDK
    arv_asset
    arv_command
    arv_core
    arv_entity
    arv_player
    arv_protocol
    arv_sched
    arv_voxel
    arv_world
  PRIVATE
    ${CMAKE_DL_LIBS}
)
