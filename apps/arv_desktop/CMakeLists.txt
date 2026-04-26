
set(ARV_CLIENT_SOURCES
  src/main.cpp
)

add_executable(arv_desktop ${ARV_CLIENT_SOURCES})
target_link_libraries(arv_desktop PRIVATE
  arv_client
  arv_core
  arv_io
  arv_net
  arv_platform
  arv_persistence
  arv_player
  arv_render
  arv_sched
  arv_server
  arv_voxel
  arv_world
)
