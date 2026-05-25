add_executable(arv_benchmarks
  core_benchmarks.cpp
  io_benchmarks.cpp
  lighting_benchmarks.cpp
  meshing_benchmarks.cpp
  natural_spawning_benchmarks.cpp
  protocol_benchmarks.cpp
  simulation_benchmarks.cpp
  world_benchmarks.cpp
)

target_include_directories(arv_benchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(arv_benchmarks PRIVATE
  arv_core
  arv_persistence
  arv_protocol
  arv_render
  arv_voxel
  arv_world
  benchmark::benchmark_main
)
