// Lemon 编辑器 — 新建项目向导（M4-Editor-Plan §5 M4.5；06 §1 布局 + §7 模板）
// blank 模板：目录骨架 + project.lemon + Game/ 脚本工程（HintPath 引 SDK）+
// 种子资产 spawn.png（固定 guid，SpawnerBehaviour 引用）+ Scenes/Main.scene。
// 纯文件系统 + SceneArchive（零 GPU/ImGui——lemon-editor-core 可单测）。
// 06 §1 模型：引擎独立安装、项目引用（升级永不触碰用户目录）；SDK 路径在创建期
// 以绝对 HintPath 固化（M5 随安装器改相对/环境变量锚点时再迁）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lemon::editor {

struct ProjectDesc {
    std::string parentDir;       // 绝对父目录（项目落 <parent>/<name>/）
    std::string name;            // 项目名（= 目录名 = csproj 程序集名）
    std::string sdkDir;          // Lemon.SDK.dll 所在目录（绝对；HintPath）
    std::string engineVersion;   // 写入 project.lemon 的引擎版本锚点
};

class ProjectWizard {
public:
    /// 创建 blank 模板项目。成功返回项目根绝对路径；失败（目录已存在/不可写）返回空。
    /// outSpawnGuid 非空时回写种子资产 guid（调用方装配脚本用）。
    static std::string Create(const ProjectDesc& d, uint64_t* outSpawnGuid = nullptr);

    /// Game/ 脚本工程内容（csproj + GameMain + 示例脚本；独立出来供测试断言）
    static bool WriteGameProject(const ProjectDesc& d, uint64_t spawnGuid);

    /// dotnet build 包装（热重载/向导共用）：编译 <csproj> → 输出目录 outDir。
    /// 返回 0 = 成功；非 0 = 退出码（-1 = 启动 dotnet 失败）。
    /// outOutput 非空时回捕合并 stdout/stderr（M4.6 §5-6 编译错误解析原料）。
    static int BuildGameProject(const std::string& csprojAbs, const std::string& outDir,
                                double* outSeconds = nullptr,
                                std::string* outOutput = nullptr);

    /// 从 dotnet build 输出提取错误行（M4.6 §5-6；纯函数可单测）：
    /// `path/file.cs(l,c): error CSxxxx: message [csproj]` → 去 csproj 尾巴，原行返回。
    static std::vector<std::string> ExtractCompileErrors(const std::string& dotnetOutput);

    /// 新建行为脚本（M4.6 §5-4）：模板 .cs 落 gameDir/<类名>.cs（已存在 = false），
    /// 并在 GameMain.cs 注册锚点前插 Register 行（锚点缺失 = 告警手注册，文件仍建）。
    static bool AddBehaviourScript(const std::string& gameDirAbs, const std::string& className);
};

} // namespace lemon::editor
