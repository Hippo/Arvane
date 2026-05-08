#include "arv/ui/screens/singleplayer.hpp"

#include "menu_common.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace arv::ui {
namespace {

using namespace screens_detail;

class SinglePlayerMenuScreen final : public UIScreen {
public:
  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::SinglePlayerMenu;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "single_player_menu";
  }

  void build(UIContext& ctx) override {
    const std::vector<SinglePlayerSaveEntry> saves = saves_from(ctx);
    if (selected_save_.has_value() && selected_save_.value() >= saves.size()) {
      selected_save_.reset();
    }

    const UiTheme& theme = ctx.tree.theme();
    const UiNodeID stack = add_menu_shell(ctx, screen_id(), "Single Player", 448.0F, 524.0F);
    if (saves.empty()) {
      UiElement empty = make_text(
          "menu.single_player.empty",
          "No local saves",
          theme.colors.text_muted,
          UiTextRole::Small);
      empty.layout.anchor = UiAnchor::TopCenter;
      ctx.tree.add_child(stack, std::move(empty));
    } else {
      for (usize index = 0; index < saves.size(); ++index) {
        const SinglePlayerSaveEntry& save = saves[index];
        add_list_row(
            ctx.tree,
            stack,
            "menu.single_player.world." + std::to_string(index),
            save_display_name(save),
            save.detail.empty() ? "Ready to play" : save.detail,
            "single_player.select_world",
            true,
            selected_save_.has_value() && selected_save_.value() == index);
      }
    }

    add_button(
        ctx.tree,
        stack,
        "menu.single_player.create",
        "Create World",
        "single_player.create",
        true,
        false,
        UiControlRole::Primary);
    add_button(
        ctx.tree,
        stack,
        "menu.single_player.delete",
        "Delete Save",
        "single_player.delete",
        selected_save_.has_value(),
        false,
        UiControlRole::Danger);
    add_button(ctx.tree, stack, "menu.single_player.back", "Back", "menu.back");
    add_error_text(ctx.tree, stack, "menu.single_player.error", error_);

    if (confirm_delete_ && selected_save_.has_value() && selected_save_.value() < saves.size()) {
      add_backdrop(ctx.tree, "menu.single_player.confirm.backdrop", {0.0F, 0.0F, 0.0F, 0.52F});
      UiElement panel = make_panel("menu.single_player.confirm.panel", 380.0F, 194.0F);
      panel.layout.anchor = UiAnchor::Center;
      panel.layout.padding = {
          theme.widgets.panel.padding,
          theme.widgets.panel.padding,
          theme.widgets.panel.padding,
          theme.widgets.panel.padding};
      apply_panel_style(panel, theme, true);
      const UiNodeID panel_id = ctx.tree.add_child(ctx.tree.root_id(), std::move(panel));
      UiElement confirm_stack = make_stack("menu.single_player.confirm.stack", theme.layout.spacing_md);
      const UiNodeID confirm_stack_id = ctx.tree.add_child(panel_id, std::move(confirm_stack));
      ctx.tree.add_child(
          confirm_stack_id,
          make_text(
              "menu.single_player.confirm.text",
              "Delete " + save_display_name(saves[selected_save_.value()]) + "?",
              theme.colors.danger,
              UiTextRole::Heading));
      add_button(
          ctx.tree,
          confirm_stack_id,
          "menu.single_player.confirm.delete",
          "Delete",
          "single_player.confirm_delete",
          true,
          false,
          UiControlRole::Danger);
      add_button(
          ctx.tree,
          confirm_stack_id,
          "menu.single_player.confirm.cancel",
          "Cancel",
          "single_player.cancel_delete");
    }
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    const std::vector<SinglePlayerSaveEntry> saves = saves_from(ctx);
    if (action.action_id == "single_player.cancel_delete") {
      confirm_delete_ = false;
      return true;
    }
    if (action.action_id == "single_player.confirm_delete") {
      if (selected_save_.has_value() && selected_save_.value() < saves.size()) {
        const SinglePlayerSaveEntry& save = saves[selected_save_.value()];
        submit_command(
            ctx,
            DeleteSinglePlayerSaveCommand{
                server_save_name_for_command(save),
                primary_world_name_for_command(save)});
      }
      confirm_delete_ = false;
      selected_save_.reset();
      return true;
    }
    if (confirm_delete_) {
      return true;
    }

    usize index = 0;
    if (action.action_id == "single_player.select_world" &&
        parse_index_suffix(action.element_id, "menu.single_player.world.", index) &&
        index < saves.size()) {
      if (selected_save_.has_value() && selected_save_.value() == index) {
        submit_command(
            ctx,
            StartSinglePlayerCommand{
                server_save_name_for_command(saves[index]),
                primary_world_name_for_command(saves[index])});
        if (ctx.screens != nullptr) {
          ctx.screens->clear();
        }
      } else {
        selected_save_ = index;
      }
      error_.clear();
      return true;
    }
    if (action.action_id == "single_player.create" && ctx.screens != nullptr) {
      ctx.screens->push(make_create_world_screen());
      return true;
    }
    if (action.action_id == "single_player.delete") {
      if (!selected_save_.has_value()) {
        error_ = "Select a save first";
      } else {
        confirm_delete_ = true;
        error_.clear();
      }
      return true;
    }
    if (action.action_id == "menu.back") {
      return handle_cancel(ctx);
    }
    return false;
  }

