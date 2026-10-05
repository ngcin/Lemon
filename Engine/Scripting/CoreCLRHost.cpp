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

#if defined(_WIN32)
// UTF-8 路径 → LoadLibraryW（批⑦）：LoadLibraryA 按 ACP 解释窄码——非 ASCII
// 安装路径（中文用户名等）下 hostfxr 装载哑火；FileOps::RenameReplace 的 widen
// 同款口径，就地小实现不引依赖
std::wstring Widen(const char* s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 1), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}
#endif

void* OpenLibrary(const char* path) {
#if defined(_WIN32)
    return (void*)LoadLibraryW(Widen(path).c_str());
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
// 只认 brew 默认路径会静默哑火，CI 首跑实证 2026-09-30）→ 平台默认安装位
// （macOS = /usr/local/share/dotnet；Windows = %ProgramFiles%\dotnet——批⑦ 补）。
// 显式根两形态皆落空时继续走链（M7a 批⑤：dev 形态 exe 旁 runtime/ 只有托管件，
// GameEntry 恒传该目录——回退到安装机 dotnet 的既有语义不变）。
std::vector<std::filesystem::path> DotnetRootCandidates(const char* dotnetRoot) {
    std::vector<std::filesystem::path> roots;
    if (dotnetRoot && *dotnetRoot) roots.emplace_back(dotnetRoot);
    for (const char* var : {"LEMON_DOTNET_ROOT", "DOTNET_ROOT"})
        if (const char* v = getenv(var); v && *v)
            roots.emplace_back(v);
#if defined(_WIN32)
    if (const char* pf = getenv("ProgramFiles"); pf && *pf)
        roots.emplace_back(std::filesystem::path(pf) / "dotnet");
    roots.emplace_back("C:/Program Files/dotnet");
#else
    roots.emplace_back("/usr/local/share/dotnet");
#endif
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
    // 错误回调：把宿主侧错误引到引擎日志（可选导出；hostfxr 错误串按 char_t 传——
    // POSIX = char，Windows = wchar_t，两侧各自分支，批⑦ char_t 分叉清账）
    if (auto setWriter = (hostfxr_set_error_writer_fn)Sym(api.lib, "hostfxr_set_error_writer")) {
#if defined(_WIN32)
        setWriter(+[](const char_t* message) {
            if (!message) return;
            const int n = WideCharToMultiByte(CP_UTF8, 0, message, -1, nullptr, 0, nullptr, nullptr);
            std::string u8((size_t)(n > 0 ? n : 1), '\0');
            if (n > 0) WideCharToMultiByte(CP_UTF8, 0, message, -1, u8.data(), n, nullptr, nullptr);
            LEMON_WARN("hostfxr: %s", u8.c_str());
        });
#else
        setWriter(+[](const char_t* message) {
            if (message) LEMON_WARN("hostfxr: %s", message);
        });
#endif
    }
    LEMON_LOG("hostfxr: %s (close=%s)", libPath.string().c_str(), api.close ? "yes" : "no");
    return true;
}

bool LoadHostfxr(HostfxrApi& api, const char* dotnetRoot) {
    for (const std::filesystem::path& root : DotnetRootCandidates(dotnetRoot)) {
        // 形态一（M7a 批⑤ 包形态）：self-contained 平铺——dotnet publish -r
        // --self-contained 产物根直接持有 hostfxr（无 host/fxr/<ver> 多版本层）。
        // 文件名平台各异：POSIX = libhostfxr.dylib，Windows = hostfxr.dll（无 lib
        // 前缀——批⑦实锤：按 libhostfxr.dll 探测永远落空，链尾被 DOTNET_ROOT 短路
        // 后以 framework-dependent 语义找 hostpolicy 必败，VM 包体 smoke 实抓）
        std::error_code ec;
        std::filesystem::path flat = root /
#if defined(_WIN32)
                                     "hostfxr.dll"
#else
                                     "libhostfxr.dylib"
#endif
        ;
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
    // hostfxr API 的字符串参数全按 char_t 走（review 实锤⑤：Windows = wchar_t）。
    // 宽化一律走 Widen（CP_UTF8）——fs::path(std::string) 在 win 按 ACP 解释窄串，
    // UTF-8 中文路径会被转错（review 二轮自查实抓）
#if defined(_WIN32)
    const std::wstring wCfg = Widen(runtimeConfigPath ? runtimeConfigPath : "");
    int rc = fxr_->api.initForConfig(wCfg.c_str(), nullptr, &ctx);
#else
    int rc = fxr_->api.initForConfig(runtimeConfigPath, nullptr, &ctx);
#endif
    if (rc != 0 || ctx == nullptr) {
        // 自含组件回退（M7a 批⑤ 包形态实测坑）：initialize_for_runtime_config
        // 明确拒 SC 形态 runtimeconfig（includedFrameworks，rc=0x80008093）——改走
        // apphost 同款 command_line 初始化（argv[0] = 入口程序集路径；hostfxr 自
        // 取其旁 runtimeconfig，runtime/hostpolicy 按 app base = 程序集目录解析平
        // 铺布局）。dev 形态（framework 引用）不受影响——首试路径命中即短路。
        if (fxr_->api.initCmdLine && entryAssemblyPath && *entryAssemblyPath) {
            // char_t 分叉（批⑦）：Windows = wchar_t——UTF-8 入参经 Widen 宽化
            // （CP_UTF8，不用 fs::path——win 侧窄串按 ACP 解释）；POSIX = char 直传。
            // argv 生命周期只在 initCmdLine 调用内 → 局部串安全
#if defined(_WIN32)
            const std::wstring wEntry = Widen(entryAssemblyPath);
            const char_t* argv[1] = { wEntry.c_str() };
#else
            const char_t* argv[1] = { entryAssemblyPath };
#endif
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
    // load_assembly 三字符串参也是 char_t（review 实锤⑤同源）：类型/方法名是 ASCII
    // 标识符但参数类型不豁免——win 侧统一经 Widen（CP_UTF8 对 ASCII 恒等）
#if defined(_WIN32)
    const std::wstring wAsm = Widen(entryAssemblyPath_.c_str());
    const std::wstring wType = Widen(typeNameWithAsm);
    const std::wstring wMethod = Widen(methodName);
    int rc = ((load_assembly_and_get_function_pointer_fn)loadAssembly_)(
        wAsm.c_str(), wType.c_str(), wMethod.c_str(), UNMANAGEDCALLERSONLY_METHOD,
        nullptr, &fn);
#else
    int rc = ((load_assembly_and_get_function_pointer_fn)loadAssembly_)(
        entryAssemblyPath_.c_str(), typeNameWithAsm, methodName, UNMANAGEDCALLERSONLY_METHOD,
        nullptr, &fn);
#endif
    if (rc != 0 || fn == nullptr) {
        LEMON_WARN("load export %s.%s failed hr=0x%08x", typeNameWithAsm, methodName,
                   (unsigned)rc);
        return nullptr;
    }
    return fn;
}

} // namespace lemon::scripting
