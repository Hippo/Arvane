set(ARV_RENDER_HEADERS
  include/arv/render/chunk_mesher.hpp
  include/arv/render/entity_model.hpp
  include/arv/render/item_model.hpp
  include/arv/render/opengl_renderer.hpp
  include/arv/render/paged_texture_atlas.hpp
  include/arv/render/render_commands.hpp
  include/arv/render/render_types.hpp
)

add_library(arv_render STATIC
  src/chunk_mesher.cpp
  src/entity_model.cpp
  src/item_model.cpp
  src/opengl_renderer.cpp
  src/paged_texture_atlas.cpp
  src/render_commands.cpp
  src/render_types.cpp
)
target_include_directories(arv_render PUBLIC include)
target_link_libraries(arv_render PUBLIC arv_core arv_sched arv_voxel glad)
target_link_libraries(arv_render PRIVATE OpenGL::GL nlohmann_json::nlohmann_json)
