#pragma once

#include "arv/api.h"
#include "arv/buffer.hpp"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <string_view>

namespace arv {

class PersistentData {
public:
  explicit constexpr PersistentData(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] Result unsupported() const {
    if (api_ == nullptr || api_->persistent_data == nullptr ||
        api_->persistent_data->unsupported == nullptr) {
      return Result::unsupported("persistent data API is unavailable");
    }
    return Result{api_->persistent_data->unsupported(api_->persistent_data_ctx)};
  }

  [[nodiscard]] Result set_world(
      ArvWorldHandle world,
      std::string_view key,
      ArvBytesView bytes) const {
    if (api_ == nullptr || api_->persistent_data == nullptr ||
        api_->persistent_data->set_world_data == nullptr) {
      return Result::unsupported("persistent data API is unavailable");
    }
    return Result{api_->persistent_data->set_world_data(
        api_->persistent_data_ctx,
        world,
        view(key),
        bytes)};
  }

  [[nodiscard]] BufferResult get_world(
      ArvWorldHandle world,
      std::string_view key) const {
    ArvByteBuffer buffer{};
    if (api_ == nullptr || api_->persistent_data == nullptr ||
        api_->persistent_data->get_world_data == nullptr) {
      return {Result::unsupported("persistent data API is unavailable"), {}};
    }
    Result result{api_->persistent_data->get_world_data(
        api_->persistent_data_ctx,
        world,
        view(key),
        &buffer)};
    return {result, Buffer{api_, buffer}};
  }

  [[nodiscard]] Result remove_world(
      ArvWorldHandle world,
      std::string_view key) const {
    if (api_ == nullptr || api_->persistent_data == nullptr ||
        api_->persistent_data->remove_world_data == nullptr) {
      return Result::unsupported("persistent data API is unavailable");
    }
    return Result{api_->persistent_data->remove_world_data(
        api_->persistent_data_ctx,
        world,
        view(key))};
  }

  [[nodiscard]] Result set_entity(
      ArvEntityHandle entity,
      std::string_view key,
      ArvBytesView bytes) const {
    if (api_ == nullptr || api_->persistent_data == nullptr ||
        api_->persistent_data->set_entity_data == nullptr) {
      return Result::unsupported("persistent data API is unavailable");
    }
    return Result{api_->persistent_data->set_entity_data(
        api_->persistent_data_ctx,
        entity,
        view(key),
        bytes)};
  }

  [[nodiscard]] BufferResult get_entity(
      ArvEntityHandle entity,
      std::string_view key) const {
    ArvByteBuffer buffer{};
    if (api_ == nullptr || api_->persistent_data == nullptr ||
        api_->persistent_data->get_entity_data == nullptr) {
      return {Result::unsupported("persistent data API is unavailable"), {}};
    }
    Result result{api_->persistent_data->get_entity_data(
        api_->persistent_data_ctx,
        entity,
        view(key),
        &buffer)};
    return {result, Buffer{api_, buffer}};
  }

  [[nodiscard]] Result remove_entity(
      ArvEntityHandle entity,
      std::string_view key) const {
    if (api_ == nullptr || api_->persistent_data == nullptr ||
        api_->persistent_data->remove_entity_data == nullptr) {
      return Result::unsupported("persistent data API is unavailable");
    }
    return Result{api_->persistent_data->remove_entity_data(
        api_->persistent_data_ctx,
        entity,
        view(key))};
  }

  [[nodiscard]] Result set_block(
      ArvWorldHandle world,
      ArvBlockPos pos,
      std::string_view key,
      ArvBytesView bytes) const {
    if (api_ == nullptr || api_->persistent_data == nullptr ||
        api_->persistent_data->set_block_data == nullptr) {
      return Result::unsupported("persistent data API is unavailable");
    }
    return Result{api_->persistent_data->set_block_data(
        api_->persistent_data_ctx,
        world,
        pos,
        view(key),
        bytes)};
  }

  [[nodiscard]] BufferResult get_block(
      ArvWorldHandle world,
      ArvBlockPos pos,
      std::string_view key) const {
    ArvByteBuffer buffer{};
    if (api_ == nullptr || api_->persistent_data == nullptr ||
        api_->persistent_data->get_block_data == nullptr) {
      return {Result::unsupported("persistent data API is unavailable"), {}};
    }
    Result result{api_->persistent_data->get_block_data(
        api_->persistent_data_ctx,
        world,
        pos,
        view(key),
        &buffer)};
    return {result, Buffer{api_, buffer}};
  }

  [[nodiscard]] Result remove_block(
      ArvWorldHandle world,
      ArvBlockPos pos,
      std::string_view key) const {
    if (api_ == nullptr || api_->persistent_data == nullptr ||
        api_->persistent_data->remove_block_data == nullptr) {
      return Result::unsupported("persistent data API is unavailable");
    }
    return Result{api_->persistent_data->remove_block_data(
        api_->persistent_data_ctx,
        world,
        pos,
        view(key))};
  }

private:
  const ArvAPI* api_{};
};

} // namespace arv
