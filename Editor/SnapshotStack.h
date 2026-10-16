// Lemon 编辑器 — 面板本地快照撤销栈（M8 批⑩b，ADR-013 D6 精简图 v1）
// 语义：变更前压快照（Push），容量封顶丢最旧；新变更清空重做分支；Undo/Redo
// 交换当前态与栈顶。典型调用 = 面板在任一数据变更落地前 Push(edit_)，拖动类
// 交互动按"松手拍一次"（会话起拍快照，松手时若有实际变更才 Push）。
// 与场景结构轨 Undo（EditorContext::Undo）刻意不通——登记后续统一（批⑩ §7）。
// 无 ImGui/无引擎依赖：editor-core 头，单测直测（tests/engine/EditorTests.cpp）。
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace lemon::editor {

template <typename T>
class SnapshotStack {
public:
    explicit SnapshotStack(size_t capacity = 50) : cap_(capacity == 0 ? 1 : capacity) {}

    /// 变更前压入快照；重做分支随之清空（新分支分叉）
    void Push(const T& snapshot) {
        if (undo_.size() >= cap_) undo_.erase(undo_.begin());
        undo_.push_back(snapshot);
        redo_.clear();
    }

    /// 撤销：当前态存入重做分支，上一快照经 out 返回；栈空 = false（当前态不动）
    bool Undo(const T& current, T& out) {
        if (undo_.empty()) return false;
        out = std::move(undo_.back());
        undo_.pop_back();
        if (redo_.size() >= cap_) redo_.erase(redo_.begin());
        redo_.push_back(current);
        return true;
    }

    /// 重做：镜像 Undo（对称容量封顶）
    bool Redo(const T& current, T& out) {
        if (redo_.empty()) return false;
        out = std::move(redo_.back());
        redo_.pop_back();
        if (undo_.size() >= cap_) undo_.erase(undo_.begin());
        undo_.push_back(current);
        return true;
    }

    void Clear() {
        undo_.clear();
        redo_.clear();
    }
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    size_t UndoDepth() const { return undo_.size(); }
    size_t RedoDepth() const { return redo_.size(); }

private:
    size_t cap_;
    std::vector<T> undo_;
    std::vector<T> redo_;
};

} // namespace lemon::editor
