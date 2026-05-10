#include "arv/ui/screens/multiplayer.hpp"

#include "menu_common.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace arv::ui {
namespace {

using namespace screens_detail;

[[nodiscard]] std::string wrapped_reason_text(std::string_view reason) {
  if (reason.empty()) {
    return "Disconnected.";
  }
  constexpr usize MaxLine = 44U;
  constexpr usize MaxDisplay = 360U;
  std::string wrapped;
  wrapped.reserve(std::min(reason.size(), MaxDisplay) + 16U);
  usize line_width = 0U;
  for (usize index = 0; index < reason.size() && index < MaxDisplay; ++index) {
    const char ch = reason[index];
    if (ch == '\r') {
      continue;
    }
    if (ch == '\n') {
      wrapped.push_back('\n');
      line_width = 0U;
      continue;
    }
    if (line_width >= MaxLine && ch == ' ') {
      wrapped.push_back('\n');
      line_width = 0U;
      continue;
    }
    if (line_width >= MaxLine) {
      wrapped.push_back('\n');
      line_width = 0U;
    }
    wrapped.push_back(ch);
    ++line_width;
  }
  if (reason.size() > MaxDisplay) {
    wrapped.append("...");
  }
  return wrapped;
}

class MultiplayerDisconnectScreen final : public UIScreen {
public:
  MultiplayerDisconnectScreen(
      std::string reason,
      std::optional<ServerListEntry> reconnect_server) : reason_(std::move(reason)),
                                                         reconnect_server_(std::move(reconnect_server)) {
  }

  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::MultiplayerDisconnect;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "multiplayer_disconnect";
  }

  void build(UIContext& ctx) override {
    const UiTheme& theme = ctx.tree.theme();
    const UiNodeID stack = add_menu_shell(ctx, screen_id(), "Disconnected", 472.0F, 344.0F);
    UiElement reason = make_text(
        "menu.disconnect.reason",
        wrapped_reason_text(reason_),
        theme.colors.text,
        UiTextRole::Body);
    reason.layout.anchor = UiAnchor::TopCenter;
    ctx.tree.add_child(stack, std::move(reason));

    add_button(
        ctx.tree,
        stack,
        "menu.disconnect.reconnect",
        "Reconnect",
        "disconnect.reconnect",
        reconnect_server_.has_value() && !reconnect_server_->address.empty(),
        false,
        UiControlRole::Primary);
    add_button(
        ctx.tree,
        stack,
        "menu.disconnect.server_list",
        "Server List",
        "disconnect.server_list");
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (action.action_id == "disconnect.reconnect") {
      if (reconnect_server_.has_value()) {
        submit_command(
            ctx,
            JoinServerCommand{reconnect_server_->name, reconnect_server_->address});
      }
      return true;
    }
    if (action.action_id == "disconnect.server_list") {
      return handle_cancel(ctx);
    }
    return false;
  }

  bool handle_cancel(UIContext& ctx) override {
    if (ctx.screens != nullptr) {
      ctx.screens->replace(make_multi_player_menu_screen());
      return true;
    }
    return UIScreen::handle_cancel(ctx);
  }

  [[nodiscard]] bool pauses_singleplayer() const noexcept override {
    return true;
  }

private:
  std::string reason_{};
  std::optional<ServerListEntry> reconnect_server_{};
};

