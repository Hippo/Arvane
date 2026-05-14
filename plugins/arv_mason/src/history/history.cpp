#include "arv_mason/history/history.hpp"

#include <algorithm>

namespace arv_mason {

void MasonHistory::set_limit(std::size_t limit) {
  limit_ = std::max<std::size_t>(1U, limit);
  while (undo_.size() > limit_) {
    undo_.erase(undo_.begin());
  }
  while (redo_.size() > limit_) {
    redo_.erase(redo_.begin());
  }
}

void MasonHistory::push(HistoryEntry entry) {
  undo_.push_back(std::move(entry));
  clear_redo();
  while (undo_.size() > limit_) {
    undo_.erase(undo_.begin());
  }
}

bool MasonHistory::can_undo() const noexcept {
  return !undo_.empty();
}

bool MasonHistory::can_redo() const noexcept {
  return !redo_.empty();
}

const HistoryEntry* MasonHistory::peek_undo() const noexcept {
  return undo_.empty() ? nullptr : &undo_.back();
}

const HistoryEntry* MasonHistory::peek_redo() const noexcept {
  return redo_.empty() ? nullptr : &redo_.back();
}

std::optional<HistoryEntry> MasonHistory::take_undo() {
  if (undo_.empty()) {
    return std::nullopt;
  }
  HistoryEntry entry = std::move(undo_.back());
  undo_.pop_back();
  return entry;
}

std::optional<HistoryEntry> MasonHistory::take_redo() {
  if (redo_.empty()) {
    return std::nullopt;
  }
  HistoryEntry entry = std::move(redo_.back());
  redo_.pop_back();
  return entry;
}

void MasonHistory::push_redo(HistoryEntry entry) {
  redo_.push_back(std::move(entry));
  while (redo_.size() > limit_) {
    redo_.erase(redo_.begin());
  }
}

void MasonHistory::push_undo_without_clearing_redo(HistoryEntry entry) {
  undo_.push_back(std::move(entry));
  while (undo_.size() > limit_) {
    undo_.erase(undo_.begin());
  }
}

void MasonHistory::clear_redo() {
  redo_.clear();
}

std::size_t MasonHistory::undo_size() const noexcept {
  return undo_.size();
}

std::size_t MasonHistory::redo_size() const noexcept {
  return redo_.size();
}

HistoryEntry inverse_history_entry(const HistoryEntry& entry, std::string summary) {
  HistoryEntry inverse;
  inverse.world = entry.world;
  inverse.summary = std::move(summary);
  inverse.changes.reserve(entry.changes.size());
  for (const BlockChange& change : entry.changes) {
    inverse.changes.push_back({change.pos, change.after, change.before});
  }
  return inverse;
}

} // namespace arv_mason
