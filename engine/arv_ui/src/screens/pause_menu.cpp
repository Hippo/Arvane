#include "arv/ui/screens/pause_menu.hpp"

#include "arv/ui/screens/main_menu.hpp"
#include "arv/ui/screens/settings.hpp"

#include "menu_common.hpp"

#include <memory>

namespace arv::ui {
namespace {

using namespace screens_detail;

class PauseMenuScreen final : public UIScreen {
public:
  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::PauseMenu;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "pause_menu";
  }

  void build(UIContext& ctx) override {
    const UiNodeID stack =
        add_menu_shell(ctx, screen_id(), "Paused", 360.0F, 300.0F, PauseBackdropColor);
    add_button(ctx.tree, stack, "menu.pause.resume", "Resume", "pause.resume");
    add_button(ctx.tree, stack, "menu.pause.main_menu", "Back to Main Menu", "pause.main_menu");
    add_button(ctx.tree, stack, "menu.pause.settings", "Settings", "pause.settings");
    add_button(ctx.tree, stack, "menu.pause.quit", "Quit", "pause.quit");
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (action.action_id == "pause.resume") {
      submit_command(ctx, ResumeGameCommand{});
      if (ctx.screens != nullptr) {
        ctx.screens->pop();
      }
      return true;
    }
    if (action.action_id == "pause.main_menu") {
      submit_command(ctx, BackToMainMenuCommand{});
      if (ctx.screens != nullptr) {
        ctx.screens->replace(make_main_menu_screen());
      }
      return true;
    }
    if (action.action_id == "pause.settings" && ctx.screens != nullptr) {
      ctx.screens->push(make_settings_menu_screen());
      return true;
    }
    if (action.action_id == "pause.quit") {
      submit_command(ctx, QuitGameCommand{});
      return true;
    }
    return false;
  }

  bool handle_cancel(UIContext& ctx) override {
    submit_command(ctx, ResumeGameCommand{});
    if (ctx.screens != nullptr) {
      ctx.screens->pop();
    }
    return true;
  }

  [[nodiscard]] bool pauses_singleplayer() const noexcept override {
    return true;
  }
};

} // namespace

std::unique_ptr<UIScreen> make_pause_menu_screen() {
  return std::make_unique<PauseMenuScreen>();
}

} // namespace arv::ui
