cmake_minimum_required(VERSION 3.19)
project(arv_perms LANGUAGES CXX)

set(ARV_PERMS_CORE_SOURCES
  src/permission_store.cpp
  src/arv_perms_plugin.cpp
)

add_library(arv_perms_core STATIC ${ARV_PERMS_CORE_SOURCES})
add_library(Arvane::PermsCore ALIAS arv_perms_core)
target_include_directories(arv_perms_core PUBLIC include)
target_link_libraries(arv_perms_core PUBLIC Arvane::PluginSDK)
target_compile_features(arv_perms_core PUBLIC cxx_std_20)
set_target_properties(arv_perms_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(arv_perms SHARED
  src/plugin_main.cpp
)
add_library(Arvane::PermsPlugin ALIAS arv_perms)
target_link_libraries(arv_perms PRIVATE arv_perms_core)
target_compile_features(arv_perms PRIVATE cxx_std_20)
set_target_properties(arv_perms PROPERTIES
  OUTPUT_NAME "arv_perms"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)

set(ARV_PERMS_PLATFORM_KEY "linux-x86_64")
if(WIN32)
  set(ARV_PERMS_PLATFORM_KEY "windows-x86_64")
elseif(APPLE)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(ARV_PERMS_PLATFORM_KEY "macos-arm64")
  else()
    set(ARV_PERMS_PLATFORM_KEY "macos-x86_64")
  endif()
endif()

set(ARV_PERMS_PACK_DIR "${CMAKE_BINARY_DIR}/arvane_home/packs/arv_perms")
set(ARV_PERMS_DEV_PACK_DIR "${CMAKE_SOURCE_DIR}/arvane_home/packs/arv_perms")
get_filename_component(ARV_PERMS_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(ARV_PERMS_CONFIG_SOURCE_DIR "${ARV_PERMS_REPO_ROOT}/arvane_home/config/plugins/arv_perms")
set(ARV_PERMS_BUILD_CONFIG_DIR "${CMAKE_BINARY_DIR}/arvane_home/config/plugins/arv_perms")

function(arv_perms_add_pack_copy_target TARGET_NAME PACK_DIR)
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
            "${PACK_DIR}/native/${ARV_PERMS_PLATFORM_KEY}"
    COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:arv_perms>"
            "${PACK_DIR}/native/${ARV_PERMS_PLATFORM_KEY}/$<TARGET_FILE_NAME:arv_perms>"
    DEPENDS arv_perms
    VERBATIM
  )
endfunction()

arv_perms_add_pack_copy_target(arv_perms_pack "${ARV_PERMS_PACK_DIR}" TRUE)
arv_perms_add_pack_copy_target(arv_perms_install_pack "${ARV_PERMS_DEV_PACK_DIR}")

if(EXISTS "${ARV_PERMS_CONFIG_SOURCE_DIR}")
  add_custom_target(arv_perms_config ALL
    COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${ARV_PERMS_BUILD_CONFIG_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ARV_PERMS_CONFIG_SOURCE_DIR}"
            "${ARV_PERMS_BUILD_CONFIG_DIR}"
    VERBATIM
  )
endif()

install(DIRECTORY pack/ DESTINATION packs/arv_perms)
if(EXISTS "${ARV_PERMS_CONFIG_SOURCE_DIR}")
  install(DIRECTORY "${ARV_PERMS_CONFIG_SOURCE_DIR}/" DESTINATION config/plugins/arv_perms)
endif()
install(TARGETS arv_perms
  LIBRARY DESTINATION packs/arv_perms/native/${ARV_PERMS_PLATFORM_KEY}
  RUNTIME DESTINATION packs/arv_perms/native/${ARV_PERMS_PLATFORM_KEY}
)
