set(ARV_BASE_HEADERS
  include/arv/base/base_commands.hpp
  include/arv/base/base_ids.hpp
  include/arv/base/base_module.hpp
  include/arv/base/command_services.hpp
)

set(ARV_BASE_UI_HEADERS
  include/arv/base/ui.hpp
  include/arv/base/ui/screens/account.hpp
  include/arv/base/ui/screens/main_menu.hpp
  include/arv/base/ui/screens/menu_screens.hpp
  include/arv/base/ui/screens/multiplayer.hpp
  include/arv/base/ui/screens/screen.hpp
  include/arv/base/ui/screens/settings.hpp
  include/arv/base/ui/screens/singleplayer.hpp
  include/arv/base/ui/widgets/chat.hpp
  include/arv/base/ui/widgets/debug_hud.hpp
  include/arv/base/ui/widgets/gameplay.hpp
  include/arv/base/ui/widgets/health.hpp
  include/arv/base/ui/widgets/hotbar.hpp
  include/arv/base/ui/widgets/inventory.hpp
  include/arv/base/ui/widgets/items.hpp
  include/arv/base/ui/widgets/scoreboard.hpp
  include/arv/base/ui/widgets/tab_menu.hpp
  include/arv/base/ui/widgets/text_input.hpp
)

if(EMSCRIPTEN AND ARVANE_BUILD_WEB)
  add_library(arv_base_ui STATIC
    include/arv/base/ui/screens/screen.hpp
    include/arv/base/ui/screens/settings.hpp
    include/arv/base/ui/widgets/chat.hpp
    include/arv/base/ui/widgets/debug_hud.hpp
    include/arv/base/ui/widgets/gameplay.hpp
    include/arv/base/ui/widgets/health.hpp
    include/arv/base/ui/widgets/hotbar.hpp
    include/arv/base/ui/widgets/inventory.hpp
    include/arv/base/ui/widgets/items.hpp
    include/arv/base/ui/widgets/scoreboard.hpp
    include/arv/base/ui/widgets/tab_menu.hpp
    include/arv/base/ui/widgets/text_input.hpp
    src/ui/screens/screen_manager.cpp
    src/ui/screens/settings.cpp
    src/ui/widgets/chat.cpp
    src/ui/widgets/debug_hud.cpp
    src/ui/widgets/gameplay.cpp
    src/ui/widgets/health.cpp
    src/ui/widgets/hotbar.cpp
    src/ui/widgets/inventory.cpp
    src/ui/widgets/scoreboard.cpp
    src/ui/widgets/tab_menu.cpp
  )
  target_include_directories(arv_base_ui PUBLIC include)
  target_link_libraries(arv_base_ui PUBLIC arv_core arv_player arv_protocol_headers arv_render_headers arv_text arv_ui arv_voxel)
  return()
endif()

add_library(arv_base STATIC
  src/base_commands.cpp
  src/blueprint_command.cpp
  src/base_module.cpp
  src/blocks/block_behavior_registration.cpp
  src/core_commands.cpp
  src/entities/entity_goal_registration.cpp
  src/entities/entity_registration.cpp
  src/explosion_command.cpp
  src/flyspeed_command.cpp
  src/fluids/fluid_registration.cpp
  src/gamemode_command.cpp
  src/gameplay/block_break_registration.cpp
  src/items/item_behavior_registration.cpp
  src/gameplay/starter_inventory.cpp
  src/msg_command.cpp
  src/selection_command.cpp
  src/spawn_command.cpp
  src/gamemodes/gamemode_registration.cpp
  src/tp_command.cpp
)
target_include_directories(arv_base PUBLIC include)
target_link_libraries(arv_base PUBLIC arv_blueprint arv_command arv_core arv_entity arv_player arv_render arv_server arv_voxel arv_world)
target_link_libraries(arv_base PRIVATE arv_text)

add_library(arv_base_ui STATIC
  ${ARV_BASE_UI_HEADERS}
  src/ui/screens/account.cpp
  src/ui/screens/main_menu.cpp
  src/ui/screens/multiplayer.cpp
  src/ui/screens/screen_manager.cpp
  src/ui/screens/settings.cpp
  src/ui/screens/singleplayer.cpp
  src/ui/widgets/chat.cpp
  src/ui/widgets/debug_hud.cpp
  src/ui/widgets/gameplay.cpp
  src/ui/widgets/health.cpp
  src/ui/widgets/hotbar.cpp
  src/ui/widgets/inventory.cpp
  src/ui/widgets/scoreboard.cpp
  src/ui/widgets/tab_menu.cpp
)
target_include_directories(arv_base_ui PUBLIC include)
target_link_libraries(arv_base_ui PUBLIC arv_core arv_player arv_protocol arv_render arv_text arv_ui arv_voxel)
