set(ARV_PLATFORM_HEADERS
  include/arv/platform/glfw_window.hpp
)

add_library(arv_platform STATIC
  src/glfw_window.cpp
)
target_include_directories(arv_platform PUBLIC include)
target_link_libraries(arv_platform PUBLIC arv_core)
target_link_libraries(arv_platform PRIVATE glad glfw OpenGL::GL)
