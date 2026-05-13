#pragma once

#include "arv/api.h"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <string_view>

namespace arv {

class Entities {
public:
  explicit constexpr Entities(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] ValueResult<ArvEntityHandle> spawn(
      ArvWorldHandle world,
      std::string_view entity_type,
      ArvVec3 position) const {
    ArvEntityHandle entity{};
    if (api_ == nullptr || api_->entities == nullptr || api_->entities->spawn == nullptr) {
      return {Result::unsupported("entity API is unavailable"), entity};
    }
    return {
        Result{api_->entities->spawn(
            api_->entities_ctx,
            world,
            view(entity_type),
            position,
            &entity)},
        entity,
    };
  }

  [[nodiscard]] Result destroy(ArvEntityHandle entity) const {
    if (api_ == nullptr || api_->entities == nullptr || api_->entities->destroy == nullptr) {
      return Result::unsupported("entity API is unavailable");
    }
    return Result{api_->entities->destroy(api_->entities_ctx, entity)};
  }

  [[nodiscard]] bool exists(ArvEntityHandle entity) const {
    return api_ != nullptr && api_->entities != nullptr &&
           api_->entities->exists != nullptr &&
           api_->entities->exists(api_->entities_ctx, entity);
  }

  [[nodiscard]] ValueResult<ArvWorldHandle> world(ArvEntityHandle entity) const {
    ArvWorldHandle world_handle{};
    if (api_ == nullptr || api_->entities == nullptr || api_->entities->world == nullptr) {
      return {Result::unsupported("entity API is unavailable"), world_handle};
    }
    return {
        Result{api_->entities->world(api_->entities_ctx, entity, &world_handle)},
        world_handle,
    };
  }

  [[nodiscard]] ValueResult<ArvVec3> position(ArvEntityHandle entity) const {
    ArvVec3 position_value{};
    if (api_ == nullptr || api_->entities == nullptr ||
        api_->entities->position == nullptr) {
      return {Result::unsupported("entity API is unavailable"), position_value};
    }
    return {
        Result{api_->entities->position(api_->entities_ctx, entity, &position_value)},
        position_value,
    };
  }

  [[nodiscard]] Result set_position(ArvEntityHandle entity, ArvVec3 position) const {
    if (api_ == nullptr || api_->entities == nullptr ||
        api_->entities->set_position == nullptr) {
      return Result::unsupported("entity API is unavailable");
    }
    return Result{api_->entities->set_position(api_->entities_ctx, entity, position)};
  }

  [[nodiscard]] Result add_tag(ArvEntityHandle entity, std::string_view tag) const {
    return tag_call(api_ == nullptr || api_->entities == nullptr ? nullptr : api_->entities->add_tag, entity, tag);
  }

  [[nodiscard]] Result remove_tag(ArvEntityHandle entity, std::string_view tag) const {
    return tag_call(api_ == nullptr || api_->entities == nullptr ? nullptr : api_->entities->remove_tag, entity, tag);
  }

  [[nodiscard]] ValueResult<bool> has_tag(ArvEntityHandle entity, std::string_view tag) const {
    bool has{};
    if (api_ == nullptr || api_->entities == nullptr ||
        api_->entities->has_tag == nullptr) {
      return {Result::unsupported("entity API is unavailable"), has};
    }
    return {
        Result{api_->entities->has_tag(api_->entities_ctx, entity, view(tag), &has)},
        has,
    };
  }

private:
  using TagMember = ArvResult (*)(void*, ArvEntityHandle, ArvResourceID);

  [[nodiscard]] Result tag_call(
      TagMember member,
      ArvEntityHandle entity,
      std::string_view tag) const {
    if (api_ == nullptr || api_->entities == nullptr || member == nullptr) {
      return Result::unsupported("entity API is unavailable");
    }
    return Result{member(api_->entities_ctx, entity, view(tag))};
  }

  const ArvAPI* api_{};
};

} // namespace arv
