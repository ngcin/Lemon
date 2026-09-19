#include "Tooling/UndoStack.h"

namespace lemon::editor {

void UndoStack::Push(Record r) {
    undo_.push_back(std::move(r));
    if (undo_.size() > kLimit) undo_.pop_front(); // FIFO 淘汰（§3.5）
    redo_.clear(); // 新编辑分叉重做链（Unity 心智）
}

bool UndoStack::Undo() {
    if (undo_.empty()) return false;
    Record r = std::move(undo_.back());
    undo_.pop_back();
    r.apply(/*undo=*/true);
    redo_.push_back(std::move(r));
    return true;
}

bool UndoStack::Redo() {
    if (redo_.empty()) return false;
    Record r = std::move(redo_.back());
    redo_.pop_back();
    r.apply(/*undo=*/false);
    undo_.push_back(std::move(r));
    return true;
}

void UndoStack::Clear() {
    undo_.clear();
    redo_.clear();
}

} // namespace lemon::editor
