cmake_minimum_required(VERSION 3.19)
project(arv_mason LANGUAGES CXX)

set(ARV_MASON_SOURCES
  src/plugin_main.cpp
  src/mason_plugin.cpp
  src/config.cpp
  src/selection/selection.cpp
  src/clipboard/clipboard.cpp
  src/operations/operation.cpp
  src/blueprints/blueprint.cpp
  src/history/history.cpp
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
add_custom_target(arv_mason_pack ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/pack"
          "${ARV_MASON_PACK_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${ARV_MASON_PACK_DIR}/native/${ARV_MASON_PLATFORM_KEY}"
  COMMAND ${CMAKE_COMMAND} -E copy
          "$<TARGET_FILE:arv_mason>"
          "${ARV_MASON_PACK_DIR}/native/${ARV_MASON_PLATFORM_KEY}/$<TARGET_FILE_NAME:arv_mason>"
  DEPENDS arv_mason
  VERBATIM
)

install(DIRECTORY pack/ DESTINATION packs/arv_mason)
install(TARGETS arv_mason
  LIBRARY DESTINATION packs/arv_mason/native/${ARV_MASON_PLATFORM_KEY}
  RUNTIME DESTINATION packs/arv_mason/native/${ARV_MASON_PLATFORM_KEY}
)

if(ARVANE_BUILD_TESTS AND TARGET GTest::gtest_main)
  add_executable(arv_mason_tests
    tests/selection_tests.cpp
    tests/operation_tests.cpp
    tests/blueprint_codec_tests.cpp
    src/selection/selection.cpp
    src/clipboard/clipboard.cpp
    src/operations/operation.cpp
    src/blueprints/blueprint.cpp
    src/history/history.cpp
  )
  target_include_directories(arv_mason_tests PRIVATE include)
  target_link_libraries(arv_mason_tests PRIVATE Arvane::PluginSDK GTest::gtest GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(arv_mason_tests)
endif()
