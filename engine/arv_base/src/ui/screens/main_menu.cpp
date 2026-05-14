#include "arv/base/ui/screens/main_menu.hpp"

#include "arv/base/ui/screens/account.hpp"
#include "arv/base/ui/screens/multiplayer.hpp"
#include "arv/base/ui/screens/settings.hpp"
#include "arv/base/ui/screens/singleplayer.hpp"

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
    const UiNodeID stack = add_menu_shell(ctx, screen_id(), tr(ctx, "ui.menu.main.title"), 396.0F, 344.0F);
    add_button(
        ctx.tree,
        stack,
        "menu.main.single_player",
        tr(ctx, "ui.menu.main.single_player"),
        "main.single_player",
        true,
        false,
        UiControlRole::Primary);
    add_button(ctx.tree, stack, "menu.main.multi_player", tr(ctx, "ui.menu.main.multi_player"), "main.multi_player");
    add_button(ctx.tree, stack, "menu.main.settings", tr(ctx, "ui.menu.common.settings"), "main.settings");
    add_button(
        ctx.tree,
        stack,
        "menu.main.quit",
        tr(ctx, "ui.menu.common.quit"),
        "main.quit",
        true,
        false,
        UiControlRole::Danger);
    build_account_manager_dropdown(ctx, account_state_);
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (ctx.screens == nullptr) {
      return false;
    }
    if (handle_account_manager_action(action, ctx, account_state_)) {
      return true;
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

  bool handle_scroll(Vec2f cursor, f32 wheel_y, UIContext& ctx) override {
    return handle_account_manager_scroll(cursor, wheel_y, ctx, account_state_);
  }

  [[nodiscard]] bool can_close() const noexcept override {
    return false;
  }

  [[nodiscard]] bool pauses_singleplayer() const noexcept override {
    return true;
  }

private:
  AccountManagerDropdownState account_state_{};
};

} // namespace

std::unique_ptr<UIScreen> make_main_menu_screen() {
  return std::make_unique<MainMenuScreen>();
}

} // namespace arv::ui