  bool handle_submit(UIContext& ctx) override {
    const std::vector<SinglePlayerSaveEntry> saves = saves_from(ctx);
    if (!selected_save_.has_value() || selected_save_.value() >= saves.size()) {
      return false;
    }
    const SinglePlayerSaveEntry& save = saves[selected_save_.value()];
    submit_command(
        ctx,
        StartSinglePlayerCommand{
            server_save_name_for_command(save),
            primary_world_name_for_command(save)});
    if (ctx.screens != nullptr) {
      ctx.screens->clear();
    }
    return true;
  }

  [[nodiscard]] bool pauses_singleplayer() const noexcept override {
    return true;
  }

private:
  std::optional<usize> selected_save_{};
  bool confirm_delete_{};
  std::string error_{};
};

class CreateWorldScreen final : public UIScreen {
public:
  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::CreateWorld;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "create_world";
  }

  void build(UIContext& ctx) override {
    const UiNodeID stack = add_menu_shell(ctx, screen_id(), "Create World", 448.0F, 372.0F);
    add_text_box(
        ctx.tree,
        stack,
        "menu.create_world.name",
        "Name",
        name_,
        "World name",
        "create_world.focus_name",
        active_field_ == TextField::Name);
    add_text_box(
        ctx.tree,
        stack,
        "menu.create_world.seed",
        "Seed",
        seed_,
        "Optional numeric seed",
        "create_world.focus_seed",
        active_field_ == TextField::Seed);
    add_error_text(ctx.tree, stack, "menu.create_world.error", error_);
    add_button(
        ctx.tree,
        stack,
        "menu.create_world.create",
        "Create",
        "create_world.create",
        true,
        false,
        UiControlRole::Primary);
    add_button(ctx.tree, stack, "menu.create_world.cancel", "Cancel", "menu.back");
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (action.action_id == "create_world.focus_name") {
      active_field_ = TextField::Name;
      return true;
    }
    if (action.action_id == "create_world.focus_seed") {
      active_field_ = TextField::Seed;
      return true;
    }
    if (action.action_id == "create_world.create") {
      return create(ctx);
    }
    if (action.action_id == "menu.back") {
      return handle_cancel(ctx);
    }
    return false;
  }

  bool handle_text_input(std::string_view text, UIContext&) override {
    if (active_field_ == TextField::Name) {
      name_ += text;
      return true;
    }
    if (active_field_ == TextField::Seed) {
      seed_ += text;
      return true;
    }
    return false;
  }

  bool handle_backspace(UIContext&) override {
    if (active_field_ == TextField::Name) {
      erase_last_utf8_codepoint(name_);
      return true;
    }
    if (active_field_ == TextField::Seed) {
      erase_last_utf8_codepoint(seed_);
      return true;
    }
    return false;
  }

  bool handle_submit(UIContext& ctx) override {
    return create(ctx);
  }

  bool handle_cancel(UIContext& ctx) override {
    if (active_field_ != TextField::None) {
      active_field_ = TextField::None;
      return true;
    }
    return UIScreen::handle_cancel(ctx);
  }

  [[nodiscard]] bool pauses_singleplayer() const noexcept override {
    return true;
  }

private:
  bool create(UIContext& ctx) {
    const std::string name = trim_copy(name_);
    const std::string seed = trim_copy(seed_);
    const std::string sanitized = sanitize_world_folder_name(name);
    if (name.empty()) {
      error_ = "World name is required";
      return true;
    }
    if (sanitized.empty()) {
      error_ = "World name needs letters or numbers";
      return true;
    }
    if (!valid_seed_text(seed)) {
      error_ = "Seed must be an unsigned number";
      return true;
    }
    for (const SinglePlayerSaveEntry& save : saves_from(ctx)) {
      if (sanitize_world_folder_name(save_display_name(save)) == sanitized ||
          sanitize_world_folder_name(save.server_save_name) == sanitized) {
        error_ = "A save with that name already exists";
        return true;
      }
    }
    submit_command(ctx, CreateWorldCommand{name, seed});
    if (ctx.screens != nullptr) {
      ctx.screens->pop();
    }
    return true;
  }

  TextField active_field_{TextField::Name};
  std::string name_{};
  std::string seed_{};
  std::string error_{};
};

} // namespace

std::unique_ptr<UIScreen> make_single_player_menu_screen() {
  return std::make_unique<SinglePlayerMenuScreen>();
}

std::unique_ptr<UIScreen> make_create_world_screen() {
  return std::make_unique<CreateWorldScreen>();
}

} // namespace arv::ui
