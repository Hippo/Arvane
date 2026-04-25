enable_testing()

include(GoogleTest)

add_executable(arv_tests
  client/camera_tests.cpp
  core/coordinate_tests.cpp
  core/fixed_timestep_tests.cpp
  core/id_tests.cpp
  core/math_tests.cpp
  io/config_tests.cpp
  net/integrated_handshake_tests.cpp
  protocol/handshake_tests.cpp
  protocol/packet_tests.cpp
  render/mesh_handoff_tests.cpp
  render/chunk_meshing_tests.cpp
  sched/scheduler_tests.cpp
  server/registry_tests.cpp
  voxel/block_registry_tests.cpp
  voxel/chunk_storage_tests.cpp
  voxel/light_tests.cpp
  world/fluid_tests.cpp
  world/lighting_tests.cpp
  world/world_generation_tests.cpp
  world/world_runtime_tests.cpp
)

target_link_libraries(arv_tests PRIVATE
  arv_client
  arv_core
  arv_io
  arv_net
  arv_protocol
  arv_render
  arv_sched
  arv_server
  arv_voxel
  arv_world
  GTest::gtest
  GTest::gtest_main
  GTest::gmock
  GTest::gmock_main
)
gtest_discover_tests(arv_tests)
