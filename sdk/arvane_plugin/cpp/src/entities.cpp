#include "arv/entities.hpp"

namespace arv {

Entities::Entities(const ArvAPI* api) noexcept : api_(api) {
}

ValueResult<ArvEntityHandle> Entities::spawn(
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

Result Entities::destroy(ArvEntityHandle entity) const {
  if (api_ == nullptr || api_->entities == nullptr || api_->entities->destroy == nullptr) {
    return Result::unsupported("entity API is unavailable");
  }
  return Result{api_->entities->destroy(api_->entities_ctx, entity)};
}

bool Entities::exists(ArvEntityHandle entity) const {
  return api_ != nullptr && api_->entities != nullptr &&
         api_->entities->exists != nullptr &&
         api_->entities->exists(api_->entities_ctx, entity);
}

ValueResult<ArvWorldHandle> Entities::world(ArvEntityHandle entity) const {
  ArvWorldHandle world_handle{};
  if (api_ == nullptr || api_->entities == nullptr || api_->entities->world == nullptr) {
    return {Result::unsupported("entity API is unavailable"), world_handle};
  }
  return {
      Result{api_->entities->world(api_->entities_ctx, entity, &world_handle)},
      world_handle,
  };
}

ValueResult<ArvVec3> Entities::position(ArvEntityHandle entity) const {
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

Result Entities::set_position(ArvEntityHandle entity, ArvVec3 position) const {
  if (api_ == nullptr || api_->entities == nullptr ||
      api_->entities->set_position == nullptr) {
    return Result::unsupported("entity API is unavailable");
  }
  return Result{api_->entities->set_position(api_->entities_ctx, entity, position)};
}

Result Entities::add_tag(ArvEntityHandle entity, std::string_view tag) const {
  return tag_call(api_ == nullptr || api_->entities == nullptr ? nullptr : api_->entities->add_tag, entity, tag);
}

Result Entities::remove_tag(ArvEntityHandle entity, std::string_view tag) const {
  return tag_call(api_ == nullptr || api_->entities == nullptr ? nullptr : api_->entities->remove_tag, entity, tag);
}

ValueResult<bool> Entities::has_tag(ArvEntityHandle entity, std::string_view tag) const {
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

Result Entities::tag_call(
    TagMember member,
    ArvEntityHandle entity,
    std::string_view tag) const {
  if (api_ == nullptr || api_->entities == nullptr || member == nullptr) {
    return Result::unsupported("entity API is unavailable");
  }
  return Result{member(api_->entities_ctx, entity, view(tag))};
}

} // namespace arv
