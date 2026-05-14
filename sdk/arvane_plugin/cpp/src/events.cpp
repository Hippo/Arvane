#include "arv/events.hpp"

namespace arv {

Event::Event(const ArvEvent* event) noexcept : event_(event) {
}

std::string_view Event::id() const noexcept {
  return event_ == nullptr ? std::string_view{} : view(event_->event_id);
}

ArvTickIndex Event::tick() const noexcept {
  return event_ == nullptr ? 0 : event_->tick;
}

ArvWorldHandle Event::world() const noexcept {
  return event_ == nullptr ? 0 : event_->world;
}

std::span<const std::byte> Event::payload() const noexcept {
  if (event_ == nullptr) {
    return {};
  }
  return {
      reinterpret_cast<const std::byte*>(event_->payload.data),
      event_->payload.size,
  };
}

Events::Events(const ArvAPI* api) noexcept : api_(api) {
}

Result Events::unsubscribe(ArvEventSubscriptionHandle handle) const {
  if (api_ == nullptr || api_->events == nullptr || api_->events->unsubscribe == nullptr) {
    return Result::unsupported("event API is unavailable");
  }
  return Result{api_->events->unsubscribe(api_->events_ctx, handle)};
}

} // namespace arv
