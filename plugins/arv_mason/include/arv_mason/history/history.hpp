#pragma once

#include "arv_mason/operations/operation.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace arv_mason {

struct HistoryEntry {
  ArvWorldHandle world{};
  std::vector<BlockChange> changes{};
  std::string summary{};
};

class MasonHistory {
public:
  void set_limit(std::size_t limit);
  void push(HistoryEntry entry);

  [[nodiscard]] bool can_undo() const noexcept;
  [[nodiscard]] bool can_redo() const noexcept;
  [[nodiscard]] const HistoryEntry* peek_undo() const noexcept;
  [[nodiscard]] const HistoryEntry* peek_redo() const noexcept;

  [[nodiscard]] std::optional<HistoryEntry> take_undo();
  [[nodiscard]] std::optional<HistoryEntry> take_redo();
  void push_redo(HistoryEntry entry);
  void push_undo_without_clearing_redo(HistoryEntry entry);
  void clear_redo();

  [[nodiscard]] std::size_t undo_size() const noexcept;
  [[nodiscard]] std::size_t redo_size() const noexcept;

private:
  std::size_t limit_{15};
  std::vector<HistoryEntry> undo_{};
  std::vector<HistoryEntry> redo_{};
};

[[nodiscard]] HistoryEntry inverse_history_entry(const HistoryEntry& entry, std::string summary);

} // namespace arv_mason
