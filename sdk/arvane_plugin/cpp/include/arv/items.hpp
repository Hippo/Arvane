#pragma once

#include "arv/api.h"
#include "arv/data.hpp"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <cstdint>
#include <string_view>

namespace arv {

struct ItemStackInfo {
  bool present{};
  std::uint32_t item_id{};
  std::string_view item_name{};
  std::uint16_t count{};
  bool has_data{};
};

class ItemStackRef {
public:
  ItemStackRef() noexcept;
  ItemStackRef(const ArvAPI* api, ArvItemStackLocation location) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] ArvItemStackLocation location() const noexcept;

  [[nodiscard]] ValueResult<ItemStackInfo> info() const;
  [[nodiscard]] ValueResult<DataDocument> load_data() const;
  [[nodiscard]] ValueResult<DataDocument> load_namespace_data() const;
  [[nodiscard]] ValueResult<DataDocument> load_namespace_data(std::string_view name_space) const;
  [[nodiscard]] Result save_namespace_data(const DataDocument& document) const;
  [[nodiscard]] Result save_namespace_data(
      std::string_view name_space,
      const DataDocument& document) const;
  [[nodiscard]] Result clear_namespace_data() const;
  [[nodiscard]] Result clear_namespace_data(std::string_view name_space) const;

private:
  [[nodiscard]] std::string_view default_namespace() const;
  [[nodiscard]] bool available() const noexcept;

  const ArvAPI* api_{};
  ArvItemStackLocation location_{};
};

class Items {
public:
  explicit Items(const ArvAPI* api) noexcept;

  [[nodiscard]] ItemStackRef selected_hotbar(ArvPlayerID player) const noexcept;
  [[nodiscard]] ItemStackRef inventory_slot(
      ArvPlayerID player,
      std::uint32_t slot_index) const noexcept;
  [[nodiscard]] ItemStackRef cursor(ArvPlayerID player) const noexcept;

  [[nodiscard]] Result give(
      ArvPlayerID player,
      std::string_view item_name,
      std::uint16_t count,
      const DataDocument& namespace_document) const;
  [[nodiscard]] Result give(
      ArvPlayerID player,
      std::string_view item_name,
      std::uint16_t count,
      std::string_view name_space,
      const DataDocument& namespace_document) const;

private:
  [[nodiscard]] std::string_view default_namespace() const;
  [[nodiscard]] bool available() const noexcept;

  const ArvAPI* api_{};
};

} // namespace arv
