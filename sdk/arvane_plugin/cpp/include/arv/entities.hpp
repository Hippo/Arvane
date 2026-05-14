#pragma once

#include "arv/api.h"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <string_view>

namespace arv {

class Entities {
public:
  explicit Entities(const ArvAPI* api) noexcept;

  [[nodiscard]] ValueResult<ArvEntityHandle> spawn(
      ArvWorldHandle world,
      std::string_view entity_type,
      ArvVec3 position) const;

  [[nodiscard]] Result destroy(ArvEntityHandle entity) const;

  [[nodiscard]] bool exists(ArvEntityHandle entity) const;

  [[nodiscard]] ValueResult<ArvWorldHandle> world(ArvEntityHandle entity) const;

  [[nodiscard]] ValueResult<ArvVec3> position(ArvEntityHandle entity) const;

  [[nodiscard]] Result set_position(ArvEntityHandle entity, ArvVec3 position) const;

  [[nodiscard]] Result add_tag(ArvEntityHandle entity, std::string_view tag) const;

  [[nodiscard]] Result remove_tag(ArvEntityHandle entity, std::string_view tag) const;

  [[nodiscard]] ValueResult<bool> has_tag(ArvEntityHandle entity, std::string_view tag) const;

private:
  using TagMember = ArvResult (*)(void*, ArvEntityHandle, ArvResourceID);

  [[nodiscard]] Result tag_call(
      TagMember member,
      ArvEntityHandle entity,
      std::string_view tag) const;

  const ArvAPI* api_{};
};

} // namespace arv
