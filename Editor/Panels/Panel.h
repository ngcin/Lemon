// Lemon 编辑器 — 面板框架（M4-Editor-Plan §2.2/§3.1）
// IEditorPanel + 编译期注册表（反射注册降级为编译期版，对应 08 §4 砍单 #7）。
// 面板间不互相 include：共享状态一律经 EditorApp/EditorContext（§3.3）。
// 面板开合状态持久化随 ImGui ini（窗口折叠即不绘制，布局文件同时记录开关）。
#pragma once

#include <cstring>
#include <memory>
#include <vector>

namespace lemon::editor {

class EditorApp; // 前置：面板经 App 取共享状态（EditorContext 在 M4.1 成型前以此过渡）

class IEditorPanel {
public:
    virtual ~IEditorPanel() = default;
    /// 窗口标题（兼作 ImGui 窗口名 = DockBuilder 定位键；改名 = 布局失配，须谨慎）
    virtual const char* Name() const = 0;
    /// 每帧绘制（ImGui::Begin/End 由面板自理，便于各自控制窗口标志）
    virtual void OnGui(EditorApp& app) = 0;
    /// 默认布局中的开关（Window 菜单可切换；有 ini 时以 ini 恢复为准）
    virtual bool OpenByDefault() const { return true; }
};

class PanelRegistry {
public:
    struct Entry {
        IEditorPanel* panel;
        bool open; // 运行时开合（Window 菜单驱动）
    };

    void Add(IEditorPanel* p) { entries_.push_back({p, p->OpenByDefault()}); }
    Entry* FindEntry(const char* name) {
        for (auto& e : entries_)
            if (std::strcmp(e.panel->Name(), name) == 0) return &e;
        return nullptr;
    }
    std::vector<Entry>& Entries() { return entries_; }

private:
    std::vector<Entry> entries_; // 面板为长寿命对象（EditorApp 持有），此处只存指针
};

/// 创建全部面板并入注册表（编译期注册表唯一登记点；新增面板在此追加）。
/// M4 面板集冻结为核心 7（决议 #2）：Hierarchy/Inspector/Scene/Game/Assets/Console/Profiler。
std::vector<std::unique_ptr<IEditorPanel>> CreateAllPanels();

} // namespace lemon::editor
