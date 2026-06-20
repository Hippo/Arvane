set(ARV_RENDER_HEADERS
  include/arv/render/block_model_geometry.hpp
  include/arv/render/chunk_mesher.hpp
  include/arv/render/entity_model.hpp
  include/arv/render/item_model.hpp
  include/arv/render/opengl_render_commands.hpp
  include/arv/render/opengl_renderer.hpp
  include/arv/render/paged_texture_atlas.hpp
  include/arv/render/paged_texture_region_allocator.hpp
  include/arv/render/render_commands.hpp
  include/arv/render/render_culling.hpp
  include/arv/render/render_types.hpp
  include/arv/render/webgl_renderer.hpp
  include/arv/render/webgl_render_commands.hpp
  include/arv/render/webgl_shader.hpp
)

add_library(arv_render_headers INTERFACE)
target_include_directories(arv_render_headers INTERFACE include)
target_link_libraries(arv_render_headers INTERFACE arv_core_headers arv_voxel_headers)

add_library(arv_render_mesh STATIC
  src/block_model_geometry.cpp
  src/chunk_mesher.cpp
  src/entity_model.cpp
  src/item_model.cpp
  src/paged_texture_region_allocator.cpp
  src/render_types.cpp
)
target_include_directories(arv_render_mesh PUBLIC include)
target_link_libraries(arv_render_mesh PUBLIC arv_render_headers arv_core arv_voxel)
target_link_libraries(arv_render_mesh PRIVATE arv_io_data)

add_library(arv_render_commands STATIC
  src/render_commands.cpp
)
target_include_directories(arv_render_commands PUBLIC include)
target_link_libraries(arv_render_commands PUBLIC arv_render_mesh arv_core arv_voxel)

if(EMSCRIPTEN AND ARVANE_BUILD_WEB)
  add_library(arv_render_webgl STATIC
    src/webgl_render_commands.cpp
    src/webgl_renderer.cpp
    src/webgl_shader.cpp
  )
  target_include_directories(arv_render_webgl PUBLIC include)
  target_link_libraries(arv_render_webgl PUBLIC arv_render_commands arv_render_mesh arv_core arv_voxel)
  return()
endif()

target_sources(arv_render_commands PRIVATE
  src/render_commands_sched.cpp
)
target_link_libraries(arv_render_commands PUBLIC arv_sched)

add_library(arv_render_opengl STATIC
  src/opengl_renderer.cpp
  src/opengl_render_commands.cpp
  src/paged_texture_atlas.cpp
)
target_include_directories(arv_render_opengl PUBLIC include)
target_link_libraries(arv_render_opengl PUBLIC arv_render_commands arv_render_mesh arv_core arv_voxel glad)
target_link_libraries(arv_render_opengl PRIVATE OpenGL::GL arv_io)

add_library(arv_render STATIC
  src/render_module.cpp
)
target_include_directories(arv_render PUBLIC include)
target_link_libraries(arv_render PUBLIC arv_render_commands arv_render_opengl)
