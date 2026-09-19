// Lemon 编辑器 — 内置文件选择器（M4-Editor-Plan §3.8 场景 IO 配套）
// 不用 OS 原生对话框（SDL dialog 异步回调 + 平台差异；编辑器内实现可无头冒烟，
// 新建项目向导 M4.5 复用）。用法：Open() 后每帧 Draw()，返回 PickerResult。
#pragma once

#include <filesystem>
#include <string>
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
    /// 每帧调用；弹窗打开期间返回 None 以外的动作恰好一次
    PickerResult Draw();
    bool IsOpen() const { return open_; }

private:
    void Refresh();

    bool open_ = false;
    bool firstFrame_ = true;
    std::string title_;
    std::filesystem::path dir_;
    std::string fileName_;
    std::string requireExt_;
    struct Entry {
        std::string name;
        bool isDir;
    };
    std::vector<Entry> entries_;
    std::filesystem::path selected_; // 高亮项（目录或文件）
};

} // namespace lemon::editor
