set(ARV_BASE_HEADERS
  include/arv/base/base_commands.hpp
  include/arv/base/command_services.hpp
)

add_library(arv_base STATIC
  src/base_commands.cpp
  src/tp_command.cpp
)
target_include_directories(arv_base PUBLIC include)
target_link_libraries(arv_base PUBLIC arv_command arv_core)
target_link_libraries(arv_base PRIVATE arv_lang)
