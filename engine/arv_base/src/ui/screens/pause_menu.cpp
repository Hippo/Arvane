#include "arv/base/ui/screens/pause_menu.hpp"

#include "arv/base/ui/screens/settings.hpp"

#include "menu_common.hpp"

#include <memory>

namespace arv::ui {
namespace {

using namespace screens_detail;

class GameplayScreen final : public UIScreen {
public:
  explicit GameplayScreen(PauseMenuOptions options) : options_(options) {
  }

  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::Gameplay;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "gameplay";
  }

  void build(UIContext& ctx) override {
    (void)ctx;
  }

  bool handle_cancel(UIContext& ctx) override {
    if (ctx.screens == nullptr) {
      return false;
    }
    ctx.screens->push(make_pause_menu_screen(options_));
    return true;
  }

  [[nodiscard]] bool can_close() const noexcept override {
    return false;
  }

  [[nodiscard]] bool blocks_gameplay_input() const noexcept override {
    return false;
  }

private:
  PauseMenuOptions options_{};
};

class PauseMenuScreen final : public UIScreen {
public:
  explicit PauseMenuScreen(PauseMenuOptions options) : options_(options) {
  }

  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::PauseMenu;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "pause_menu";
  }

  void build(UIContext& ctx) override {
    const UiTheme& theme = ctx.tree.theme();
    add_backdrop(ctx.tree, "pause_menu.backdrop", theme.colors.pause_backdrop);

    UiElement stack_element = make_stack("pause_menu.stack", theme.layout.spacing_md);
    stack_element.layout.width_mode = UiSizeMode::Fixed;
    stack_element.layout.width = menu_button_width(theme);
    stack_element.layout.anchor = UiAnchor::Center;
    const UiNodeID stack = ctx.tree.add_child(ctx.tree.root_id(), std::move(stack_element));

    UiElement title = make_text("pause_menu.title", tr(ctx, "ui.menu.pause.title"), theme.colors.text, UiTextRole::Heading);
    title.layout.anchor = UiAnchor::TopCenter;
    title.layout.offset = {0.0F, -theme.layout.spacing_sm};
    ctx.tree.add_child(stack, std::move(title));

    add_button(
        ctx.tree,
        stack,
        "menu.pause.resume",
        tr(ctx, "ui.menu.pause.resume"),
        "pause.resume",
        true,
        false,
        UiControlRole::Primary);
    add_button(ctx.tree, stack, "menu.pause.settings", tr(ctx, "ui.menu.common.settings"), "pause.settings");
    add_button(ctx.tree, stack, "menu.pause.main_menu", tr(ctx, "ui.menu.pause.back_to_main_menu"), "pause.main_menu");
    if (options_.show_quit) {
      add_button(
          ctx.tree,
          stack,
          "menu.pause.quit",
          tr(ctx, "ui.menu.common.quit"),
          "pause.quit",
          true,
          false,
          UiControlRole::Danger);
    }
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

private:
  PauseMenuOptions options_{};
};

} // namespace

std::unique_ptr<UIScreen> make_gameplay_screen(PauseMenuOptions options) {
  return std::make_unique<GameplayScreen>(options);
}

std::unique_ptr<UIScreen> make_pause_menu_screen(PauseMenuOptions options) {
  return std::make_unique<PauseMenuScreen>(options);
}

} // namespace arv::ui
