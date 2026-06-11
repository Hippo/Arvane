set(ARV_ASSET_HEADERS
  include/arv/asset/font.hpp
  include/arv/asset/pack_bundle.hpp
  include/arv/asset/pack.hpp
  include/arv/asset/resource.hpp
  include/arv/asset/texture_atlas.hpp
)

add_library(arv_asset STATIC
  src/font.cpp
  src/pack_bundle.cpp
  src/pack.cpp
  src/resource.cpp
  src/texture_atlas.cpp
)

target_include_directories(arv_asset PUBLIC include)

if(TARGET libzstd_static)
  set(ARV_ASSET_ZSTD_TARGET libzstd_static)
elseif(TARGET zstd::libzstd_static)
  set(ARV_ASSET_ZSTD_TARGET zstd::libzstd_static)
elseif(TARGET zstd)
  set(ARV_ASSET_ZSTD_TARGET zstd)
else()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(ZSTD QUIET IMPORTED_TARGET libzstd)
  endif()
  if(TARGET PkgConfig::ZSTD)
    set(ARV_ASSET_ZSTD_TARGET PkgConfig::ZSTD)
  else()
    set(ARV_ASSET_ZSTD_TARGET zstd)
  endif()
endif()

target_link_libraries(arv_asset
  PUBLIC
    arv_core
    arv_voxel
  PRIVATE
    arv_crypto
    arv_io
    Freetype::Freetype
    harfbuzz::harfbuzz
    rectpack2D
    stb
    ${ARV_ASSET_ZSTD_TARGET}
)
