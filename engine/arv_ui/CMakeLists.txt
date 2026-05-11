set(ARV_UI_HEADERS
  include/arv/ui/core/ui.hpp
  include/arv/ui/screens/account.hpp
  include/arv/ui/screens/main_menu.hpp
  include/arv/ui/screens/menu_screens.hpp
  include/arv/ui/screens/multiplayer.hpp
  include/arv/ui/screens/pause_menu.hpp
  include/arv/ui/screens/screen.hpp
  include/arv/ui/screens/settings.hpp
  include/arv/ui/screens/singleplayer.hpp
  include/arv/ui/theme.hpp
  include/arv/ui/ui.hpp
  include/arv/ui/widgets/chat.hpp
  include/arv/ui/widgets/debug_hud.hpp
  include/arv/ui/widgets/gameplay.hpp
  include/arv/ui/widgets/health.hpp
  include/arv/ui/widgets/hotbar.hpp
  include/arv/ui/widgets/inventory.hpp
  include/arv/ui/widgets/items.hpp
  include/arv/ui/widgets/scoreboard.hpp
  include/arv/ui/widgets/tab_menu.hpp
)

add_library(arv_ui STATIC
  src/core/ui.cpp
  src/screens/account.cpp
  src/screens/main_menu.cpp
  src/screens/multiplayer.cpp
  src/screens/pause_menu.cpp
  src/screens/screen_manager.cpp
  src/screens/settings.cpp
  src/screens/singleplayer.cpp
  src/theme.cpp
  src/widgets/chat.cpp
  src/widgets/debug_hud.cpp
  src/widgets/gameplay.cpp
  src/widgets/health.cpp
  src/widgets/hotbar.cpp
  src/widgets/inventory.cpp
  src/widgets/scoreboard.cpp
  src/widgets/tab_menu.cpp
)
target_include_directories(arv_ui PUBLIC include)
target_link_libraries(arv_ui PUBLIC arv_asset arv_core arv_lang arv_player arv_protocol arv_render arv_voxel)
target_link_libraries(arv_ui PRIVATE yaml-cpp)
