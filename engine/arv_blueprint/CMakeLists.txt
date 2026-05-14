set(ARV_BLUEPRINT_HEADERS
  include/arv/blueprint/blueprint.hpp
)

add_library(arv_blueprint STATIC
  src/blueprint.cpp
)
set_target_properties(arv_blueprint PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(arv_blueprint PUBLIC include)
target_link_libraries(arv_blueprint PUBLIC arv_core)
