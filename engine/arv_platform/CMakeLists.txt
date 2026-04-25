set(ARV_PLATFORM_HEADERS
  include/arv/platform/glfw_window.hpp
)

add_library(arv_platform INTERFACE)
target_include_directories(arv_platform INTERFACE include)
target_link_libraries(arv_platform INTERFACE arv_core glad glfw OpenGL::GL)
