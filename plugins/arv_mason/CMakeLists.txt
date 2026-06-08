cmake_minimum_required(VERSION 3.19)
project(arv_mason LANGUAGES CXX)

set(ARV_MASON_SOURCES
  src/plugin_main.cpp
  src/config.cpp
  src/messages.cpp
  src/commands/command_utils.cpp
  src/commands/registration.cpp
  src/commands/player_state.cpp
  src/commands/marker_commands.cpp
  src/commands/edit_commands.cpp
  src/commands/clipboard_commands.cpp
  src/commands/history_commands.cpp
  src/selection/selection.cpp
  src/clipboard/clipboard.cpp
  src/operations/operation.cpp
  src/history/history.cpp
  src/world/block_resolution.cpp
)

add_library(arv_mason SHARED ${ARV_MASON_SOURCES})
add_library(Arvane::MasonPlugin ALIAS arv_mason)

target_include_directories(arv_mason
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(arv_mason
  PRIVATE
    Arvane::PluginSDK
)
target_compile_features(arv_mason PRIVATE cxx_std_20)
set_target_properties(arv_mason PROPERTIES
  OUTPUT_NAME "arv_mason"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)

set(ARV_MASON_PLATFORM_KEY "linux-x86_64")
if(WIN32)
  set(ARV_MASON_PLATFORM_KEY "windows-x86_64")
elseif(APPLE)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(ARV_MASON_PLATFORM_KEY "macos-arm64")
  else()
    set(ARV_MASON_PLATFORM_KEY "macos-x86_64")
  endif()
endif()

set(ARV_MASON_PACK_DIR "${CMAKE_BINARY_DIR}/arvane_home/packs/arv_mason")
set(ARV_MASON_DEV_PACK_DIR "${CMAKE_SOURCE_DIR}/arvane_home/packs/arv_mason")
get_filename_component(ARV_MASON_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(ARV_MASON_CONFIG_SOURCE_DIR "${ARV_MASON_REPO_ROOT}/arvane_home/config/plugins/arv_mason")
set(ARV_MASON_BUILD_CONFIG_DIR "${CMAKE_BINARY_DIR}/arvane_home/config/plugins/arv_mason")

function(arv_mason_add_pack_copy_target TARGET_NAME PACK_DIR)
  set(ALL_ARG)
  if(ARGC GREATER 2 AND ARGV2)
    set(ALL_ARG ALL)
  endif()

  add_custom_target(${TARGET_NAME} ${ALL_ARG}
    COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${PACK_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/pack"
            "${PACK_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${PACK_DIR}/native/${ARV_MASON_PLATFORM_KEY}"
    COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:arv_mason>"
            "${PACK_DIR}/native/${ARV_MASON_PLATFORM_KEY}/$<TARGET_FILE_NAME:arv_mason>"
    DEPENDS arv_mason
    VERBATIM
  )
endfunction()

arv_mason_add_pack_copy_target(arv_mason_pack "${ARV_MASON_PACK_DIR}" TRUE)
arv_mason_add_pack_copy_target(arv_mason_install_pack "${ARV_MASON_DEV_PACK_DIR}")

if(EXISTS "${ARV_MASON_CONFIG_SOURCE_DIR}")
  add_custom_target(arv_mason_config ALL
    COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${ARV_MASON_BUILD_CONFIG_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ARV_MASON_CONFIG_SOURCE_DIR}"
            "${ARV_MASON_BUILD_CONFIG_DIR}"
    VERBATIM
  )
endif()

install(DIRECTORY pack/ DESTINATION packs/arv_mason)
if(EXISTS "${ARV_MASON_CONFIG_SOURCE_DIR}")
  install(DIRECTORY "${ARV_MASON_CONFIG_SOURCE_DIR}/" DESTINATION config/plugins/arv_mason)
endif()
install(TARGETS arv_mason
  LIBRARY DESTINATION packs/arv_mason/native/${ARV_MASON_PLATFORM_KEY}
  RUNTIME DESTINATION packs/arv_mason/native/${ARV_MASON_PLATFORM_KEY}
)

if(ARVANE_BUILD_TESTS AND TARGET GTest::gtest_main)
  add_executable(arv_mason_tests
    tests/selection_tests.cpp
    tests/operation_tests.cpp
    src/selection/selection.cpp
    src/clipboard/clipboard.cpp
    src/operations/operation.cpp
    src/history/history.cpp
  )
  target_include_directories(arv_mason_tests PRIVATE include)
  target_link_libraries(arv_mason_tests PRIVATE Arvane::PluginSDK GTest::gtest GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(arv_mason_tests)
endif()
