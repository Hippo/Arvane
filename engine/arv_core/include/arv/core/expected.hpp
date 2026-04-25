#pragma once

#include <utility>
#include <variant>

namespace arv {

template<typename T, typename E>
class Expected {
public:
  static Expected success(T value) {
    return Expected(std::move(value));
  }

  static Expected failure(E error) {
    return Expected(std::move(error));
  }

  [[nodiscard]] bool has_value() const noexcept {
    return std::holds_alternative<T>(storage_);
  }

  explicit operator bool() const noexcept {
    return has_value();
  }

  T& value() {
    return std::get<T>(storage_);
  }

  const T& value() const {
    return std::get<T>(storage_);
  }

  E& error() {
    return std::get<E>(storage_);
  }

  const E& error() const {
    return std::get<E>(storage_);
  }

private:
  explicit Expected(T value) : storage_(std::move(value)) {}
  explicit Expected(E error) : storage_(std::move(error)) {}

  std::variant<T, E> storage_;
};

template<typename E>
class Expected<void, E> {
public:
  static Expected success() {
    return Expected();
  }

  static Expected failure(E error) {
    return Expected(std::move(error));
  }

  [[nodiscard]] bool has_value() const noexcept {
    return ok_;
  }

  explicit operator bool() const noexcept {
    return has_value();
  }

  E& error() {
    return error_;
  }

  const E& error() const {
    return error_;
  }

private:
  Expected() = default;
  explicit Expected(E error) : ok_(false), error_(std::move(error)) {}

  bool ok_{true};
  E error_{};
};

} // namespace arv
