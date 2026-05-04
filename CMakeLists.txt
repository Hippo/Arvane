cmake_minimum_required(VERSION 3.19)
project(arvane)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ARVANE_BUILD_TESTS "Build Arvane tests" ON)
option(ARVANE_BUILD_BENCHMARKS "Build Arvane benchmarks" ON)
option(ARVANE_ENABLE_PROFILING "Enable Arvane profiling instrumentation and timing counters" ON)
option(ARVANE_ENABLE_TRACY "Enable Tracy profiling zones when profiling is enabled" ON)

if(NOT ARVANE_BUILD_TESTS)
  set(BUILD_TESTING OFF CACHE BOOL "Build tests" FORCE)
endif()

find_package(OpenGL REQUIRED)
find_package(Freetype REQUIRED)
find_package(glfw3 REQUIRED)
find_package(harfbuzz REQUIRED)
find_package(GameNetworkingSockets CONFIG REQUIRED)

add_subdirectory(vendors/glad)
add_subdirectory(vendors/stb)
add_subdirectory(vendors/imgui)

include(FetchContent)

set(BUILD_TESTING OFF CACHE BOOL "Build dependency tests" FORCE)
set(TF_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RECTPACK2D_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(ENTT_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

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

  add_subdirectory(tests)
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
if(ARVANE_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
add_subdirectory(apps)
