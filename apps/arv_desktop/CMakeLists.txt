
set(ARV_CLIENT_SOURCES
  src/app_config.cpp
  src/asset_helpers.cpp
  src/chunk_view.cpp
  src/desktop_application.cpp
  src/desktop_assets.cpp
  src/desktop_chat_commands.cpp
  src/desktop_frame_pacing.cpp
  src/desktop_frame_loop.cpp
  src/desktop_gameplay_interactions.cpp
  src/desktop_input_routing.cpp
  src/desktop_menu_state.cpp
  src/desktop_menu_commands.cpp
  src/desktop_render_builders.cpp
  src/desktop_runtime_config.cpp
  src/desktop_render_thread.cpp
  src/desktop_save_helpers.cpp
  src/desktop_session.cpp
  src/desktop_simulation.cpp
  src/desktop_skin_textures.cpp
  src/desktop_ui_runtime.cpp
  src/debug_imgui.cpp
  src/debug_menu.cpp
  src/input_adapter.cpp
  src/main.cpp
  src/renderer_bootstrap.cpp
  src/save_state.cpp
  src/ui_model.cpp
)

add_executable(arv_desktop ${ARV_CLIENT_SOURCES})

include(FetchContent)

option(ARVANE_FETCH_NATIVE_FILE_DIALOG "Fetch nativefiledialog-extended when it is not provided by the system" ON)

function(arvane_select_nfd_target out_var)
  if(TARGET nfd::nfd)
    set(${out_var} nfd::nfd PARENT_SCOPE)
  elseif(TARGET NFD::nfd)
    set(${out_var} NFD::nfd PARENT_SCOPE)
  elseif(TARGET nativefiledialog-extended::nfd)
    set(${out_var} nativefiledialog-extended::nfd PARENT_SCOPE)
  elseif(TARGET nfd)
    set(${out_var} nfd PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

find_package(nfd CONFIG QUIET)
arvane_select_nfd_target(ARVANE_NFD_TARGET)

if(NOT ARVANE_NFD_TARGET)
  find_package(nativefiledialog-extended CONFIG QUIET)
  arvane_select_nfd_target(ARVANE_NFD_TARGET)
endif()

if(NOT ARVANE_NFD_TARGET AND ARVANE_FETCH_NATIVE_FILE_DIALOG)
  set(NFD_BUILD_TESTS OFF CACHE BOOL "Build nativefiledialog-extended tests" FORCE)
  set(NFD_INSTALL OFF CACHE BOOL "Enable nativefiledialog-extended install target" FORCE)
  FetchContent_Declare(
    nativefiledialog-extended
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG v1.3.0
    UPDATE_DISCONNECTED TRUE
  )
  FetchContent_MakeAvailable(nativefiledialog-extended)
  arvane_select_nfd_target(ARVANE_NFD_TARGET)
endif()

target_link_libraries(arv_desktop PRIVATE
  arv_base
  arv_base_ui
  arv_client
  arv_core
  arv_crypto
  arv_io
  arv_text
  arv_net
  arv_platform
  arv_persistence
  arv_plugin
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

if(ARVANE_NFD_TARGET)
  target_link_libraries(arv_desktop PRIVATE ${ARVANE_NFD_TARGET})
  target_compile_definitions(arv_desktop PRIVATE ARVANE_HAS_NFD=1)
else()
  target_compile_definitions(arv_desktop PRIVATE ARVANE_HAS_NFD=0)
endif()
