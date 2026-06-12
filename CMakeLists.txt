cmake_minimum_required(VERSION 3.19)
project(arvane)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ARVANE_BUILD_TESTS "Build Arvane tests" ON)
option(ARVANE_BUILD_BENCHMARKS "Build Arvane benchmarks" ON)
option(ARVANE_BUILD_WEB "Build the Emscripten browser client" OFF)
option(ARVANE_BUILD_SDK_EXAMPLES "Build standalone SDK examples" ON)
option(ARVANE_ENABLE_PROFILING "Enable Arvane profiling instrumentation and timing counters" ON)
option(ARVANE_ENABLE_TRACY "Enable Tracy profiling zones when profiling is enabled" ON)
option(ARVANE_FETCH_DEPENDENCIES "Fetch package-style third-party dependencies when system packages are unavailable or not preferred" ON)
option(ARVANE_USE_SYSTEM_DEPENDENCIES "Prefer system packages for dependencies that also support FetchContent" OFF)

if(EMSCRIPTEN AND ARVANE_BUILD_WEB)
  set(ARVANE_BUILD_TESTS OFF CACHE BOOL "Build Arvane tests" FORCE)
  set(ARVANE_BUILD_BENCHMARKS OFF CACHE BOOL "Build Arvane benchmarks" FORCE)
  set(ARVANE_BUILD_SDK_EXAMPLES OFF CACHE BOOL "Build standalone SDK examples" FORCE)
endif()

if(NOT ARVANE_BUILD_TESTS)
  set(BUILD_TESTING OFF CACHE BOOL "Build tests" FORCE)
endif()

if(EMSCRIPTEN AND ARVANE_BUILD_WEB)
  include(FetchContent)
  set(BUILD_TESTING OFF CACHE BOOL "Build dependency tests" FORCE)
  FetchContent_Declare(
    concurrentqueue
    GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
    GIT_TAG v1.0.5
    UPDATE_DISCONNECTED TRUE
  )
  FetchContent_MakeAvailable(concurrentqueue)
  FetchContent_Declare(
    readerwriterqueue
    GIT_REPOSITORY https://github.com/cameron314/readerwriterqueue.git
    GIT_TAG v1.0.7
    UPDATE_DISCONNECTED TRUE
  )
  FetchContent_MakeAvailable(readerwriterqueue)
  FetchContent_Declare(
    unordered_dense
    GIT_REPOSITORY https://github.com/martinus/unordered_dense.git
    GIT_TAG v4.8.1
    UPDATE_DISCONNECTED TRUE
  )
  FetchContent_MakeAvailable(unordered_dense)
  FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.17.0
    UPDATE_DISCONNECTED TRUE
  )
  FetchContent_MakeAvailable(spdlog)
  set(ZSTD_BUILD_STATIC ON CACHE BOOL "Build zstd static library" FORCE)
  set(ZSTD_BUILD_SHARED OFF CACHE BOOL "Build zstd shared library" FORCE)
  set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "Build zstd command-line tools" FORCE)
  set(ZSTD_BUILD_TESTS OFF CACHE BOOL "Build zstd tests" FORCE)
  FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG v1.5.7
    SOURCE_SUBDIR build/cmake
    UPDATE_DISCONNECTED TRUE
  )
  FetchContent_MakeAvailable(zstd)
  add_subdirectory(engine/arv_core)
  add_subdirectory(engine/arv_io)
  add_subdirectory(engine/arv_crypto)
  add_subdirectory(engine/arv_voxel)
  add_subdirectory(engine/arv_player)
  add_subdirectory(engine/arv_physics)
  add_subdirectory(engine/arv_protocol)
  add_subdirectory(engine/arv_net)
  add_subdirectory(engine/arv_asset)
  add_subdirectory(engine/arv_client)
  add_subdirectory(apps)
  return()
endif()

include(FetchContent)

function(arvane_alias_target alias_name target_name)
  if(NOT TARGET ${alias_name} AND TARGET ${target_name})
    add_library(${alias_name} ALIAS ${target_name})
  endif()
endfunction()

function(arvane_require_target target_name dependency_name)
  if(NOT TARGET ${target_name})
    message(FATAL_ERROR "${dependency_name} did not provide required target ${target_name}")
  endif()
endfunction()

