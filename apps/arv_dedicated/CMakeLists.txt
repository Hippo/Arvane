set(ARV_DEDICATED_SOURCES
  src/dedicated_application.cpp
  src/dedicated_launch_options.cpp
  src/dedicated_lifetime_log.cpp
  src/dedicated_registry_loader.cpp
  src/dedicated_runtime_config.cpp
  src/dedicated_runtime_layout.cpp
  src/dedicated_save_state.cpp
  src/dedicated_server_bootstrap.cpp
  src/dedicated_signals.cpp
  src/dedicated_tick_loop.cpp
  src/dedicated_worldgen_loader.cpp
  src/main.cpp
)

add_executable(arv_dedicated ${ARV_DEDICATED_SOURCES})
add_custom_command(TARGET arv_dedicated POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/arvane_server"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:arv_dedicated>"
          "${CMAKE_SOURCE_DIR}/arvane_server/$<TARGET_FILE_NAME:arv_dedicated>"
  VERBATIM
)
target_link_libraries(arv_dedicated PRIVATE
  arv_asset
  arv_base
  arv_core
  arv_crypto
  arv_entity
  arv_io
  arv_text
  arv_net
  arv_net_gns
  arv_persistence
  arv_plugin
  arv_player
  arv_sched
  arv_server
  arv_voxel
  arv_world
)
