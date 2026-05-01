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

void UIScreenManager::push(std::unique_ptr<UIScreen> screen) {
  if (!screen) {
    return;
  }
  screen->on_enter();
  stack_.push_back(std::move(screen));
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
}

void UIScreenManager::replace(std::unique_ptr<UIScreen> screen) {
  if (!screen) {
    return;
  }
  if (!stack_.empty()) {
    stack_.back()->on_exit();
    stack_.pop_back();
  }
  push(std::move(screen));
}

void UIScreenManager::clear() {
  while (!stack_.empty()) {
    stack_.back()->on_exit();
    stack_.pop_back();
  }
}

UIScreen* UIScreenManager::current() noexcept {
  return stack_.empty() ? nullptr : stack_.back().get();
}

const UIScreen* UIScreenManager::current() const noexcept {
  return stack_.empty() ? nullptr : stack_.back().get();
}

void UIScreenManager::update(UIContext& ctx) {
  ctx.tree.clear();
  append_to(ctx);
}

void UIScreenManager::append_to(UIContext& ctx) {
  ctx.screens = this;
  if (UIScreen* screen = current(); screen != nullptr) {
    screen->build(ctx);
  }
}

bool UIScreenManager::route_action(const UiAction& action, UIContext& ctx) {
  ctx.screens = this;
  UIScreen* screen = current();
  return screen != nullptr && screen->handle_action(action, ctx);
}

bool UIScreenManager::handle_cancel(UIContext& ctx) {
  ctx.screens = this;
  UIScreen* screen = current();
  return screen != nullptr && screen->handle_cancel(ctx);
}

bool UIScreenManager::handle_text_input(std::string_view text, UIContext& ctx) {
  ctx.screens = this;
  UIScreen* screen = current();
  return screen != nullptr && screen->handle_text_input(text, ctx);
}

bool UIScreenManager::handle_backspace(UIContext& ctx) {
  ctx.screens = this;
  UIScreen* screen = current();
  return screen != nullptr && screen->handle_backspace(ctx);
}

bool UIScreenManager::handle_submit(UIContext& ctx) {
  ctx.screens = this;
  UIScreen* screen = current();
  return screen != nullptr && screen->handle_submit(ctx);
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
