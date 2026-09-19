// Lemon 引擎 — CoreCLR 宿主（04 文档 §1：hostfxr 引导与托管导出获取）
// 纪律：hostfxr/CoreCLR 类型只允许出现在 Scripting 的 .cpp（同 Renderer 零泄漏形态），
//   本头文件只有自有句柄。
// 本机实测结论（M0 spike-03，Go-NoGo 教训 5/6）：
//   * libhostfxr 不导出 hostfxr_close_handle —— 按可选符号处理（句柄进程级存活）；
//   * 类库工程不生成 runtimeconfig.json —— 模板手工分发（dotnet/Lemon.Entry/）；
//   * load_assembly_and_get_function_pointer 按托管方法名解析（非 EntryPoint 名）。
#pragma once

#include <string>

namespace lemon::scripting {

/// hostfxr 引导 + 入口程序集导出获取。进程级一次：CoreCLR 起后不可卸载（M3 无此需求，
/// 进程退出即回收——ADR-010：域卸载只针对用户脚本 ALC，由 DomainManager 管）。
class CoreCLRHost {
public:
    /// dotnetRoot 为空时依次取 $LEMON_DOTNET_ROOT、/usr/local/share/dotnet。
    /// runtimeConfigPath = 入口程序集的 runtimeconfig（手工模板）；entryAssemblyPath = 入口 DLL。
    bool Load(const char* dotnetRoot, const char* runtimeConfigPath, const char* entryAssemblyPath);

    bool IsLoaded() const { return loadAssembly_ != nullptr; }

    /// 取入口程序集导出（UNMANAGEDCALLERSONLY_METHOD；methodName 用托管方法名）。
    /// typeNameWithAsm 形如 "Lemon.Entry.Exports, Lemon.Entry"。失败返回 nullptr。
    /// 启动期一次取全并缓存为强类型函数指针——热路径不做字符串查找。
    void* GetExport(const char* typeNameWithAsm, const char* methodName) const;

private:
    struct Fxr;                  // hostfxr 函数表（定义在 .cpp，头文件零泄漏）
    Fxr* fxr_ = nullptr;
    void* loadAssembly_ = nullptr; // load_assembly_and_get_function_pointer_fn
    std::string entryAssemblyPath_;
};

} // namespace lemon::scripting
