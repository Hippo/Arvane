add_executable(arv_benchmarks
  chunk_streaming_benchmarks.cpp
  core_benchmarks.cpp
  io_benchmarks.cpp
  lighting_benchmarks.cpp
  meshing_benchmarks.cpp
  natural_spawning_benchmarks.cpp
  protocol_benchmarks.cpp
  simulation_benchmarks.cpp
  world_benchmarks.cpp
  ../apps/arv_desktop/src/chunk_view.cpp
)

target_include_directories(arv_benchmarks PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/apps/arv_desktop/src
)

target_link_libraries(arv_benchmarks PRIVATE
  arv_client
  arv_core
  arv_io
  arv_persistence
  arv_protocol
  arv_render
  arv_server
  arv_sched
  arv_voxel
  arv_world
  benchmark::benchmark_main
)
