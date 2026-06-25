#pragma once

#include "arv/base/ui/screens/screen.hpp"
#include "arv/base/ui/widgets/chat.hpp"
#include "arv/platform/input_snapshot.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace arv::client {

class ClientRuntime;

struct ChatMouseRoutingCallbacks {
  FunctionRef<bool(std::string_view)> open_url{};
  FunctionRef<bool(std::string_view)> copy_to_clipboard{};
  FunctionRef<void(std::string_view)> run_command{};
  std::string blocked_unsafe_url_message{};
  std::string copy_unavailable_message{};
};

[[nodiscard]] bool route_inventory_mouse_input(
    ui::UiTree& ui_tree,
    ClientRuntime& client,
    const platform::InputSnapshot& platform_input,
    bool has_carried_stack,
    ui::UICommandSink* command_sink = nullptr,
    const ui::UIScreenDataSource* data_source = nullptr);

[[nodiscard]] bool route_player_menu_captured_input(
    ClientRuntime& client,
    ui::UICommandSink* command_sink,
    const std::optional<ui::CapturedInputView>& captured_input);

[[nodiscard]] bool route_player_menu_captured_input(
    ui::UiTree& ui_tree,
    ClientRuntime& client,
    ui::UICommandSink* command_sink,
    const platform::InputSnapshot& platform_input);

[[nodiscard]] bool route_screen_mouse_input(
    ui::UIScreenManager& screen_manager,
    ui::UIContext& ui_context,
    const platform::InputSnapshot& platform_input);

[[nodiscard]] bool route_blocking_screen_input(
    ui::UIScreenManager& screen_manager,
    ui::UIContext& ui_context,
    const platform::InputSnapshot& platform_input);

[[nodiscard]] bool route_captured_screen_input(
    ui::UIScreenManager& screen_manager,
    ui::UIContext& ui_context,
    const platform::InputSnapshot& platform_input);

[[nodiscard]] bool route_captured_screen_input(
    ui::UIScreenManager& screen_manager,
    ui::UIContext& ui_context,
    const std::optional<ui::CapturedInputView>& captured_input);

[[nodiscard]] bool route_chat_mouse_input(
    ui::UiTree& ui_tree,
    ClientRuntime& client,
    const platform::InputSnapshot& platform_input,
    std::span<const ui::ChatLineView> chat_lines,
    usize chat_history_size,
    usize open_max_lines,
    usize& chat_history_scroll_offset,
    const ChatMouseRoutingCallbacks& callbacks);

void route_ui_mouse_focus(
    ui::UiTree& ui_tree,
    const platform::InputSnapshot& platform_input);

} // namespace arv::client
