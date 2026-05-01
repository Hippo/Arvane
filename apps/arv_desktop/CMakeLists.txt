
set(ARV_CLIENT_SOURCES
  src/app_config.cpp
  src/asset_helpers.cpp
  src/chunk_view.cpp
  src/desktop_application.cpp
  src/desktop_assets.cpp
  src/desktop_frame_pacing.cpp
  src/desktop_input_routing.cpp
  src/desktop_menu_state.cpp
  src/desktop_render_builders.cpp
  src/desktop_runtime_config.cpp
  src/desktop_save_helpers.cpp
  src/desktop_session.cpp
  src/debug_imgui.cpp
  src/debug_menu.cpp
  src/input_adapter.cpp
  src/main.cpp
  src/renderer_bootstrap.cpp
  src/save_state.cpp
  src/ui_model.cpp
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
  arv_ui
  arv_voxel
  arv_world
  imgui
  glfw
  OpenGL::GL
)