class MultiplayerConnectingScreen final : public UIScreen {
public:
  explicit MultiplayerConnectingScreen(ServerListEntry server) : server_(std::move(server)) {
  }

  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::MultiplayerConnecting;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "multiplayer_connecting";
  }

  void build(UIContext& ctx) override {
    const UiTheme& theme = ctx.tree.theme();
    const UiNodeID stack =
        add_menu_shell(ctx, screen_id(), "Connecting to server...", 448.0F, 272.0F);
    UiElement status = make_text(
        "menu.connecting.status",
        "Waiting for the server to respond.",
        theme.colors.text,
        UiTextRole::Body);
    status.layout.anchor = UiAnchor::TopCenter;
    ctx.tree.add_child(stack, std::move(status));

    const std::string server_text =
        server_.name.empty() ? server_.address : server_.name + "\n" + server_.address;
    UiElement server = make_text(
        "menu.connecting.server",
        server_text,
        theme.colors.text_muted,
        UiTextRole::Small);
    server.layout.anchor = UiAnchor::TopCenter;
    ctx.tree.add_child(stack, std::move(server));

    add_button(
        ctx.tree,
        stack,
        "menu.connecting.cancel",
        "Cancel",
        "connecting.cancel",
        true,
        false,
        UiControlRole::Danger);
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (action.action_id == "connecting.cancel") {
      return handle_cancel(ctx);
    }
    return false;
  }

  bool handle_cancel(UIContext& ctx) override {
    submit_command(ctx, CancelServerConnectCommand{});
    if (ctx.screens != nullptr) {
      ctx.screens->replace(make_multi_player_menu_screen());
    }
    return true;
  }

  [[nodiscard]] bool can_close() const noexcept override {
    return false;
  }

  [[nodiscard]] bool pauses_singleplayer() const noexcept override {
    return true;
  }

private:
  ServerListEntry server_{};
};

class MultiPlayerMenuScreen final : public UIScreen {
public:
  [[nodiscard]] MenuScreenType type() const noexcept override {
    return MenuScreenType::MultiPlayerMenu;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return "multi_player_menu";
  }

