set(ARV_RENDER_HEADERS
  include/arv/render/block_model_geometry.hpp
  include/arv/render/chunk_mesher.hpp
  include/arv/render/entity_model.hpp
  include/arv/render/item_model.hpp
  include/arv/render/opengl_renderer.hpp
  include/arv/render/paged_texture_atlas.hpp
  include/arv/render/render_commands.hpp
  include/arv/render/render_culling.hpp
  include/arv/render/render_types.hpp
)

add_library(arv_render_headers INTERFACE)
target_include_directories(arv_render_headers INTERFACE include)
target_link_libraries(arv_render_headers INTERFACE arv_core_headers arv_voxel_headers)

add_library(arv_render_mesh STATIC
  src/block_model_geometry.cpp
  src/chunk_mesher.cpp
  src/render_types.cpp
)
target_include_directories(arv_render_mesh PUBLIC include)
target_link_libraries(arv_render_mesh PUBLIC arv_render_headers arv_core arv_voxel)

if(EMSCRIPTEN AND ARVANE_BUILD_WEB)
  return()
endif()

add_library(arv_render STATIC
  src/entity_model.cpp
  src/item_model.cpp
  src/opengl_renderer.cpp
  src/paged_texture_atlas.cpp
  src/render_commands.cpp
)
target_include_directories(arv_render PUBLIC include)
target_link_libraries(arv_render PUBLIC arv_render_mesh arv_core arv_sched arv_voxel glad)
target_link_libraries(arv_render PRIVATE OpenGL::GL arv_io)
