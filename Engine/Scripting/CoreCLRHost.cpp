// Lemon 引擎 — CoreCLR 宿主实现（hostfxr 细节全部收敛于此文件）
// 形态以 M0 spike-03 本机实测为准（docs/Reports/2026-09-18-m0-go-no-go.md 教训 5/6）；
// 04 文档 §1 的 Luma CoreCLRHost 仅作对照（ADR-010：不整体移植，避免未实测面）。
#include "Scripting/CoreCLRHost.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <coreclr_delegates.h>
#include <hostfxr.h>

#include "Core/Log.h"

namespace lemon::scripting {
namespace {

struct HostfxrApi {
    void* lib = nullptr;
    hostfxr_initialize_for_runtime_config_fn initForConfig = nullptr;
    hostfxr_initialize_for_dotnet_command_line_fn initCmdLine = nullptr;
    hostfxr_get_runtime_delegate_fn getDelegate = nullptr;
    hostfxr_close_fn close = nullptr; // 部分 hostfxr 不导出（本机实测）——可选
};

void* OpenLibrary(const char* path) {
#if defined(_WIN32)
    return (void*)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}

void* Sym(void* lib, const char* name) {
#if defined(_WIN32)
    return (void*)GetProcAddress((HMODULE)lib, name);
#else
    return dlsym(lib, name);
#endif
}

/// 版本目录按数字段比较："10.0.401" > "8.0.30"（字典序会错——spike 实测坑）
std::vector<long long> VersionKey(const std::string& v) {
    std::vector<long long> parts;
    size_t pos = 0;
    while (pos != std::string::npos) {
        size_t dot = v.find('.', pos);
        parts.push_back(std::atoll(v.substr(pos, dot - pos).c_str()));
        pos = dot == std::string::npos ? dot : dot + 1;
    }
    return parts;
}

// fxr 根候选链：显式参 → $LEMON_DOTNET_ROOT（引擎专用覆写）→ $DOTNET_ROOT（dotnet
// 官方变量；actions/setup-dotnet 等 CI 装配即设此——runner 的 dotnet 装在临时目录，
// 只认 brew 默认路径会静默哑火，CI 首跑实证 2026-09-30）→ /usr/local/share/dotnet。
// 显式根两形态皆落空时继续走链（M7a 批⑤：dev 形态 exe 旁 runtime/ 只有托管件，
// GameEntry 恒传该目录——回退到安装机 dotnet 的既有语义不变）。
std::vector<std::filesystem::path> DotnetRootCandidates(const char* dotnetRoot) {
    std::vector<std::filesystem::path> roots;
    if (dotnetRoot && *dotnetRoot) roots.emplace_back(dotnetRoot);
    for (const char* var : {"LEMON_DOTNET_ROOT", "DOTNET_ROOT"})
        if (const char* v = getenv(var); v && *v)
            roots.emplace_back(v);
    roots.emplace_back("/usr/local/share/dotnet");
    return roots;
}

bool BindHostfxr(HostfxrApi& api, const std::filesystem::path& libPath) {
    api.lib = OpenLibrary(libPath.string().c_str());
    if (!api.lib) {
        LEMON_WARN("open %s failed", libPath.string().c_str());
        return false;
    }
    api.initForConfig = (hostfxr_initialize_for_runtime_config_fn)Sym(api.lib, "hostfxr_initialize_for_runtime_config");
    api.initCmdLine = (hostfxr_initialize_for_dotnet_command_line_fn)Sym(api.lib, "hostfxr_initialize_for_dotnet_command_line");
    api.getDelegate = (hostfxr_get_runtime_delegate_fn)Sym(api.lib, "hostfxr_get_runtime_delegate");
    api.close = (hostfxr_close_fn)Sym(api.lib, "hostfxr_close_handle"); // 可选符号
    if (!api.initForConfig || !api.getDelegate) {
        LEMON_WARN("hostfxr %s missing required exports", libPath.string().c_str());
        return false;
    }
    // 错误回调：把宿主侧错误引到引擎日志（可选导出）
    if (auto setWriter = (void (*)(void (*)(const char*)))Sym(api.lib, "hostfxr_set_error_writer"))
        setWriter(+[](const char* message) { LEMON_WARN("hostfxr: %s", message); });
    LEMON_LOG("hostfxr: %s (close=%s)", libPath.string().c_str(), api.close ? "yes" : "no");
    return true;
}

bool LoadHostfxr(HostfxrApi& api, const char* dotnetRoot) {
    for (const std::filesystem::path& root : DotnetRootCandidates(dotnetRoot)) {
        // 形态一（M7a 批⑤ 包形态）：self-contained 平铺——dotnet publish -r
        // --self-contained 产物根直接持有 libhostfxr（无 host/fxr/<ver> 多版本层）
        std::error_code ec;
        std::filesystem::path flat = root / (std::string("libhostfxr") +
#if defined(_WIN32)
                                             ".dll"
#else
                                             ".dylib"
#endif
        );
        if (std::filesystem::is_regular_file(flat, ec) && BindHostfxr(api, flat)) return true;
        // 形态二（安装机/brew）：host/fxr/<ver> 多版本布局，取最高版
        std::filesystem::path fxrDir = root / "host" / "fxr";
        if (!std::filesystem::exists(fxrDir)) continue;
        std::string best;
        std::vector<long long> bestKey;
        for (auto& e : std::filesystem::directory_iterator(fxrDir)) {
            if (!e.is_directory()) continue;
            std::string name = e.path().filename().string();
            auto key = VersionKey(name);
            if (best.empty() || key > bestKey) {
                best = name;
                bestKey = key;
            }
        }
        if (best.empty()) continue;
#if defined(_WIN32)
        auto libPath = fxrDir / best / "hostfxr.dll";
#else
        auto libPath = fxrDir / best / "libhostfxr.dylib";
#endif
        if (BindHostfxr(api, libPath)) return true;
    }
    LEMON_WARN("no usable dotnet fxr root（brew install dotnet-sdk / set LEMON_DOTNET_ROOT"
               " 或 DOTNET_ROOT / 包形态检查 runtime/ 自含产物）");
    return false;
}

} // namespace

struct CoreCLRHost::Fxr {
    HostfxrApi api;
};

bool CoreCLRHost::Load(const char* dotnetRoot, const char* runtimeConfigPath,
                       const char* entryAssemblyPath) {
    if (IsLoaded()) return true;
    // 失败路径清理（review 2026-10-02 #53）：原实现每次进来无条件 new Fxr——
    // 失败后重试泄漏旧 Fxr（含已 dlopen 的 hostfxr 句柄）；Fxr 复用，重试只重走
    // 解析。注意 hostfxr_set_error_writer 是线程局部注册（LoadHostfxr 内，vendored
    // 头注记）——只覆盖装载线程的错误，其他线程的 hostfxr 错误走默认 stderr
    if (!fxr_) fxr_ = new Fxr{};
    if (!LoadHostfxr(fxr_->api, dotnetRoot)) return false;

    hostfxr_handle ctx = nullptr;
    int rc = fxr_->api.initForConfig(runtimeConfigPath, nullptr, &ctx);
    if (rc != 0 || ctx == nullptr) {
        // 自含组件回退（M7a 批⑤ 包形态实测坑）：initialize_for_runtime_config
        // 明确拒 SC 形态 runtimeconfig（includedFrameworks，rc=0x80008093）——改走
        // apphost 同款 command_line 初始化（argv[0] = 入口程序集路径；hostfxr 自
        // 取其旁 runtimeconfig，runtime/hostpolicy 按 app base = 程序集目录解析平
        // 铺布局）。dev 形态（framework 引用）不受影响——首试路径命中即短路。
        if (fxr_->api.initCmdLine && entryAssemblyPath && *entryAssemblyPath) {
            const char_t* argv[1] = { entryAssemblyPath };
            rc = fxr_->api.initCmdLine(1, argv, nullptr, &ctx);
        }
        if (rc != 0 || ctx == nullptr) {
            LEMON_WARN("hostfxr_initialize_for_runtime_config(%s) failed rc=%d",
                       runtimeConfigPath, rc);
            return false;
        }
        LEMON_LOG("hostfxr：自含形态走 command_line 初始化（apphost 语义）");
    }
    rc = fxr_->api.getDelegate(ctx, hdt_load_assembly_and_get_function_pointer, &loadAssembly_);
    if (rc != 0 || loadAssembly_ == nullptr) {
        LEMON_WARN("get_runtime_delegate(load_assembly) failed rc=%d", rc);
        if (fxr_->api.close) fxr_->api.close(ctx); // #53：失败同关（原仅成功路径关）
        return false;
    }
    if (fxr_->api.close) fxr_->api.close(ctx); // 句柄进程级存活；缺 close 不影响

    entryAssemblyPath_ = entryAssemblyPath;
    LEMON_LOG("CoreCLR up (entry: %s)", entryAssemblyPath);
    return true;
}

void* CoreCLRHost::GetExport(const char* typeNameWithAsm, const char* methodName) const {
    if (!IsLoaded()) return nullptr;
    void* fn = nullptr;
    int rc = ((load_assembly_and_get_function_pointer_fn)loadAssembly_)(
        entryAssemblyPath_.c_str(), typeNameWithAsm, methodName, UNMANAGEDCALLERSONLY_METHOD,
        nullptr, &fn);
    if (rc != 0 || fn == nullptr) {
        LEMON_WARN("load export %s.%s failed hr=0x%08x", typeNameWithAsm, methodName,
                   (unsigned)rc);
        return nullptr;
    }
    return fn;
}

} // namespace lemon::scripting
