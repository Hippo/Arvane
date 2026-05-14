set(ARV_UI_HEADERS
  include/arv/ui/core/ui.hpp
  include/arv/ui/theme.hpp
  include/arv/ui/ui.hpp
)

add_library(arv_ui STATIC
  src/core/ui.cpp
  src/theme.cpp
)
target_include_directories(arv_ui PUBLIC include)
target_link_libraries(arv_ui PUBLIC arv_asset arv_core arv_render)
target_link_libraries(arv_ui PRIVATE yaml-cpp)
