#pragma once

#include "arv/core/primitives.hpp"

#include <cassert>
#include <functional>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <ankerl/unordered_dense.h>

namespace arv {

template<class K, class V, class Hash = ankerl::unordered_dense::hash<K>,
         class Eq = std::equal_to<K>>
using HashMap = ankerl::unordered_dense::map<K, V, Hash, Eq>;

template<class T, class Hash = ankerl::unordered_dense::hash<T>,
         class Eq = std::equal_to<T>>
using HashSet = ankerl::unordered_dense::set<T, Hash, Eq>;

template<class K, class V, class Hash = std::hash<K>,
         class Eq = std::equal_to<K>>
using StableHashMap = std::unordered_map<K, V, Hash, Eq>;

template<class T, class Hash = std::hash<T>,
         class Eq = std::equal_to<T>>
using StableHashSet = std::unordered_set<T, Hash, Eq>;

template<typename>
class FunctionRef;

template<typename R, typename... Args>
class FunctionRef<R(Args...)> {
  union Storage {
    void* object;
    R (*function)(Args...);

    constexpr Storage() noexcept : object(nullptr) {
    }

    constexpr explicit Storage(void* value) noexcept : object(value) {
    }
  };

public:
  FunctionRef() = default;

  FunctionRef(std::nullptr_t) noexcept {
  }

  FunctionRef(R (*fn)(Args...)) noexcept {
    if (fn == nullptr) {
      return;
    }
    storage_.function = fn;
    callback_ = [](Storage storage, Args... args) -> R {
      return storage.function(std::forward<Args>(args)...);
    };
  }

  template<
      typename F,
      typename Fn = std::remove_reference_t<F>,
      typename = std::enable_if_t<
          !std::is_same_v<Fn, FunctionRef> &&
          std::is_invocable_r_v<R, Fn&, Args...>>>
  FunctionRef(F&& fn) noexcept
      : storage_{const_cast<void*>(
            static_cast<const void*>(std::addressof(fn)))},
        callback_([](Storage storage, Args... args) -> R {
          return std::invoke(
              *static_cast<Fn*>(storage.object),
              std::forward<Args>(args)...);
        }) {
  }

  R operator()(Args... args) const {
    assert(callback_ && "Called empty FunctionRef");
    return callback_(storage_, std::forward<Args>(args)...);
  }

  explicit operator bool() const noexcept {
    return callback_ != nullptr;
  }

private:
  Storage storage_{};
  R (*callback_)(Storage, Args...) = nullptr;
};

} // namespace arv
