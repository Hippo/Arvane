if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "arv_web must be configured with Emscripten")
endif()

set(ARV_WEB_DIST_DIR "${CMAKE_CURRENT_BINARY_DIR}/dist")
set(ARV_WEB_SHELL_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/shell/index.html"
  "${CMAKE_CURRENT_SOURCE_DIR}/shell/loader.js"
  "${CMAKE_CURRENT_SOURCE_DIR}/shell/styles.css"
)

add_executable(arv_web
  src/main.cpp
  src/web_application.cpp
  src/web_entity_fallback.cpp
  src/web_input.cpp
  src/web_platform.cpp
  src/web_player_skin_atlas.cpp
)

target_compile_features(arv_web PRIVATE cxx_std_20)
target_link_libraries(arv_web
  PRIVATE
    arv_client_chunk_snapshot
    arv_client_chunk_collision
    arv_client_runtime
    arv_client_item_animation
    arv_client_local_player
    arv_client_item_rendering
    arv_client_item_ui
    arv_client_render_assets
    arv_base_ui
    arv_net
    arv_physics
    arv_platform_input
    arv_render_mesh
    arv_ui
)
set_target_properties(arv_web PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${ARV_WEB_DIST_DIR}"
  OUTPUT_NAME "arv_web"
  SUFFIX ".js"
)

target_link_options(arv_web PRIVATE
  "-lidbfs.js"
  "-sENVIRONMENT=web"
  "-sUSE_WEBGL2=1"
  "-sMIN_WEBGL_VERSION=2"
  "-sMAX_WEBGL_VERSION=2"
  "-sALLOW_MEMORY_GROWTH=1"
  "-sINITIAL_MEMORY=67108864"
  "-sSTACK_SIZE=1048576"
  "-sFILESYSTEM=1"
  "-sFORCE_FILESYSTEM=1"
  "-sASSERTIONS=1"
  "-sEXPORTED_FUNCTIONS=['_main','_arv_web_frame','_arv_web_connect','_arv_web_disconnect','_arv_web_crypto_self_test']"
  "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']"
)

add_custom_target(arv_web_assets
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${ARV_WEB_DIST_DIR}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/shell/index.html"
          "${ARV_WEB_DIST_DIR}/index.html"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/shell/loader.js"
          "${ARV_WEB_DIST_DIR}/loader.js"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_SOURCE_DIR}/shell/styles.css"
          "${ARV_WEB_DIST_DIR}/styles.css"
  DEPENDS ${ARV_WEB_SHELL_FILES}
  VERBATIM
)
add_dependencies(arv_web arv_web_assets)
