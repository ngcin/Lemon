// Lemon 编辑器 — EditorApp 脚本编译与热重载管线（编译排队/错误红字/csproj
// 发现/宿主装配/源码变更检测/编译+换装/手动触发；M4.md §3.7 + §5-5/§5-6 起）。
// 批④-2 2026-09-30 机械外迁：八函数自 EditorAppScripts.cpp（成员函数跨 TU
// 定义，类定义零改动，代码逐行原样——纯函数搬移，无状态无全局，调用点零变化）。
#include "App/EditorApp.h"

#include <filesystem>
#include <vector>

#include "Assets/AssetDatabase.h"
#include "Assets/ProjectWizard.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Scripting/ScriptHost.h"

namespace lemon::editor {

void EditorApp::QueueScriptRebuild(const char* reason) {
    // §5-5：排队后本帧 BuildUI 画"编译中…" → 下帧主循环才真构建（dotnet 阻塞 1–2s
    // 期间屏幕上留着提示；主线程阻塞现状不动 = §6 观察项）
    if (compileQueued_) return; // 已排队（合并）
    compileQueued_ = true;
    compileQueuedReason_ = reason;
}

void EditorApp::LogCompileErrors(const std::string& dotnetOutput) {
    // §5-6：dotnet 输出 → `file(l,c): error CSxxxx: msg` 红字进 Console（可读性：
    // 绝对路径裁成项目相对）
    const std::vector<std::string> errs = ProjectWizard::ExtractCompileErrors(dotnetOutput);
    if (errs.empty()) {
        std::string snippet = dotnetOutput.substr(0, 400);
        LEMON_ERROR("编译失败（dotnet 输出无 error 行；输出片段）：%s", snippet.c_str());
        return;
    }
    const std::string prefix = ctx_.Assets().ProjectRoot() + "/";
    for (const std::string& e : errs) {
        std::string line = e;
        if (line.rfind(prefix, 0) == 0) line = line.substr(prefix.size());
        LEMON_ERROR("编译错误：%s", line.c_str());
    }
    if (errs.size() >= 50) LEMON_WARN("编译错误超 50 条，仅列前 50");
}

bool EditorApp::FindGameProject(std::string& csproj, std::string& dll) {
    namespace fs = std::filesystem;
    const std::string& root = ctx_.Assets().ProjectRoot();
    if (root.empty()) return false;
    std::error_code ec;
    for (auto it = fs::directory_iterator(root + "/Game", ec);
         it != fs::directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".csproj") continue;
        csproj = it->path().string();
        dll = root + "/.lemon/bin/" +
              it->path().stem().string() + ".dll";
        return true;
    }
    return false;
}

bool EditorApp::InitScriptHostFrom(const std::string& dllAbs) {
#ifdef LEMON_SCRIPT_DIR
    namespace fs = std::filesystem;
    if (!fs::exists(dllAbs)) {
        LEMON_WARN("脚本装配失败：程序集不存在 %s", dllAbs.c_str());
        return false;
    }
    // 不变量先行：ctx 指针先清，宿主怎么动都不悬空（Profiler 每帧经 ctx.Scripts()
    // 调 GcAllocated——悬空 = SIGSEGV，M4.6 实测）
    ctx_.SetScriptHost(nullptr);
    // 已有宿主（会话内切项目/二次装配）：CoreCLR 进程单例，二次 Initialize 必失败
    // （script-tests 探针钉板 second-host init=0）——复用宿主走 A 线换装装配新项目
    // 程序集。原实现 make_unique 先毁旧宿主 → ctx 悬空 + 二次初始化失败 = 闪退双因
    if (host_) {
        const auto info = host_->HotReloadAssembly(dllAbs.c_str());
        if (info.ok) {
            ctx_.SetScriptHost(host_.get());
            LEMON_LOG("脚本域换装至：%s（复用宿主）", dllAbs.c_str());
            return true;
        }
        LEMON_WARN("脚本域换装失败（%s）——转无脚本状态（修错后可再装配）", dllAbs.c_str());
        return false;
    }
    host_ = std::make_unique<scripting::ScriptHost>();
    // DomainManager 要求绝对路径（ALC LoadFromAssemblyPath 约束）
    std::error_code eca;
    std::string scriptAbs = std::filesystem::absolute(dllAbs, eca).generic_string();
    if (host_->Initialize(nullptr, LEMON_SCRIPT_DIR "/Lemon.Entry.runtimeconfig.json",
                          LEMON_SCRIPT_DIR "/Lemon.Entry.dll") &&
        host_->LoadUserAssembly(scriptAbs.c_str())) {
        ctx_.SetScriptHost(host_.get());
        LEMON_LOG("脚本宿主就绪：%s（类型 %zu 个）", scriptAbs.c_str(),
                  ctx_.ScriptTypeNames().size());
        return true;
    }
    LEMON_WARN("脚本宿主初始化失败（%s）——无脚本继续", scriptAbs.c_str());
    host_.reset();
#endif
    return false;
}

