// Lemon 编辑器 — 内置文件选择器（M4-Editor-Plan §3.8 场景 IO 配套）
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
    /// 每帧调用；弹窗打开期间返回 None 以外的动作恰好一次
    PickerResult Draw();
    bool IsOpen() const { return open_; }

    /// 快捷目录钮（M4.6 §5-7）：(标签, 绝对路径)，如 Home / 当前项目根。打开前设置。
    void SetQuickDirs(std::vector<std::pair<std::string, std::string>> dirs) {
        quickDirs_ = std::move(dirs);
    }

private:
    void Refresh();

    bool open_ = false;
    bool firstFrame_ = true;
    bool opening_ = false;  // Open 后首帧 OpenPopup（模态化：打开期间主 UI 不可点）
    bool dirMode_ = false;  // 目录选择模式
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
