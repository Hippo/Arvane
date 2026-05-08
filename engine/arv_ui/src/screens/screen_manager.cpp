#include "arv/ui/screens/screen.hpp"

#include <algorithm>
#include <utility>

namespace arv::ui {

bool UIScreen::handle_action(const UiAction& action, UIContext& ctx) {
  (void)action;
  (void)ctx;
  return false;
}

bool UIScreen::handle_cancel(UIContext& ctx) {
  if (ctx.screens != nullptr && can_close()) {
    ctx.screens->pop();
    return true;
  }
  return false;
}

bool UIScreen::handle_text_input(std::string_view text, UIContext& ctx) {
  (void)text;
  (void)ctx;
  return false;
}

bool UIScreen::handle_backspace(UIContext& ctx) {
  (void)ctx;
  return false;
}

bool UIScreen::handle_submit(UIContext& ctx) {
  (void)ctx;
  return false;
}

bool UIScreen::handle_scroll(Vec2f cursor, f32 wheel_y, UIContext& ctx) {
  (void)cursor;
  (void)wheel_y;
  (void)ctx;
  return false;
}

bool UIScreen::handle_pointer(
    Vec2f cursor,
    Vec2f delta,
    bool left_pressed,
    bool left_released,
    bool left_held,
    UIContext& ctx) {
  (void)cursor;
  (void)delta;
  (void)left_pressed;
  (void)left_released;
  (void)left_held;
  (void)ctx;
  return false;
}

std::optional<PlayerSkinPreviewRequest> UIScreen::player_skin_preview(
    const UIContext& ctx) const {
  (void)ctx;
  return std::nullopt;
}

void UIScreenManager::push(std::unique_ptr<UIScreen> screen) {
  if (!screen) {
    return;
  }
  screen->on_enter();
  stack_.push_back(std::move(screen));
  mark_dirty();
}

void UIScreenManager::pop() {
  if (stack_.empty()) {
    return;
  }
  if (stack_.size() == 1U && !stack_.back()->can_close()) {
    return;
  }
  stack_.back()->on_exit();
  stack_.pop_back();
  mark_dirty();
}

void UIScreenManager::replace(std::unique_ptr<UIScreen> screen) {
  if (!screen) {
    return;
  }
  if (!stack_.empty()) {
    stack_.back()->on_exit();
    stack_.pop_back();
    mark_dirty();
  }
  push(std::move(screen));
}

void UIScreenManager::clear() {
  const bool had_screens = !stack_.empty();
  while (!stack_.empty()) {
    stack_.back()->on_exit();
    stack_.pop_back();
  }
  if (had_screens) {
    mark_dirty();
  }
}

UIScreen* UIScreenManager::current() noexcept {
  return stack_.empty() ? nullptr : stack_.back().get();
}

const UIScreen* UIScreenManager::current() const noexcept {
  return stack_.empty() ? nullptr : stack_.back().get();
}

void UIScreenManager::update(UIContext& ctx) {
  ctx.tree.set_theme(ctx.theme);
  ctx.tree.clear();
  append_to(ctx);
}

void UIScreenManager::append_to(UIContext& ctx) {
  ctx.tree.set_theme(ctx.theme);
  ctx.screens = this;
  if (UIScreen* screen = current(); screen != nullptr) {
    screen->build(ctx);
  }
}

void UIScreenManager::mark_dirty() noexcept {
  ++revision_;
}

bool UIScreenManager::route_action(const UiAction& action, UIContext& ctx) {
  ctx.screens = this;
  UIScreen* screen = current();
  const bool handled = screen != nullptr && screen->handle_action(action, ctx);
  if (handled) {
    mark_dirty();
  }
  return handled;
}

bool UIScreenManager::handle_cancel(UIContext& ctx) {
  ctx.screens = this;
  UIScreen* screen = current();
  const bool handled = screen != nullptr && screen->handle_cancel(ctx);
  if (handled) {
    mark_dirty();
  }
  return handled;
}

bool UIScreenManager::handle_text_input(std::string_view text, UIContext& ctx) {
  ctx.screens = this;
  UIScreen* screen = current();
  const bool handled = screen != nullptr && screen->handle_text_input(text, ctx);
  if (handled) {
    mark_dirty();
  }
  return handled;
}

bool UIScreenManager::handle_backspace(UIContext& ctx) {
  ctx.screens = this;
  UIScreen* screen = current();
  const bool handled = screen != nullptr && screen->handle_backspace(ctx);
  if (handled) {
    mark_dirty();
  }
  return handled;
}

bool UIScreenManager::handle_submit(UIContext& ctx) {
  ctx.screens = this;
  UIScreen* screen = current();
  const bool handled = screen != nullptr && screen->handle_submit(ctx);
  if (handled) {
    mark_dirty();
  }
  return handled;
}

bool UIScreenManager::handle_scroll(Vec2f cursor, f32 wheel_y, UIContext& ctx) {
  ctx.screens = this;
  UIScreen* screen = current();
  const bool handled = screen != nullptr && screen->handle_scroll(cursor, wheel_y, ctx);
  if (handled) {
    mark_dirty();
  }
  return handled;
}

bool UIScreenManager::handle_pointer(
    Vec2f cursor,
    Vec2f delta,
    bool left_pressed,
    bool left_released,
    bool left_held,
    UIContext& ctx) {
  ctx.screens = this;
  UIScreen* screen = current();
  const bool handled =
      screen != nullptr &&
      screen->handle_pointer(
          cursor,
          delta,
          left_pressed,
          left_released,
          left_held,
          ctx);
  if (handled) {
    mark_dirty();
  }
  return handled;
}

bool UIScreenManager::blocks_gameplay_input() const noexcept {
  const UIScreen* screen = current();
  return screen != nullptr && screen->blocks_gameplay_input();
}

bool UIScreenManager::pauses_singleplayer() const noexcept {
  return std::any_of(stack_.begin(), stack_.end(), [](const std::unique_ptr<UIScreen>& screen) {
    return screen != nullptr && screen->pauses_singleplayer();
  });
}

} // namespace arv::ui
