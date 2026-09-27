// Lemon 编辑器 — 内置文件选择器（M4.md §3.8 场景 IO 配套）
// 不用 OS 原生对话框（SDL dialog 异步回调 + 平台差异；编辑器内实现可无头冒烟，
// 新建项目向导 M4.5 复用）。用法：Open() 后每帧 Draw()，返回 PickerResult。
#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace lemon::editor {

enum class PickerAction { None, Open, Save, Cancel };
struct PickerResult {
    PickerAction action = PickerAction::None;
    std::string path; // 完整路径（目录 + 文件名）
    std::vector<std::string> paths; // 多选模式（action=Open 且非空 = 多选集；path = 首项）
};

class FilePicker {
public:
    /// title 弹窗标题；defaultDir 起始目录；defaultName 保存模式缺省文件名；
    /// requireExt 非空 = 保存时无后缀自动补（如 ".scene"）
    void Open(const char* title, const std::string& defaultDir,
              const std::string& defaultName = "", const char* requireExt = nullptr);
    /// 目录选择模式（M4.6 §4-2）：只列目录 + "选择此目录"返回当前目录（action=Open）；
    /// 向导父目录 / 打开项目起点用
    void OpenDir(const char* title, const std::string& defaultDir);
    /// 多选模式（动画工作台 v3.1 图片多选加帧）：filterExts 非空 = 只列这些扩展名
    /// 的文件（目录恒显示，如 {".png", ".jpg"}）。系统式选择语义（T3-UX4）：单击 =
    /// 单选清余；Ctrl+单击 = 加选/移除；Shift+单击 = 锚点范围连选；Ctrl+A = 全选
    /// 文件；双击 = 确认。默认缩略图视图（可切列表）。确认返回 paths（action=Open，
    /// 按显示序）。与 Open 互斥——Open 重置多选态。
    void OpenMulti(const char* title, const std::string& defaultDir,
                   std::vector<std::string> filterExts);
    /// 每帧调用；弹窗打开期间返回 None 以外的动作恰好一次
    PickerResult Draw();
    bool IsOpen() const { return open_; }

    /// 快捷目录钮（M4.6 §5-7）：(标签, 绝对路径)，如 Home / 当前项目根。打开前设置。
    void SetQuickDirs(std::vector<std::pair<std::string, std::string>> dirs) {
        quickDirs_ = std::move(dirs);
    }

    // ---- T3-UX4 冒烟注入面（--smoke-anim）----
    /// 多选计数（真实 multiSel_ 大小）
    size_t MultiSelCountForTest() const { return multiSel_.size(); }
    /// 直接走真实选择语义（点击处理器同款入口；idx = entries_ 下标）
    void ApplyMultiClickForTest(size_t idx, bool ctrl, bool shift) {
        ApplyMultiClick(idx, ctrl, shift);
    }

private:
    void Refresh();
    void ApplyMultiClick(size_t idx, bool ctrl, bool shift); // 系统式选择语义核心
    /// 确认路径集（按显示序归一——Ctrl 逐张点选的点击序 ≠ 直觉的帧序）
    std::vector<std::filesystem::path> OrderedMultiSel() const;
    void ConfirmMulti(PickerResult& out);       // 双击/打开按钮共用出口
    void NavigateTo(const std::filesystem::path& sub); // 清锚点/选集 + 刷新
    void DrawListEntries(PickerResult& out);    // 列表视图（原路径）
    void DrawIconEntries(PickerResult& out);    // 缩略图网格（T3-UX4）

    bool open_ = false;
    bool firstFrame_ = true;
    bool opening_ = false;  // Open 后首帧 OpenPopup（模态化：打开期间主 UI 不可点）
    bool dirMode_ = false;  // 目录选择模式
    bool multi_ = false;    // 多选模式（T3-UX2）
    enum class View { List, Icons };
    View view_ = View::List; // T3-UX4：列表 / 缩略图网格（多选默认网格）
    std::vector<std::string> filterExts_;          // 扩展名白名单（小写含点；空 = 不过滤）
    std::vector<std::filesystem::path> multiSel_;  // 多选累积集（有序）
    int anchorIdx_ = -1;                           // Shift 范围锚（最近一次单击/Ctrl+A 首文件）
    std::string title_;
    std::filesystem::path dir_;
    std::string fileName_;
    std::string requireExt_;
    std::string pathInput_; // 路径手输框（M4.6 §5-7；随 dir_ 同步，回车直达）
    std::vector<std::pair<std::string, std::string>> quickDirs_;
    struct Entry {
        std::string name;
        bool isDir;
    };
    std::vector<Entry> entries_;
    std::filesystem::path selected_; // 高亮项（目录或文件）
};

} // namespace lemon::editor
