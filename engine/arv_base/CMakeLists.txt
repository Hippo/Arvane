set(ARV_BASE_HEADERS
  include/arv/base/base_commands.hpp
  include/arv/base/base_ids.hpp
  include/arv/base/base_module.hpp
  include/arv/base/command_services.hpp
)

add_library(arv_base STATIC
  src/base_commands.cpp
  src/base_module.cpp
  src/blocks/block_behavior_registration.cpp
  src/entities/entity_goal_registration.cpp
  src/entities/entity_registration.cpp
  src/fluids/fluid_registration.cpp
  src/gameplay/block_break_registration.cpp
  src/items/item_behavior_registration.cpp
  src/gameplay/starter_inventory.cpp
  src/gamemodes/gamemode_registration.cpp
  src/tp_command.cpp
)
target_include_directories(arv_base PUBLIC include)
target_link_libraries(arv_base PUBLIC arv_command arv_core arv_entity arv_player arv_render arv_server arv_voxel arv_world)
target_link_libraries(arv_base PRIVATE arv_lang)
