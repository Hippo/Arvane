enable_language(C)

add_library(ArvaneSDK STATIC
  src/arvane_plugin.c
  cpp/src/buffer.cpp
  cpp/src/commands.cpp
  cpp/src/config.cpp
  cpp/src/context.cpp
  cpp/src/data.cpp
  cpp/src/entities.cpp
  cpp/src/events.cpp
  cpp/src/gamemode.cpp
  cpp/src/logger.cpp
  cpp/src/metrics.cpp
  cpp/src/network.cpp
  cpp/src/permissions.cpp
  cpp/src/persistent_data.cpp
  cpp/src/plugin.cpp
  cpp/src/regions.cpp
  cpp/src/registries.cpp
  cpp/src/resources.cpp
  cpp/src/result.cpp
  cpp/src/scheduler.cpp
  cpp/src/services.cpp
  cpp/src/text.cpp
  cpp/src/types.cpp
  cpp/src/world.cpp
  cpp/src/worldgen.cpp
)
add_library(Arvane::SDK ALIAS ArvaneSDK)
add_library(Arvane::PluginSDK ALIAS ArvaneSDK)

target_include_directories(ArvaneSDK PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cpp/include>"
  "$<INSTALL_INTERFACE:include>"
)
target_compile_features(ArvaneSDK PUBLIC cxx_std_20)
target_compile_definitions(ArvaneSDK PUBLIC ARV_SDK=1)
set_target_properties(ArvaneSDK PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
  cmake/ArvaneSDKConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/ArvaneSDKConfig.cmake"
  INSTALL_DESTINATION lib/cmake/ArvaneSDK
)

install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY cpp/include/ DESTINATION include)
install(
  TARGETS ArvaneSDK
  EXPORT ArvaneSDKTargets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(
  EXPORT ArvaneSDKTargets
  NAMESPACE Arvane::
  DESTINATION lib/cmake/ArvaneSDK
)
install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/ArvaneSDKConfig.cmake"
  DESTINATION lib/cmake/ArvaneSDK
)
