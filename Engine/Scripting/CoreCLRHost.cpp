// Lemon 引擎 — CoreCLR 宿主实现（hostfxr 细节全部收敛于此文件）
// 形态以 M0 spike-03 本机实测为准（docs/EngineDesign/M0-Go-NoGo.md 教训 5/6）；
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

bool LoadHostfxr(HostfxrApi& api, const char* dotnetRoot) {
    std::filesystem::path root = (dotnetRoot && *dotnetRoot)
                                     ? std::filesystem::path(dotnetRoot)
                                     : (getenv("LEMON_DOTNET_ROOT")
                                            ? std::filesystem::path(getenv("LEMON_DOTNET_ROOT"))
                                            : std::filesystem::path("/usr/local/share/dotnet"));
    std::filesystem::path fxrDir = root / "host" / "fxr";
    if (!std::filesystem::exists(fxrDir)) {
        LEMON_WARN("no dotnet fxr dir at %s (brew install dotnet-sdk / set LEMON_DOTNET_ROOT)",
                   fxrDir.string().c_str());
        return false;
    }
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
    if (best.empty()) {
        LEMON_WARN("no hostfxr version dir under %s", fxrDir.string().c_str());
        return false;
    }
#if defined(_WIN32)
    auto libPath = fxrDir / best / "hostfxr.dll";
#else
    auto libPath = fxrDir / best / "libhostfxr.dylib";
#endif
    api.lib = OpenLibrary(libPath.string().c_str());
    if (!api.lib) {
        LEMON_WARN("open %s failed", libPath.string().c_str());
        return false;
    }
    api.initForConfig = (hostfxr_initialize_for_runtime_config_fn)Sym(api.lib, "hostfxr_initialize_for_runtime_config");
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

} // namespace

struct CoreCLRHost::Fxr {
    HostfxrApi api;
};

bool CoreCLRHost::Load(const char* dotnetRoot, const char* runtimeConfigPath,
                       const char* entryAssemblyPath) {
    if (IsLoaded()) return true;
    fxr_ = new Fxr{};
    if (!LoadHostfxr(fxr_->api, dotnetRoot)) return false;

    hostfxr_handle ctx = nullptr;
    int rc = fxr_->api.initForConfig(runtimeConfigPath, nullptr, &ctx);
    if (rc != 0 || ctx == nullptr) {
        LEMON_WARN("hostfxr_initialize_for_runtime_config(%s) failed rc=%d", runtimeConfigPath, rc);
        return false;
    }
    rc = fxr_->api.getDelegate(ctx, hdt_load_assembly_and_get_function_pointer, &loadAssembly_);
    if (rc != 0 || loadAssembly_ == nullptr) {
        LEMON_WARN("get_runtime_delegate(load_assembly) failed rc=%d", rc);
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
