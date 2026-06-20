set(ARV_PLATFORM_HEADERS
  include/arv/platform/glfw_window.hpp
  include/arv/platform/input_bindings.hpp
  include/arv/platform/input_snapshot.hpp
)

if(NOT TARGET arv_platform_input_headers)
  add_library(arv_platform_input_headers INTERFACE)
  target_include_directories(arv_platform_input_headers INTERFACE include)
  target_link_libraries(arv_platform_input_headers INTERFACE arv_core_headers)
endif()

if(NOT TARGET arv_platform_input)
  add_library(arv_platform_input STATIC
    src/input_bindings.cpp
  )
  target_link_libraries(arv_platform_input PUBLIC arv_platform_input_headers)
endif()

add_library(arv_platform STATIC
  src/glfw_window.cpp
)
target_include_directories(arv_platform PUBLIC include)
target_link_libraries(arv_platform PUBLIC arv_core arv_platform_input)
target_link_libraries(arv_platform PRIVATE glad glfw OpenGL::GL)
