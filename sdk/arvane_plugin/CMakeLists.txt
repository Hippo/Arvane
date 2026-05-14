add_library(ArvaneSDK INTERFACE)
add_library(Arvane::SDK ALIAS ArvaneSDK)
add_library(Arvane::PluginSDK ALIAS ArvaneSDK)

target_include_directories(ArvaneSDK INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cpp/include>"
  "$<INSTALL_INTERFACE:include>"
)
target_compile_features(ArvaneSDK INTERFACE cxx_std_20)
target_compile_definitions(ArvaneSDK INTERFACE ARV_SDK=1)

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