function(arvane_require_fetch dependency_name)
  if(NOT ARVANE_FETCH_DEPENDENCIES)
    message(FATAL_ERROR
      "${dependency_name} was not found and ARVANE_FETCH_DEPENDENCIES is OFF. "
      "Install the dependency, enable ARVANE_USE_SYSTEM_DEPENDENCIES, or turn fetching back on.")
  endif()
endfunction()

find_package(OpenGL REQUIRED)

if(ARVANE_USE_SYSTEM_DEPENDENCIES)
  find_package(glfw3 QUIET)
endif()
if(NOT TARGET glfw)
  arvane_require_fetch(GLFW)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build GLFW docs" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build GLFW examples" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build GLFW tests" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "Generate GLFW install target" FORCE)
  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 567b1ec2442d59525e24c19e8d413df6baf02496
    UPDATE_DISCONNECTED TRUE
  )
  FetchContent_MakeAvailable(glfw)
endif()
arvane_require_target(glfw GLFW)

if(ARVANE_USE_SYSTEM_DEPENDENCIES)
  find_package(Freetype QUIET)
endif()
if(NOT TARGET Freetype::Freetype)
  arvane_require_fetch(FreeType)
  set(FT_DISABLE_ZLIB ON CACHE BOOL "Use FreeType's internal zlib path instead of a system zlib" FORCE)
  set(FT_DISABLE_BZIP2 ON CACHE BOOL "Disable FreeType bzip2 support" FORCE)
  set(FT_DISABLE_PNG ON CACHE BOOL "Disable FreeType PNG support" FORCE)
  set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "Disable FreeType's optional HarfBuzz dependency" FORCE)
  set(FT_DISABLE_BROTLI ON CACHE BOOL "Disable FreeType brotli support" FORCE)
  set(FT_DISABLE_HVF ON CACHE BOOL "Disable FreeType HVF support" FORCE)
  FetchContent_Declare(
    freetype
    GIT_REPOSITORY https://github.com/freetype/freetype.git
    GIT_TAG 25a08f24cfc0da879d1938352d026532f280b77e
    UPDATE_DISCONNECTED TRUE
  )
  FetchContent_MakeAvailable(freetype)
  arvane_alias_target(Freetype::Freetype freetype)
endif()
arvane_require_target(Freetype::Freetype FreeType)

if(ARVANE_USE_SYSTEM_DEPENDENCIES)
  find_package(harfbuzz QUIET)
endif()
if(NOT TARGET harfbuzz::harfbuzz)
  arvane_require_fetch(HarfBuzz)
  set(HB_HAVE_FREETYPE ON CACHE BOOL "Enable HarfBuzz FreeType integration" FORCE)
  set(HB_BUILD_UTILS OFF CACHE BOOL "Build HarfBuzz utilities" FORCE)
  set(HB_BUILD_SUBSET OFF CACHE BOOL "Build harfbuzz-subset" FORCE)
  set(HB_BUILD_RASTER OFF CACHE BOOL "Build harfbuzz-raster" FORCE)
  set(HB_BUILD_VECTOR OFF CACHE BOOL "Build harfbuzz-vector" FORCE)
  set(HB_BUILD_GPU OFF CACHE BOOL "Build harfbuzz-gpu" FORCE)
  set(HB_HAVE_CAIRO OFF CACHE BOOL "Enable HarfBuzz cairo helpers" FORCE)
  set(HB_HAVE_GLIB OFF CACHE BOOL "Enable HarfBuzz glib helpers" FORCE)
  set(HB_HAVE_GOBJECT OFF CACHE BOOL "Enable HarfBuzz GObject bindings" FORCE)
  set(HB_HAVE_ICU OFF CACHE BOOL "Enable HarfBuzz ICU helpers" FORCE)
  FetchContent_Declare(
    harfbuzz
    GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
    GIT_TAG 14.2.1
    UPDATE_DISCONNECTED TRUE
  )
  FetchContent_MakeAvailable(harfbuzz)
  arvane_alias_target(harfbuzz::harfbuzz harfbuzz)
endif()
arvane_require_target(harfbuzz::harfbuzz HarfBuzz)

if(ARVANE_USE_SYSTEM_DEPENDENCIES)
  find_package(GameNetworkingSockets CONFIG QUIET)
