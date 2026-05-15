#include "arv/variables.hpp"

namespace arv {
namespace {

[[nodiscard]] ArvVariableBinding abi_binding(VariableBinding binding) noexcept {
  return static_cast<ArvVariableBinding>(binding);
}

} // namespace

Variables::Variables(const ArvAPI* api) noexcept : api_(api) {
}

Result Variables::unsupported() const {
  return Result::unsupported("variables API is unavailable");
}

Result Variables::set_global(
    std::string_view name,
    std::string_view value,
    VariableBinding binding) const {
  if (api_ == nullptr || api_->variables == nullptr ||
      api_->variables->set_global_string == nullptr) {
    return unsupported();
  }
  if (binding != VariableBinding::Resolved) {
    if (api_->variables->set_global_string_bound == nullptr) {
      return unsupported();
    }
    return Result{api_->variables->set_global_string_bound(
        api_->variables_ctx,
        view(name),
        view(value),
        abi_binding(binding))};
  }
  return Result{api_->variables->set_global_string(
      api_->variables_ctx,
      view(name),
      view(value))};
}

Result Variables::set_global(
    std::string_view name,
    bool value,
    VariableBinding binding) const {
  if (api_ == nullptr || api_->variables == nullptr ||
      api_->variables->set_global_bool == nullptr) {
    return unsupported();
  }
  if (binding != VariableBinding::Resolved) {
    if (api_->variables->set_global_bool_bound == nullptr) {
      return unsupported();
    }
    return Result{api_->variables->set_global_bool_bound(
        api_->variables_ctx,
        view(name),
        value,
        abi_binding(binding))};
  }
  return Result{
      api_->variables->set_global_bool(api_->variables_ctx, view(name), value)};
}

Result Variables::set_global(
    std::string_view name,
    std::int64_t value,
    VariableBinding binding) const {
  if (api_ == nullptr || api_->variables == nullptr ||
      api_->variables->set_global_i64 == nullptr) {
    return unsupported();
  }
  if (binding != VariableBinding::Resolved) {
    if (api_->variables->set_global_i64_bound == nullptr) {
      return unsupported();
    }
    return Result{api_->variables->set_global_i64_bound(
        api_->variables_ctx,
        view(name),
        value,
        abi_binding(binding))};
  }
  return Result{
      api_->variables->set_global_i64(api_->variables_ctx, view(name), value)};
}

Result Variables::set_global(
    std::string_view name,
    std::uint64_t value,
    VariableBinding binding) const {
  if (api_ == nullptr || api_->variables == nullptr ||
      api_->variables->set_global_u64 == nullptr) {
    return unsupported();
  }
  if (binding != VariableBinding::Resolved) {
    if (api_->variables->set_global_u64_bound == nullptr) {
      return unsupported();
    }
    return Result{api_->variables->set_global_u64_bound(
        api_->variables_ctx,
        view(name),
        value,
        abi_binding(binding))};
  }
  return Result{
      api_->variables->set_global_u64(api_->variables_ctx, view(name), value)};
}

Result Variables::set_global(
    std::string_view name,
    double value,
    VariableBinding binding) const {
  if (api_ == nullptr || api_->variables == nullptr ||
      api_->variables->set_global_f64 == nullptr) {
    return unsupported();
  }
  if (binding != VariableBinding::Resolved) {
    if (api_->variables->set_global_f64_bound == nullptr) {
      return unsupported();
    }
    return Result{api_->variables->set_global_f64_bound(
        api_->variables_ctx,
        view(name),
        value,
        abi_binding(binding))};
  }
  return Result{
      api_->variables->set_global_f64(api_->variables_ctx, view(name), value)};
}

Result Variables::clear_global(std::string_view name) const {
  if (api_ == nullptr || api_->variables == nullptr ||
      api_->variables->clear_global == nullptr) {
    return unsupported();
  }
  return Result{api_->variables->clear_global(api_->variables_ctx, view(name))};
}

Result Variables::set_player(
    ArvPlayerID player,
    std::string_view name,
    std::string_view value,
    VariableBinding binding) const {
  if (api_ == nullptr || api_->variables == nullptr ||
      api_->variables->set_player_string == nullptr) {
    return unsupported();
  }
  if (binding != VariableBinding::Resolved) {
    if (api_->variables->set_player_string_bound == nullptr) {
      return unsupported();
    }
    return Result{api_->variables->set_player_string_bound(
        api_->variables_ctx,
        player,
        view(name),
        view(value),
        abi_binding(binding))};
  }
  return Result{api_->variables->set_player_string(
      api_->variables_ctx,
      player,
      view(name),
      view(value))};
}

Result Variables::set_player(
    ArvPlayerID player,
    std::string_view name,
    bool value,
    VariableBinding binding) const {
  if (api_ == nullptr || api_->variables == nullptr ||
      api_->variables->set_player_bool == nullptr) {
    return unsupported();
  }
  if (binding != VariableBinding::Resolved) {
    if (api_->variables->set_player_bool_bound == nullptr) {
      return unsupported();
    }
    return Result{api_->variables->set_player_bool_bound(
        api_->variables_ctx,
        player,
        view(name),
        value,
        abi_binding(binding))};
  }
  return Result{api_->variables->set_player_bool(
      api_->variables_ctx,
      player,
      view(name),
      value)};
}

Result Variables::set_player(
    ArvPlayerID player,
    std::string_view name,
    std::int64_t value,
    VariableBinding binding) const {
  if (api_ == nullptr || api_->variables == nullptr ||
      api_->variables->set_player_i64 == nullptr) {
    return unsupported();
  }
  if (binding != VariableBinding::Resolved) {
    if (api_->variables->set_player_i64_bound == nullptr) {
      return unsupported();
    }
    return Result{api_->variables->set_player_i64_bound(
        api_->variables_ctx,
        player,
        view(name),
        value,
        abi_binding(binding))};
  }
  return Result{api_->variables->set_player_i64(
      api_->variables_ctx,
      player,
      view(name),
      value)};
}

Result Variables::set_player(
    ArvPlayerID player,
    std::string_view name,
    std::uint64_t value,
    VariableBinding binding) const {
  if (api_ == nullptr || api_->variables == nullptr ||
      api_->variables->set_player_u64 == nullptr) {
    return unsupported();
  }
  if (binding != VariableBinding::Resolved) {
    if (api_->variables->set_player_u64_bound == nullptr) {
      return unsupported();
    }
    return Result{api_->variables->set_player_u64_bound(
        api_->variables_ctx,
        player,
        view(name),
        value,
        abi_binding(binding))};
  }
  return Result{api_->variables->set_player_u64(
      api_->variables_ctx,
      player,
      view(name),
      value)};
}

Result Variables::set_player(
    ArvPlayerID player,
    std::string_view name,
    double value,
    VariableBinding binding) const {
  if (api_ == nullptr || api_->variables == nullptr ||
      api_->variables->set_player_f64 == nullptr) {
    return unsupported();
  }
  if (binding != VariableBinding::Resolved) {
    if (api_->variables->set_player_f64_bound == nullptr) {
      return unsupported();
    }
    return Result{api_->variables->set_player_f64_bound(
        api_->variables_ctx,
        player,
        view(name),
        value,
        abi_binding(binding))};
  }
  return Result{api_->variables->set_player_f64(
      api_->variables_ctx,
      player,
      view(name),
      value)};
}

Result Variables::clear_player(ArvPlayerID player, std::string_view name) const {
  if (api_ == nullptr || api_->variables == nullptr ||
      api_->variables->clear_player == nullptr) {
    return unsupported();
  }
  return Result{api_->variables->clear_player(
      api_->variables_ctx,
      player,
      view(name))};
}

} // namespace arv
