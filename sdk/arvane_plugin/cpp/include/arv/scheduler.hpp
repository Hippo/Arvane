#pragma once

#include "arv/api.h"
#include "arv/result.hpp"

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace arv {

class Job {
public:
  Job() = default;

  template<typename Handler>
  explicit Job(Handler&& handler) : handler_(std::forward<Handler>(handler)) {
  }

  void operator()() const {
    if (handler_) {
      handler_();
    }
  }

  [[nodiscard]] ArvJobCallback callback() const noexcept {
    return &invoke;
  }

  [[nodiscard]] void* user_data() noexcept {
    return this;
  }

private:
  static void invoke(void* user_data) {
    auto* self = static_cast<Job*>(user_data);
    if (self != nullptr) {
      (*self)();
    }
  }

  std::function<void()> handler_{};
};

class Scheduler {
public:
  explicit constexpr Scheduler(const ArvAPI* api) noexcept : api_(api) {
  }

  template<typename Handler>
  [[nodiscard]] Result sync(Handler&& handler, ArvScheduledJobHandle* out_handle = nullptr) const {
    return schedule_once(
        api_ == nullptr || api_->scheduler == nullptr ? nullptr : api_->scheduler->sync_now,
        0,
        0,
        std::forward<Handler>(handler),
        out_handle);
  }

  template<typename Handler>
  [[nodiscard]] Result sync(
      ArvTickIndex delay,
      Handler&& handler,
      ArvScheduledJobHandle* out_handle = nullptr) const {
    return schedule_once(
        api_ == nullptr || api_->scheduler == nullptr ? nullptr : api_->scheduler->sync_delayed,
        delay,
        0,
        std::forward<Handler>(handler),
        out_handle);
  }

  [[nodiscard]] Result sync_repeat(
      ArvTickIndex start_delay,
      ArvTickIndex interval,
      Job& job,
      ArvScheduledJobHandle* out_handle = nullptr) const {
    if (api_ == nullptr || api_->scheduler == nullptr ||
        api_->scheduler->sync_repeating == nullptr) {
      return Result::unsupported("scheduler API is unavailable");
    }
    return Result{api_->scheduler->sync_repeating(
        api_->scheduler_ctx,
        start_delay,
        interval,
        job.callback(),
        job.user_data(),
        out_handle)};
  }

  template<typename Handler>
  [[nodiscard]] Result async(
      Handler&& handler,
      ArvScheduledJobHandle* out_handle = nullptr) const {
    return schedule_once(
        api_ == nullptr || api_->scheduler == nullptr ? nullptr : api_->scheduler->async_now,
        0,
        0,
        std::forward<Handler>(handler),
        out_handle);
  }

  template<typename Handler>
  [[nodiscard]] Result async(
      ArvTickIndex delay,
      Handler&& handler,
      ArvScheduledJobHandle* out_handle = nullptr) const {
    return schedule_once(
        api_ == nullptr || api_->scheduler == nullptr ? nullptr : api_->scheduler->async_delayed,
        delay,
        0,
        std::forward<Handler>(handler),
        out_handle);
  }

  [[nodiscard]] Result async_repeat(
      ArvTickIndex start_delay,
      ArvTickIndex interval,
      Job& job,
      ArvScheduledJobHandle* out_handle = nullptr) const {
    if (api_ == nullptr || api_->scheduler == nullptr ||
        api_->scheduler->async_repeating == nullptr) {
      return Result::unsupported("scheduler API is unavailable");
    }
    return Result{api_->scheduler->async_repeating(
        api_->scheduler_ctx,
        start_delay,
        interval,
        job.callback(),
        job.user_data(),
        out_handle)};
  }

  [[nodiscard]] Result cancel(ArvScheduledJobHandle handle) const {
    if (api_ == nullptr || api_->scheduler == nullptr || api_->scheduler->cancel == nullptr) {
      return Result::unsupported("scheduler API is unavailable");
    }
    return Result{api_->scheduler->cancel(api_->scheduler_ctx, handle)};
  }

private:
  using NowMember = ArvResult (*)(
      void*,
      ArvJobCallback,
      void*,
      ArvScheduledJobHandle*);
  using DelayedMember = ArvResult (*)(
      void*,
      ArvTickIndex,
      ArvJobCallback,
      void*,
      ArvScheduledJobHandle*);

  template<typename Handler>
  struct OnceState {
    template<typename HandlerValue>
    explicit OnceState(HandlerValue&& handler_value) : handler(std::forward<HandlerValue>(handler_value)) {
    }

    static void invoke(void* user_data) {
      std::unique_ptr<OnceState> self{static_cast<OnceState*>(user_data)};
      if (self) {
        std::invoke(self->handler);
      }
    }

    Handler handler;
  };

  template<typename Member, typename Handler>
  [[nodiscard]] Result schedule_once(
      Member member,
      ArvTickIndex delay,
      ArvTickIndex,
      Handler&& handler,
      ArvScheduledJobHandle* out_handle) const {
    if (api_ == nullptr || api_->scheduler == nullptr || member == nullptr) {
      return Result::unsupported("scheduler API is unavailable");
    }
    using State = OnceState<std::decay_t<Handler>>;
    auto state = std::make_unique<State>(std::forward<Handler>(handler));
    ArvResult abi_result{};
    if constexpr (std::is_same_v<Member, NowMember>) {
      abi_result = member(
          api_->scheduler_ctx,
          &State::invoke,
          state.get(),
          out_handle);
    } else {
      abi_result = member(
          api_->scheduler_ctx,
          delay,
          &State::invoke,
          state.get(),
          out_handle);
    }
    Result result{abi_result};
    if (result) {
      state.release();
    }
    return result;
  }

  const ArvAPI* api_{};
};

} // namespace arv