endif()
if(NOT TARGET GameNetworkingSockets::static)
  arvane_require_fetch(GameNetworkingSockets)
  set(BUILD_STATIC_LIB ON CACHE BOOL "Build GameNetworkingSockets static library" FORCE)
  set(BUILD_SHARED_LIB OFF CACHE BOOL "Build GameNetworkingSockets shared library" FORCE)
  set(BUILD_EXAMPLES OFF CACHE BOOL "Build GameNetworkingSockets examples" FORCE)
  set(BUILD_TESTS OFF CACHE BOOL "Build GameNetworkingSockets tests" FORCE)
  set(BUILD_TOOLS OFF CACHE BOOL "Build GameNetworkingSockets tools" FORCE)
  FetchContent_Declare(
    GameNetworkingSockets
    GIT_REPOSITORY https://github.com/valvesoftware/GameNetworkingSockets.git
    GIT_TAG v1.6.0
    UPDATE_DISCONNECTED TRUE
  )
  FetchContent_MakeAvailable(GameNetworkingSockets)
endif()
arvane_alias_target(GameNetworkingSockets::static GameNetworkingSockets_s)
arvane_alias_target(GameNetworkingSockets::static GameNetworkingSockets::GameNetworkingSockets_s)
arvane_require_target(GameNetworkingSockets::static GameNetworkingSockets)

add_subdirectory(sdk/arvane_plugin)

add_subdirectory(vendors/glad)
add_subdirectory(vendors/stb)
add_subdirectory(vendors/imgui)

set(BUILD_TESTING OFF CACHE BOOL "Build dependency tests" FORCE)
set(TF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RECTPACK2D_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "Build zstd static library" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "Build zstd shared library" FORCE)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "Build zstd command-line tools" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "Build zstd tests" FORCE)

FetchContent_Declare(
  Taskflow
  GIT_REPOSITORY https://github.com/taskflow/taskflow.git
  GIT_TAG master
  UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(Taskflow)

FetchContent_Declare(
  rectpack2D
  GIT_REPOSITORY https://github.com/TeamHypersomnia/rectpack2D.git
  GIT_TAG master
  UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(rectpack2D)

FetchContent_Declare(
  EnTT
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v3.16.0
  UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(EnTT)

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG yaml-cpp-0.9.0
  UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(yaml-cpp)
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG master
  UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
  FastNoise2
  GIT_REPOSITORY https://github.com/Auburn/FastNoise2.git
  GIT_TAG v1.1.1
  UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(FastNoise2)

FetchContent_Declare(
  concurrentqueue
  GIT_REPOSITORY https://github.com/cameron314/concurrentqueue.git
  GIT_TAG v1.0.5
  UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(concurrentqueue)

FetchContent_Declare(
  readerwriterqueue
  GIT_REPOSITORY https://github.com/cameron314/readerwriterqueue.git
  GIT_TAG v1.0.7
  UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(readerwriterqueue)

FetchContent_Declare(
  unordered_dense
  GIT_REPOSITORY https://github.com/martinus/unordered_dense.git
  GIT_TAG v4.8.1
  UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(unordered_dense)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.17.0
  UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
  zstd
  GIT_REPOSITORY https://github.com/facebook/zstd.git
  GIT_TAG v1.5.7
  SOURCE_SUBDIR build/cmake
  UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(zstd)

if(ARVANE_ENABLE_PROFILING AND ARVANE_ENABLE_TRACY)
  FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG v0.13.1
    UPDATE_DISCONNECTED TRUE
  )
  set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy profiling" FORCE)
  set(TRACY_DELAYED_INIT ON CACHE BOOL "Delay Tracy startup until first profiling call" FORCE)
  FetchContent_MakeAvailable(tracy)
endif()

if(ARVANE_BUILD_TESTS)
  include(CTest)
  set(BUILD_TESTING ON CACHE BOOL "Build tests" FORCE)

  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    UPDATE_DISCONNECTED TRUE
  )

  # For Windows: Prevent overriding the parent project's compiler/linker settings
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

if(ARVANE_BUILD_BENCHMARKS)
  FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.9.5
    UPDATE_DISCONNECTED TRUE
  )
  # Disable testing for the library itself to speed up build
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(benchmark)
endif()

add_subdirectory(engine)
add_subdirectory(plugins)
if(ARVANE_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
add_subdirectory(apps)
if(ARVANE_BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(ARVANE_BUILD_SDK_EXAMPLES)
  add_subdirectory(examples/plugins)
endif()
