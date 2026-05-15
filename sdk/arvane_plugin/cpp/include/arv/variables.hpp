#pragma once

#include "arv/api.h"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <cstdint>
#include <string_view>

namespace arv {

enum class VariableBinding {
  Resolved = ARV_VARIABLE_BINDING_RESOLVED,
  Live = ARV_VARIABLE_BINDING_LIVE,
};

class Variables {
public:
  explicit Variables(const ArvAPI* api) noexcept;

  [[nodiscard]] Result set_global(
      std::string_view name,
      std::string_view value,
      VariableBinding binding = VariableBinding::Resolved) const;
  [[nodiscard]] Result set_global(
      std::string_view name,
      bool value,
      VariableBinding binding = VariableBinding::Resolved) const;
  [[nodiscard]] Result set_global(
      std::string_view name,
      std::int64_t value,
      VariableBinding binding = VariableBinding::Resolved) const;
  [[nodiscard]] Result set_global(
      std::string_view name,
      std::uint64_t value,
      VariableBinding binding = VariableBinding::Resolved) const;
  [[nodiscard]] Result set_global(
      std::string_view name,
      double value,
      VariableBinding binding = VariableBinding::Resolved) const;
  [[nodiscard]] Result clear_global(std::string_view name) const;

  [[nodiscard]] Result set_player(
      ArvPlayerID player,
      std::string_view name,
      std::string_view value,
      VariableBinding binding = VariableBinding::Resolved) const;
  [[nodiscard]] Result set_player(
      ArvPlayerID player,
      std::string_view name,
      bool value,
      VariableBinding binding = VariableBinding::Resolved) const;
  [[nodiscard]] Result set_player(
      ArvPlayerID player,
      std::string_view name,
      std::int64_t value,
      VariableBinding binding = VariableBinding::Resolved) const;
  [[nodiscard]] Result set_player(
      ArvPlayerID player,
      std::string_view name,
      std::uint64_t value,
      VariableBinding binding = VariableBinding::Resolved) const;
  [[nodiscard]] Result set_player(
      ArvPlayerID player,
      std::string_view name,
      double value,
      VariableBinding binding = VariableBinding::Resolved) const;
  [[nodiscard]] Result clear_player(
      ArvPlayerID player,
      std::string_view name) const;

private:
  [[nodiscard]] Result unsupported() const;

  const ArvAPI* api_{};
};

} // namespace arv