bool EditorApp::ScriptSourceChanged() {
    // FileWatcher 只报"有变化"；这里过滤出真正需要重编译的源写（.cs/.csproj，
    // 排除 obj/bin 生成物——dotnet build 会改写它们，否则自我触发死循环）
    namespace fs = std::filesystem;
    const std::string gameDir = ctx_.Assets().ProjectRoot() + "/Game";
    std::error_code ec;
    int64_t newest = 0;
    for (auto it = fs::recursive_directory_iterator(gameDir,
                                                    fs::directory_options::skip_permission_denied,
                                                    ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const fs::directory_entry& de = *it;
        const std::string name = de.path().filename().string();
        if (de.is_directory(ec)) {
            if (name == "obj" || name == "bin" || (!name.empty() && name[0] == '.'))
                it.disable_recursion_pending();
            continue;
        }
        if (name.size() < 3) continue;
        const std::string ext = de.path().extension().string();
        if (ext != ".cs" && ext != ".csproj") continue;
        auto wt = fs::last_write_time(de.path(), ec);
        if (ec) continue;
        const int64_t s = (int64_t)wt.time_since_epoch().count();
        if (s > newest) newest = s;
    }
    if (newest == 0 || newest <= lastHandledCsWrite_) return false;
    lastHandledCsWrite_ = newest;
    return true;
}

bool EditorApp::TryHotReloadScripts(const char* reason) {
    if (!host_) {
        // 无宿主 + 有 Game/ 工程 = 启动期编译失败后的恢复路径（2026-09-22 与 Play
        // 阻断配套）：此前直接跳过 = 修错保存后必须重启编辑器。这里试首装——
        // 修错 → watcher → 编译队列 → 本函数 → InitScriptHostFrom（无宿主即首装
        // 路径），成功后 Play 阻断自动解除。
        std::string csproj, dll;
        if (!FindGameProject(csproj, dll)) {
            LEMON_WARN("热重载跳过：无脚本宿主（%s）", reason);
            return false;
        }
        std::string buildOut;
        if (ProjectWizard::BuildGameProject(csproj, ctx_.Assets().ProjectRoot() + "/.lemon/bin",
                                            nullptr, &buildOut) != 0) {
            LEMON_ERROR("脚本首装编译失败（Play 仍被阻断）：dotnet build（%s）", reason);
            LogCompileErrors(buildOut);
            return false;
        }
        if (InitScriptHostFrom(dll)) {
            LEMON_LOG("脚本宿主已装配（%s）——Play 可用", reason);
            return true;
        }
        return false;
    }
    std::string csproj, dll;
    const bool hasProject = FindGameProject(csproj, dll);
    // 无 Game/ 工程（--script 直载 dll 形态）：dll 可能已被外部重编——直接换装同一文件
    if (!hasProject) {
        dll = launch_->script;
        if (dll.empty()) return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    if (hasProject) {
        std::string buildOut; // M4.6 §5-6：捕获输出 → 错误行红字进 Console
        const int rc = ProjectWizard::BuildGameProject(
            csproj, ctx_.Assets().ProjectRoot() + "/.lemon/bin", nullptr, &buildOut);
        if (rc != 0) {
            LEMON_ERROR("热重载编译失败（保持旧域运行）：dotnet build 退出码 %d（%s）", rc,
                        reason);
            LogCompileErrors(buildOut);
            return false;
        }
    }
    const auto info = host_->HotReloadAssembly(dll.c_str());
    const int reattached = ctx_.RefreshScriptsAfterReload();
    hotReloadMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                       .count();
    if (!info.ok) {
        LEMON_ERROR("热重载换装失败：新域装载异常（旧域已弃，脚本停摆——修错后再触发）");
        return false;
    }
    LEMON_LOG("热重载完成（%s）：编译+换装+重装配 %.0fms（Play 重装配 %d 实例；类型 %zu 个）",
              reason, hotReloadMs_, reattached, ctx_.ScriptTypeNames().size());
    lastBuildMs_ = hotReloadMs_; // 状态栏"上次编译"回显（M4.6 §5-5）
    if (info.leakCount > 0)
        LEMON_WARN("热重载泄漏计数 %d（旧 ALC 未回收——本 runtime 已知限制，ADR-010 A 线；"
                   "每次约百 KB 级，会话内可接受）",
                   info.leakCount);
    return true;
}

void EditorApp::MenuRebuildScripts() { QueueScriptRebuild("手动触发"); }

int EditorApp::HotReloadCount() const {
    return host_ ? host_->HotReloadCount() : 0;
}

} // namespace lemon::editor
