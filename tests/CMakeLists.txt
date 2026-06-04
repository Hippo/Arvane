enable_testing()

include(GoogleTest)

add_library(arv_test_example_plugin SHARED
  plugin/fixtures/example_plugin.cpp
)
target_link_libraries(arv_test_example_plugin PRIVATE Arvane::SDK)

add_library(arv_test_failing_plugin SHARED
  plugin/fixtures/failing_plugin.cpp
)
target_link_libraries(arv_test_failing_plugin PRIVATE Arvane::SDK)

add_library(arv_test_missing_symbol_plugin SHARED
  plugin/fixtures/missing_symbol_plugin.cpp
)
target_link_libraries(arv_test_missing_symbol_plugin PRIVATE Arvane::SDK)

add_library(arv_plugin_sdk_c_compile OBJECT
  plugin/sdk_c_compile_test.c
)
target_link_libraries(arv_plugin_sdk_c_compile PRIVATE Arvane::SDK)

add_library(arv_plugin_sdk_cpp_compile OBJECT
  plugin/sdk_cpp_compile_test.cpp
)
target_link_libraries(arv_plugin_sdk_cpp_compile PRIVATE Arvane::SDK)

add_executable(arv_tests
  assets/asset_tests.cpp
  assets/pack_tests.cpp
  base/base_behavior_tests.cpp
  blueprint/blueprint_codec_tests.cpp
  client/chunk_interest_tests.cpp
  client/client_runtime_tests.cpp
  client/camera_tests.cpp
  client/item_animation_tests.cpp
  client/local_player_controller_tests.cpp
  client/particle_system_tests.cpp
  client/local_profile_tests.cpp
  client/ui_tests.cpp
  command/command_tests.cpp
  crypto/crypto_tests.cpp
  core/coordinate_tests.cpp
  core/fixed_timestep_tests.cpp
  core/id_tests.cpp
  core/math_tests.cpp
  core/rate_limiter_tests.cpp
  core/types_tests.cpp
  data/data_document_tests.cpp
  desktop/chunk_view_tests.cpp
  desktop/debug_imgui_tests.cpp
  ${CMAKE_SOURCE_DIR}/apps/arv_desktop/src/chunk_view.cpp
  ${CMAKE_SOURCE_DIR}/apps/arv_desktop/src/debug_imgui.cpp
  ${CMAKE_SOURCE_DIR}/apps/arv_desktop/src/desktop_render_builders.cpp
  io/config_tests.cpp
  text/language_tests.cpp
  text/rich_text_tests.cpp
  net/in_process_transport_tests.cpp
  net/integrated_handshake_tests.cpp
  persistence/region_file_tests.cpp
  persistence/save_tests.cpp
  physics/collision_tests.cpp
  protocol/handshake_tests.cpp
  protocol/auth_tests.cpp
  protocol/chat_tests.cpp
  protocol/packet_tests.cpp
  player/inventory_tests.cpp
  player/permission_tests.cpp
  player/selection_tests.cpp
  plugin/plugin_system_tests.cpp
  render/entity_model_tests.cpp
  render/item_model_tests.cpp
  render/mesh_handoff_tests.cpp
  render/paged_texture_atlas_tests.cpp
  render/render_commands_tests.cpp
  render/render_culling_tests.cpp
  render/chunk_meshing_tests.cpp
  sched/job_system_tests.cpp
  server/chunk_streamer_tests.cpp
  server/chat_service_tests.cpp
  server/player_movement_validation_tests.cpp
  server/permission_config_tests.cpp
  server/player_profile_tests.cpp
  server/player_interaction_tests.cpp
  server/registry_tests.cpp
  server/skin_runtime_tests.cpp
  voxel/block_registry_tests.cpp
  voxel/chunk_storage_tests.cpp
  voxel/light_tests.cpp
  world/entity_registry_tests.cpp
  world/fluid_tests.cpp
  world/entity_tests.cpp
  world/lighting_tests.cpp
  world/natural_spawning_tests.cpp
  world/region_tests.cpp
  world/world_generation_tests.cpp
  world/world_runtime_tests.cpp
  world/world_time_tests.cpp
)

target_include_directories(arv_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/apps/arv_desktop/src
)
target_compile_definitions(arv_tests PRIVATE
  ARVANE_TEST_ASSET_ROOT="${CMAKE_SOURCE_DIR}/arvane_home/packs/arvane_base/assets/arvane"
  ARV_TEST_EXAMPLE_PLUGIN_PATH="$<TARGET_FILE:arv_test_example_plugin>"
  ARV_TEST_FAILING_PLUGIN_PATH="$<TARGET_FILE:arv_test_failing_plugin>"
  ARV_TEST_MISSING_SYMBOL_PLUGIN_PATH="$<TARGET_FILE:arv_test_missing_symbol_plugin>"
  ARV_TEST_MASON_PLUGIN_PATH="$<TARGET_FILE:arv_mason>"
)
add_dependencies(arv_tests
  arv_test_example_plugin
  arv_test_failing_plugin
  arv_test_missing_symbol_plugin
  arv_plugin_sdk_c_compile
  arv_plugin_sdk_cpp_compile
  arv_mason
)

target_link_libraries(arv_tests PRIVATE
  arv_asset
  arv_base
  arv_base_ui
  arv_blueprint
  arv_client
  arv_command
  arv_core
  arv_crypto
  arv_entity
  arv_io
  arv_text
  arv_net
  arv_persistence
  arv_physics
  arv_plugin
  arv_player
  arv_protocol
  arv_render
  arv_sched
  arv_server
  arv_ui
  arv_voxel
  arv_world
  GTest::gtest
  GTest::gtest_main
  GTest::gmock
  GTest::gmock_main
  imgui
  glfw
  OpenGL::GL
)
gtest_discover_tests(arv_tests)
