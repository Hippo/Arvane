add_library(ArvanePluginSDK INTERFACE)
add_library(Arvane::PluginSDK ALIAS ArvanePluginSDK)

target_include_directories(ArvanePluginSDK INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cpp/include>"
  "$<INSTALL_INTERFACE:include>"
)
target_compile_features(ArvanePluginSDK INTERFACE cxx_std_20)
target_compile_definitions(ArvanePluginSDK INTERFACE ARV_PLUGIN_SDK=1)

include(CMakePackageConfigHelpers)

configure_package_config_file(
  cmake/ArvanePluginSDKConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/ArvanePluginSDKConfig.cmake"
  INSTALL_DESTINATION lib/cmake/ArvanePluginSDK
)

install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY cpp/include/ DESTINATION include)
install(
  TARGETS ArvanePluginSDK
  EXPORT ArvanePluginSDKTargets
)
install(
  EXPORT ArvanePluginSDKTargets
  NAMESPACE Arvane::
  DESTINATION lib/cmake/ArvanePluginSDK
)
install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/ArvanePluginSDKConfig.cmake"
  DESTINATION lib/cmake/ArvanePluginSDK
)
