// Lemon 编辑器 — Undo 双轨栈（M4-Editor-Plan §3.5；ADR-009 对照 Prowl Undo.cs）
// 属性轨：组件字节快照（guid 定位 + POD memcpy 还原——组件 trivially copyable 前提）。
//   记录粒度 = 组件级（一次拖拽/一次控件交互 = 一条，IsItemActivated/Deactivated 合并）；
//   字段级拆分列 M5 精化（组件级是其状态超集，语义无损）。
// 结构轨：整场景 JSON 快照（Create/Destroy/SetParent/Add/Remove Component）。
//   走 SceneArchive 单一成熟通路，零特判；粒度 = 场景级（结构操作离散，无合并需求）。
// 上限 100（FIFO 淘汰）；Play 中禁用（§2.4，由 EditorContext 清栈把关）。
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace lemon::editor {

class EditorContext;

class UndoStack {
public:
    static constexpr size_t kLimit = 100;

    /// 一条记录：apply(true) = 撤销，apply(false) = 重做。闭包只捕轻量数据
    /// （guid/字节快照/JSON 串），不捕场景对象。
    struct Record {
        const char* name = "";
        std::function<void(bool undo)> apply;
    };

    void Push(Record r);
    bool Undo();                 // 栈空返回 false
    bool Redo();
    void Clear();
    bool Empty() const { return undo_.empty(); }
    size_t Size() const { return undo_.size(); }
    const std::deque<Record>& Records() const { return undo_; }

private:
    std::deque<Record> undo_, redo_;
};

} // namespace lemon::editor