  void build(UIContext& ctx) override {
    const std::vector<ServerListEntry> servers = servers_from(ctx);
    if (selected_server_.has_value() && selected_server_.value() >= servers.size()) {
      selected_server_.reset();
    }

    const UiTheme& theme = ctx.tree.theme();
    const UiNodeID stack = add_menu_shell(ctx, screen_id(), "Multi Player", 472.0F, 552.0F);
    if (servers.empty()) {
      UiElement empty = make_text(
          "menu.multi_player.empty",
          "No saved servers",
          theme.colors.text_muted,
          UiTextRole::Small);
      empty.layout.anchor = UiAnchor::TopCenter;
      ctx.tree.add_child(stack, std::move(empty));
    } else {
      for (usize index = 0; index < servers.size(); ++index) {
        const ServerListEntry& server = servers[index];
        add_list_row(
            ctx.tree,
            stack,
            "menu.multi_player.server." + std::to_string(index),
            server.name,
            server.address,
            "multi_player.select_server",
            true,
            selected_server_.has_value() && selected_server_.value() == index);
      }
    }

    add_button(
        ctx.tree,
        stack,
        "menu.multi_player.add",
        "Add Server",
        "multi_player.add",
        true,
        false,
        UiControlRole::Primary);
    add_button(
        ctx.tree,
        stack,
        "menu.multi_player.edit",
        "Edit Server",
        "multi_player.edit",
        selected_server_.has_value());
    add_button(
        ctx.tree,
        stack,
        "menu.multi_player.remove",
        "Remove Server",
        "multi_player.remove",
        selected_server_.has_value(),
        false,
        UiControlRole::Danger);
    add_button(ctx.tree, stack, "menu.multi_player.back", "Back", "menu.back");
    add_error_text(ctx.tree, stack, "menu.multi_player.error", error_);

    if (confirm_remove_ && selected_server_.has_value() &&
        selected_server_.value() < servers.size()) {
      add_backdrop(ctx.tree, "menu.multi_player.confirm.backdrop", {0.0F, 0.0F, 0.0F, 0.52F});
      UiElement panel = make_panel("menu.multi_player.confirm.panel", 392.0F, 194.0F);
      panel.layout.anchor = UiAnchor::Center;
      panel.layout.padding = {
          theme.widgets.panel.padding,
          theme.widgets.panel.padding,
          theme.widgets.panel.padding,
          theme.widgets.panel.padding};
      apply_panel_style(panel, theme, true);
      const UiNodeID panel_id = ctx.tree.add_child(ctx.tree.root_id(), std::move(panel));
      UiElement confirm_stack = make_stack("menu.multi_player.confirm.stack", theme.layout.spacing_md);
      const UiNodeID confirm_stack_id = ctx.tree.add_child(panel_id, std::move(confirm_stack));
      ctx.tree.add_child(
          confirm_stack_id,
          make_text(
              "menu.multi_player.confirm.text",
              "Remove " + servers[selected_server_.value()].name + "?",
              theme.colors.danger,
              UiTextRole::Heading));
      add_button(
          ctx.tree,
          confirm_stack_id,
          "menu.multi_player.confirm.remove",
          "Remove",
          "multi_player.confirm_remove",
          true,
          false,
          UiControlRole::Danger);
      add_button(
          ctx.tree,
          confirm_stack_id,
          "menu.multi_player.confirm.cancel",
          "Cancel",
          "multi_player.cancel_remove");
    }
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    const std::vector<ServerListEntry> servers = servers_from(ctx);
    if (action.action_id == "multi_player.cancel_remove") {
      confirm_remove_ = false;
      return true;
    }
    if (action.action_id == "multi_player.confirm_remove") {
      if (selected_server_.has_value() && selected_server_.value() < servers.size()) {
        submit_command(
            ctx,
            RemoveServerEntryCommand{selected_server_.value(), servers[selected_server_.value()]});
      }
      confirm_remove_ = false;
      selected_server_.reset();
      return true;
    }
    if (confirm_remove_) {
      return true;
    }

    usize index = 0;
    if (action.action_id == "multi_player.select_server" &&
        parse_index_suffix(action.element_id, "menu.multi_player.server.", index) &&
        index < servers.size()) {
      if (selected_server_.has_value() && selected_server_.value() == index) {
        submit_command(ctx, JoinServerCommand{servers[index].name, servers[index].address});
      } else {
        selected_server_ = index;
      }
      error_.clear();
      return true;
    }
    if (action.action_id == "multi_player.add" && ctx.screens != nullptr) {
      ctx.screens->push(make_add_server_screen());
      return true;
    }
    if (action.action_id == "multi_player.edit" && ctx.screens != nullptr) {
      if (selected_server_.has_value() && selected_server_.value() < servers.size()) {
        ctx.screens->push(
            make_edit_server_screen(selected_server_.value(), servers[selected_server_.value()]));
      }
      return true;
    }
    if (action.action_id == "multi_player.remove") {
      if (!selected_server_.has_value()) {
        error_ = "Select a server first";
      } else {
        confirm_remove_ = true;
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
    const std::vector<ServerListEntry> servers = servers_from(ctx);
    if (!selected_server_.has_value() || selected_server_.value() >= servers.size()) {
      return false;
    }
    submit_command(
        ctx,
        JoinServerCommand{
            servers[selected_server_.value()].name,
            servers[selected_server_.value()].address});
    return true;
  }

  [[nodiscard]] bool pauses_singleplayer() const noexcept override {
    return true;
  }

private:
  std::optional<usize> selected_server_{};
  bool confirm_remove_{};
  std::string error_{};
};

class AddEditServerScreen final : public UIScreen {
public:
  AddEditServerScreen(std::optional<usize> edit_index, ServerListEntry server) : edit_index_(edit_index),
                                                                                 server_name_(std::move(server.name)),
                                                                                 address_(std::move(server.address)) {
  }

  [[nodiscard]] MenuScreenType type() const noexcept override {
    return edit_index_.has_value() ? MenuScreenType::EditServer : MenuScreenType::AddServer;
  }

  [[nodiscard]] std::string_view screen_id() const noexcept override {
    return edit_index_.has_value() ? "edit_server" : "add_server";
  }

  void build(UIContext& ctx) override {
    const UiNodeID stack = add_menu_shell(
        ctx,
        screen_id(),
        edit_index_.has_value() ? "Edit Server" : "Add Server",
        448.0F,
        372.0F);
    add_text_box(
        ctx.tree,
        stack,
        "menu.server.name",
        "Server Name",
        server_name_,
        "Server name",
        "server.focus_name",
        active_field_ == TextField::ServerName);
    add_text_box(
        ctx.tree,
        stack,
        "menu.server.address",
        "Address",
        address_,
        "host or host:port",
        "server.focus_address",
        active_field_ == TextField::Address);
    add_error_text(ctx.tree, stack, "menu.server.error", error_);
    add_button(
        ctx.tree,
        stack,
        "menu.server.save",
        "Save",
        "server.save",
        true,
        false,
        UiControlRole::Primary);
    add_button(ctx.tree, stack, "menu.server.cancel", "Cancel", "menu.back");
  }

  bool handle_action(const UiAction& action, UIContext& ctx) override {
    if (action.action_id == "server.focus_name") {
      active_field_ = TextField::ServerName;
      return true;
    }
    if (action.action_id == "server.focus_address") {
      active_field_ = TextField::Address;
      return true;
    }
    if (action.action_id == "server.save") {
      return save(ctx);
    }
    if (action.action_id == "menu.back") {
      return handle_cancel(ctx);
    }
    return false;
  }

  bool handle_text_input(std::string_view text, UIContext&) override {
    if (active_field_ == TextField::ServerName) {
      server_name_ += text;
      return true;
    }
    if (active_field_ == TextField::Address) {
      address_ += text;
      return true;
    }
    return false;
  }

  bool handle_backspace(UIContext&) override {
    if (active_field_ == TextField::ServerName) {
      erase_last_utf8_codepoint(server_name_);
      return true;
    }
    if (active_field_ == TextField::Address) {
      erase_last_utf8_codepoint(address_);
      return true;
    }
    return false;
  }

  bool handle_submit(UIContext& ctx) override {
    return save(ctx);
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
  bool save(UIContext& ctx) {
    const std::string name = trim_copy(server_name_);
    const std::string address = trim_copy(address_);
    if (name.empty()) {
      error_ = "Server name is required";
      return true;
    }
    if (address.empty()) {
      error_ = "Address is required";
      return true;
    }
    if (!valid_server_address(address)) {
      error_ = "Address must be a host or host:port";
      return true;
    }
    submit_command(ctx, SaveServerEntryCommand{edit_index_, ServerListEntry{name, address}});
    if (ctx.screens != nullptr) {
      ctx.screens->pop();
    }
    return true;
  }

  std::optional<usize> edit_index_{};
  TextField active_field_{TextField::ServerName};
  std::string server_name_{};
  std::string address_{};
  std::string error_{};
};

} // namespace

std::unique_ptr<UIScreen> make_multi_player_menu_screen() {
  return std::make_unique<MultiPlayerMenuScreen>();
}

std::unique_ptr<UIScreen> make_multiplayer_connecting_screen(ServerListEntry server) {
  return std::make_unique<MultiplayerConnectingScreen>(std::move(server));
}

std::unique_ptr<UIScreen> make_multiplayer_disconnect_screen(
    std::string reason,
    std::optional<ServerListEntry> reconnect_server) {
  return std::make_unique<MultiplayerDisconnectScreen>(
      std::move(reason),
      std::move(reconnect_server));
}

std::unique_ptr<UIScreen> make_add_server_screen() {
  return std::make_unique<AddEditServerScreen>(std::nullopt, ServerListEntry{});
}

std::unique_ptr<UIScreen> make_edit_server_screen(usize server_index, ServerListEntry server) {
  return std::make_unique<AddEditServerScreen>(server_index, std::move(server));
}

} // namespace arv::ui
