#include "arv/ui/screens/settings.hpp"

#include "menu_common.hpp"

#include <memory>
#include <string>
#include <utility>

namespace arv::ui {
namespace {

using namespace screens_detail;

class SettingsMenuScreen final : public UIScreen {
public:
  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::SettingsMenu;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "settings_menu";
  }

  void build(UIContext& ctx) override {
    const UiNodeID stack = add_menu_shell(ctx, screen_id(), "Settings", 396.0F, 344.0F);
    add_button(
        ctx.tree,
        stack,
        "menu.settings.render",
        "Render Settings",
        "settings.render",
        true,
        false,
        UiControlRole::Primary);
    add_button(ctx.tree, stack, "menu.settings.key_binds", "Key Binds", "settings.key_binds");
    add_button(ctx.tree, stack, "menu.settings.other", "Other Settings", "settings.other");
    add_button(ctx.tree, stack, "menu.settings.back", "Back", "menu.back");
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (ctx.screens == nullptr) {
      return false;
    }
    if (action.action_id == "settings.render") {
      ctx.screens->push(make_render_settings_screen());
      return true;
    }
    if (action.action_id == "settings.key_binds") {
      ctx.screens->push(make_key_binds_screen());
      return true;
    }
    if (action.action_id == "settings.other") {
      ctx.screens->push(make_other_settings_screen());
      return true;
    }
    if (action.action_id == "menu.back") {
      return handle_cancel(ctx);
    }
    return false;
  }
};

class SettingsDetailScreen final : public UIScreen {
public:
  SettingsDetailScreen(MenuScreenType type, std::string id, std::string title) : type_(type),
                                                                                 id_(std::move(id)),
                                                                                 title_(std::move(title)) {
  }

  [[nodiscard]] MenuScreenType type() const noexcept override {
    return type_;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return id_;
  }

  void build(UIContext& ctx) override {
    const UiNodeID stack = add_menu_shell(ctx, screen_id(), title_, 420.0F, 244.0F);
    ctx.tree.add_child(
        stack,
        make_text(
            id_ + ".placeholder",
            "Settings controls will appear here",
            ctx.tree.theme().colors.text_muted,
            UiTextRole::Small));
    add_button(ctx.tree, stack, id_ + ".back", "Back", "menu.back");
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (action.action_id == "menu.back") {
      return handle_cancel(ctx);
    }
    return false;
  }

private:
  MenuScreenType type_{};
  std::string id_{};
  std::string title_{};
};

} // namespace

std::unique_ptr<UIScreen> make_settings_menu_screen() {
  return std::make_unique<SettingsMenuScreen>();
}

std::unique_ptr<UIScreen> make_render_settings_screen() {
  return std::make_unique<SettingsDetailScreen>(
      MenuScreenType::RenderSettings,
      "render_settings",
      "Render Settings");
}

std::unique_ptr<UIScreen> make_key_binds_screen() {
  return std::make_unique<SettingsDetailScreen>(
      MenuScreenType::KeyBinds,
      "key_binds",
      "Key Binds");
}

std::unique_ptr<UIScreen> make_other_settings_screen() {
  return std::make_unique<SettingsDetailScreen>(
      MenuScreenType::OtherSettings,
      "other_settings",
      "Other Settings");
}

} // namespace arv::ui
