#include "arv/ui/screens/main_menu.hpp"

#include "arv/ui/screens/multiplayer.hpp"
#include "arv/ui/screens/settings.hpp"
#include "arv/ui/screens/singleplayer.hpp"

#include "menu_common.hpp"

#include <memory>

namespace arv::ui {
namespace {

class MainMenuScreen final : public UIScreen {
public:
  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::MainMenu;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "main_menu";
  }

  void build(UIContext& ctx) override {
    using namespace screens_detail;
    const UiNodeID stack = add_menu_shell(ctx, screen_id(), "Arvane", 360.0F, 300.0F);
    add_button(ctx.tree, stack, "menu.main.single_player", "Single Player", "main.single_player");
    add_button(ctx.tree, stack, "menu.main.multi_player", "Multi Player", "main.multi_player");
    add_button(ctx.tree, stack, "menu.main.settings", "Settings", "main.settings");
    add_button(ctx.tree, stack, "menu.main.quit", "Quit", "main.quit");
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (ctx.screens == nullptr) {
      return false;
    }
    if (action.action_id == "main.single_player") {
      ctx.screens->push(make_single_player_menu_screen());
      return true;
    }
    if (action.action_id == "main.multi_player") {
      ctx.screens->push(make_multi_player_menu_screen());
      return true;
    }
    if (action.action_id == "main.settings") {
      ctx.screens->push(make_settings_menu_screen());
      return true;
    }
    if (action.action_id == "main.quit") {
      screens_detail::submit_command(ctx, QuitGameCommand{});
      return true;
    }
    return false;
  }

  [[nodiscard]] bool can_close() const noexcept override {
    return false;
  }

  [[nodiscard]] bool pauses_singleplayer() const noexcept override {
    return true;
  }
};

} // namespace

std::unique_ptr<UIScreen> make_main_menu_screen() {
  return std::make_unique<MainMenuScreen>();
}

} // namespace arv::ui
