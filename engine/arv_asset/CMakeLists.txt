set(ARV_ASSET_HEADERS
  include/arv/asset/font.hpp
  include/arv/asset/pack_bundle_cache.hpp
  include/arv/asset/pack_bundle.hpp
  include/arv/asset/pack.hpp
  include/arv/asset/resource.hpp
  include/arv/asset/texture_atlas.hpp
)

if(TARGET libzstd_static)
  set(ARV_ASSET_ZSTD_TARGET libzstd_static)
elseif(TARGET zstd::libzstd_static)
  set(ARV_ASSET_ZSTD_TARGET zstd::libzstd_static)
elseif(TARGET zstd::libzstd)
  set(ARV_ASSET_ZSTD_TARGET zstd::libzstd)
elseif(TARGET zstd::libzstd_shared)
  set(ARV_ASSET_ZSTD_TARGET zstd::libzstd_shared)
elseif(TARGET zstd)
  set(ARV_ASSET_ZSTD_TARGET zstd)
else()
  message(FATAL_ERROR "zstd target is missing; the root CMakeLists.txt must provide zstd via FetchContent")
endif()

add_library(arv_asset_bundle STATIC
  src/pack_bundle_cache.cpp
)

target_include_directories(arv_asset_bundle PUBLIC include)

target_link_libraries(arv_asset_bundle
  PUBLIC
    arv_crypto
  PRIVATE
    ${ARV_ASSET_ZSTD_TARGET}
)

add_library(arv_asset_core STATIC
  src/pack.cpp
  src/resource.cpp
  src/texture_atlas.cpp
)

target_include_directories(arv_asset_core PUBLIC include)

target_link_libraries(arv_asset_core
  PUBLIC
    arv_core
    arv_voxel
    arv_asset_bundle
  PRIVATE
    arv_crypto
    arv_io_data
    rectpack2D
    stb
    ${ARV_ASSET_ZSTD_TARGET}
)

if(EMSCRIPTEN AND ARVANE_BUILD_WEB)
  add_library(arv_asset STATIC
    src/font_stub.cpp
  )
else()
  add_library(arv_asset STATIC
    src/font.cpp
    src/pack_bundle.cpp
  )
endif()

target_include_directories(arv_asset PUBLIC include)

target_link_libraries(arv_asset PUBLIC arv_asset_core)

if(NOT (EMSCRIPTEN AND ARVANE_BUILD_WEB))
  target_link_libraries(arv_asset
    PRIVATE
      arv_crypto
      arv_io_data
      Freetype::Freetype
      harfbuzz::harfbuzz
  )
endif()
