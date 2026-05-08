#pragma once

#include "arv/ui/screens/screen.hpp"

#include <memory>
#include <string>

namespace arv::ui {

struct AccountManagerDropdownState {
  bool open{};
  f32 scroll_y{};
};

void build_account_manager_dropdown(UIContext& ctx, AccountManagerDropdownState& state);
[[nodiscard]] bool handle_account_manager_action(
    const UiAction& action,
    UIContext& ctx,
    AccountManagerDropdownState& state);
[[nodiscard]] bool handle_account_manager_scroll(
    Vec2f cursor,
    f32 wheel_y,
    UIContext& ctx,
    AccountManagerDropdownState& state);

[[nodiscard]] std::unique_ptr<UIScreen> make_account_create_screen();
[[nodiscard]] std::unique_ptr<UIScreen> make_account_edit_screen(std::string profile_id);

} // namespace arv::ui
