#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/types.hpp"
#include "arv/text/rich_text.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::text {

inline constexpr usize MaxTextVariableNameBytes = 128U;
inline constexpr usize MaxTextVariableValueBytes = 1024U;

enum class TextVariableBinding : u8 {
  Resolved,
  Live,
};

enum class TextVariableResolution : u8 {
  All,
  ResolvedOnly,
  LiveOnly,
};

struct TextVariableValue {
  std::string value{};
  TextVariableBinding binding{TextVariableBinding::Resolved};
};

using TextVariableMap = HashMap<std::string, TextVariableValue>;

class TextVariableStore {
public:
  void clear();
  void clear_player(PlayerID player_id);

  void set_global(
      std::string name,
      std::string value,
      TextVariableBinding binding = TextVariableBinding::Resolved);
  void set_player(
      PlayerID player_id,
      std::string name,
      std::string value,
      TextVariableBinding binding = TextVariableBinding::Resolved);

  [[nodiscard]] bool clear_global(std::string_view name);
  [[nodiscard]] bool clear_player_value(PlayerID player_id, std::string_view name);

  [[nodiscard]] std::optional<std::string_view> global(
      std::string_view name) const;
  [[nodiscard]] std::optional<std::string_view> player(
      PlayerID player_id,
      std::string_view name) const;
  [[nodiscard]] std::optional<std::string_view> resolve(
      std::optional<PlayerID> player_id,
      std::string_view name,
      TextVariableResolution resolution = TextVariableResolution::All) const;
  [[nodiscard]] std::optional<TextVariableBinding> global_binding(
      std::string_view name) const;
  [[nodiscard]] std::optional<TextVariableBinding> player_binding(
      PlayerID player_id,
      std::string_view name) const;

  [[nodiscard]] const TextVariableMap& globals() const noexcept;
  [[nodiscard]] const TextVariableMap* player_values(
      PlayerID player_id) const noexcept;

private:
  TextVariableMap globals_{};
  HashMap<PlayerID, TextVariableMap> players_{};
};

struct TextVariableResolver {
  const TextVariableStore* store{};
  std::optional<PlayerID> player_id{};
  TextVariableResolution resolution{TextVariableResolution::All};

  [[nodiscard]] std::optional<std::string_view> resolve(
      std::string_view name) const;
};

[[nodiscard]] bool valid_text_variable_name(std::string_view name) noexcept;
[[nodiscard]] bool is_reserved_builtin_text_variable(std::string_view name) noexcept;
[[nodiscard]] std::string text_variable_placeholder(std::string_view name);
[[nodiscard]] std::string interpolate_text_variables(
    std::string_view text,
    const TextVariableResolver& resolver = {});
[[nodiscard]] RichText interpolate_rich_text_variables(
    const RichText& source,
    const TextVariableResolver& resolver = {});

} // namespace arv::text
