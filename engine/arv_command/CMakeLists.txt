set(ARV_COMMAND_HEADERS
  include/arv/command/command.hpp
  include/arv/command/command_dispatcher.hpp
  include/arv/command/command_registry.hpp
  include/arv/command/command_system.hpp
)

add_library(arv_command STATIC
  src/command_args.cpp
  src/command_dispatcher.cpp
  src/command_registry.cpp
)
target_include_directories(arv_command PUBLIC include)
target_link_libraries(arv_command PUBLIC arv_core arv_crypto)
