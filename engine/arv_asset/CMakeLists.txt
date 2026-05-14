set(ARV_ASSET_HEADERS
  include/arv/asset/font.hpp
  include/arv/asset/pack.hpp
  include/arv/asset/resource.hpp
  include/arv/asset/texture_atlas.hpp
)

add_library(arv_asset STATIC
  src/font.cpp
  src/pack.cpp
  src/resource.cpp
  src/texture_atlas.cpp
)

target_include_directories(arv_asset PUBLIC include)
target_link_libraries(arv_asset
  PUBLIC
    arv_core
    arv_voxel
  PRIVATE
    arv_io
    Freetype::Freetype
    harfbuzz::harfbuzz
    rectpack2D
    stb
)
