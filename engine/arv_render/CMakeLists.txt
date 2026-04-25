set(ARV_RENDER_HEADERS
  include/arv/render/chunk_mesher.hpp
  include/arv/render/opengl_renderer.hpp
  include/arv/render/render_types.hpp
)

add_library(arv_render INTERFACE)
target_include_directories(arv_render INTERFACE include)
target_link_libraries(arv_render INTERFACE arv_core arv_voxel glad OpenGL::GL)
