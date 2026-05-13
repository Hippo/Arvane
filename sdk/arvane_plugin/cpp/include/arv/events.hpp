#pragma once

#include "arv/api.h"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace arv {

enum class EventPriority {
  Low = ARV_EVENT_PRIORITY_LOW,
  Normal = ARV_EVENT_PRIORITY_NORMAL,
  High = ARV_EVENT_PRIORITY_HIGH,
  Monitor = ARV_EVENT_PRIORITY_MONITOR,
};

class Event {
public:
  explicit constexpr Event(const ArvEvent* event) noexcept : event_(event) {
  }

  [[nodiscard]] std::string_view id() const noexcept {
    return event_ == nullptr ? std::string_view{} : view(event_->event_id);
  }

  [[nodiscard]] ArvTickIndex tick() const noexcept {
    return event_ == nullptr ? 0 : event_->tick;
  }

  [[nodiscard]] ArvWorldHandle world() const noexcept {
    return event_ == nullptr ? 0 : event_->world;
  }

  [[nodiscard]] std::span<const std::byte> payload() const noexcept {
    if (event_ == nullptr) {
      return {};
    }
    return {
        reinterpret_cast<const std::byte*>(event_->payload.data),
        event_->payload.size,
    };
  }

private:
  const ArvEvent* event_{};
};

class Events {
public:
  explicit constexpr Events(const ArvAPI* api) noexcept : api_(api) {
  }

  template<typename Handler>
  [[nodiscard]] Result subscribe(
      std::string_view event_id,
      Handler&& handler,
      EventPriority priority = EventPriority::Normal,
      ArvEventSubscriptionHandle* out_handle = nullptr) const {
    if (api_ == nullptr || api_->events == nullptr || api_->events->subscribe == nullptr) {
      return Result::unsupported("event API is unavailable");
    }
    using State = HandlerState<std::decay_t<Handler>>;
    auto state = std::make_unique<State>(std::forward<Handler>(handler));
    Result result{api_->events->subscribe(
        api_->events_ctx,
        view(event_id),
        static_cast<ArvEventPriority>(priority),
        &State::invoke,
        state.get(),
        out_handle)};
    if (result) {
      state.release();
    }
    return result;
  }

  [[nodiscard]] Result unsubscribe(ArvEventSubscriptionHandle handle) const {
    if (api_ == nullptr || api_->events == nullptr || api_->events->unsubscribe == nullptr) {
      return Result::unsupported("event API is unavailable");
    }
    return Result{api_->events->unsubscribe(api_->events_ctx, handle)};
  }

private:
  template<typename Handler>
  struct HandlerState {
    template<typename HandlerValue>
    explicit HandlerState(HandlerValue&& handler_value) : handler(std::forward<HandlerValue>(handler_value)) {
    }

    static void invoke(const ArvEvent* event, void* user_data) {
      auto* self = static_cast<HandlerState*>(user_data);
      if (self != nullptr) {
        std::invoke(self->handler, Event{event});
      }
    }

    Handler handler;
  };

  const ArvAPI* api_{};
};

} // namespace arv
