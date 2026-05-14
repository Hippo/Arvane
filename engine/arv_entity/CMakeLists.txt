set(ARV_ENTITY_HEADERS
  include/arv/entity/components.hpp
)

add_library(arv_entity STATIC
  src/components.cpp
)
target_include_directories(arv_entity PUBLIC include)
target_link_libraries(arv_entity PUBLIC arv_core arv_player)
target_link_libraries(arv_entity PRIVATE arv_io)
