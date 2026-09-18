// ---------------------------------------------------------------------------
// Lemon M0-W3 spike：CoreCLR 宿主闭环 + EnTT 固定步长
// 验证三件事（Go/No-Go 判据三）：
//   1) hostfxr 引导 CoreCLR（.NET 10）并经 load_assembly_and_get_function_pointer
//      调用 C# 导出（Luma CoreCLRHost 方式的最小闭环）；
//   2) C# 批量通道：10 万实例一次调用完成与 C++ 相同的数学（对比开销比）；
//      并验证 Collectible ALC 热重载：Unload → 确认卸载 → 重新 Load → 继续工作；
//   3) EnTT 10 万实体 60Hz 固定步长预算实测。
// 设计依据：docs/EngineDesign/04-CSharp-Scripting.md、03-ECS-Runtime.md
// ---------------------------------------------------------------------------

#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include <hostfxr.h>
#include <coreclr_delegates.h>

// 与 C# Instance 完全一致的 20B 布局（5 × 4B）
struct Instance {
    float posX, posY;
    float rot;
    float scale;
    uint32_t color;
};
static_assert(sizeof(Instance) == 20);

namespace {

double NowMs() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// ---- hostfxr 引导（Luma CoreCLRHost 的最小闭环版） ---------------------------
struct Hostfxr {
    void* lib = nullptr;
    hostfxr_initialize_for_runtime_config_fn init_for_config = nullptr;
    hostfxr_get_runtime_delegate_fn get_delegate = nullptr;
    hostfxr_close_fn close = nullptr;
};

bool LoadHostfxr(Hostfxr& h) {
    // 查找 libhostfxr：$LEMON_DOTNET_ROOT 或默认 /usr/local/share/dotnet
    std::filesystem::path root = getenv("LEMON_DOTNET_ROOT")
                                     ? std::filesystem::path(getenv("LEMON_DOTNET_ROOT"))
                                     : std::filesystem::path("/usr/local/share/dotnet");
    std::filesystem::path fxrDir = root / "host" / "fxr";
    if (!std::filesystem::exists(fxrDir)) {
        std::fprintf(stderr, "[lemon] no dotnet fxr dir at %s (set LEMON_DOTNET_ROOT)\n",
                     fxrDir.string().c_str());
        return false;
    }
    // 版本目录按数字段比较（"10.0.401" > "8.0.30"，字典序会错）
    auto VersionKey = [](const std::string& v) {
        std::vector<long long> parts;
        size_t pos = 0;
        while (pos != std::string::npos) {
            size_t dot = v.find('.', pos);
            parts.push_back(std::atoll(v.substr(pos, dot - pos).c_str()));
            pos = dot == std::string::npos ? dot : dot + 1;
        }
        return parts;
    };
    std::string best;
    std::vector<long long> bestKey;
    for (auto& e : std::filesystem::directory_iterator(fxrDir)) {
        if (!e.is_directory()) continue;
        auto name = e.path().filename().string();
        auto key = VersionKey(name);
        if (best.empty() || key > bestKey) { best = name; bestKey = key; }
    }
    auto libPath = fxrDir / best / "libhostfxr.dylib";
    h.lib = dlopen(libPath.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h.lib) {
        std::fprintf(stderr, "[lemon] dlopen %s failed: %s\n", libPath.string().c_str(), dlerror());
        return false;
    }
    h.init_for_config = (hostfxr_initialize_for_runtime_config_fn)dlsym(h.lib, "hostfxr_initialize_for_runtime_config");
    h.get_delegate = (hostfxr_get_runtime_delegate_fn)dlsym(h.lib, "hostfxr_get_runtime_delegate");
    h.close = (hostfxr_close_fn)dlsym(h.lib, "hostfxr_close_handle");   // 部分 hostfxr 未导出，可选
    std::printf("[lemon] hostfxr: %s (close=%s)\n", libPath.string().c_str(), h.close ? "yes" : "no");
    return h.init_for_config && h.get_delegate;
}

} // namespace

int main() {
    // ---- 1) 引导 CoreCLR -----------------------------------------------------
    Hostfxr fxr;
    if (!LoadHostfxr(fxr)) return 1;

    std::string runtimeConfig = LEMON_DOTNET_DIR "/LemonSpike.runtimeconfig.json";
    std::string assemblyPath = LEMON_DOTNET_DIR "/LemonSpike.dll";

    load_assembly_and_get_function_pointer_fn loadAssembly = nullptr;
    {
        // hostfxr 错误回调：把宿主错误引到我们的 stderr
        using ErrorWriterFn = void (*)(const char*);
        using SetErrorWriterFn = void (*)(ErrorWriterFn);
        if (auto setWriter = (SetErrorWriterFn)dlsym(fxr.lib, "hostfxr_set_error_writer")) {
            setWriter(+[](const char* message) {
                std::fprintf(stderr, "[hostfxr] %s\n", message);
            });
        }

        std::fprintf(stderr, "[dbg] before init_for_config\n");
        hostfxr_handle ctx = nullptr;
        int rc = fxr.init_for_config(runtimeConfig.c_str(), nullptr, &ctx);
        std::fprintf(stderr, "[dbg] init_for_config(%s) rc=%d ctx=%p\n", runtimeConfig.c_str(), rc, (void*)ctx);
        if (rc != 0 || ctx == nullptr) {
            std::fprintf(stderr, "[lemon] hostfxr_initialize_for_runtime_config failed\n");
            return 1;
        }
        rc = fxr.get_delegate(ctx, hdt_load_assembly_and_get_function_pointer, (void**)&loadAssembly);
        std::fprintf(stderr, "[dbg] get_delegate rc=%d fn=%p\n", rc, (void*)loadAssembly);
        if (rc != 0 || loadAssembly == nullptr) {
            std::fprintf(stderr, "[lemon] get_runtime_delegate failed\n");
            return 1;
        }
        if (fxr.close) fxr.close(ctx);   // 句柄进程级存活，缺 close 也不影响 spike
    }

    auto GetExport = [&](const char* name) -> void* {
        void* fn = nullptr;
        // 类型全名 + 程序集名（load_assembly_and_get_function_pointer 标准格式）
        int rc = loadAssembly(assemblyPath.c_str(), "LemonSpike.Exports, LemonSpike", name,
                              UNMANAGEDCALLERSONLY_METHOD, nullptr, &fn);
        if (rc != 0 || fn == nullptr) {
            std::fprintf(stderr, "[lemon] load export %s failed: hr=0x%08x fn=%p\n", name,
                         (unsigned)rc, fn);
            std::exit(1);
        }
        return fn;
    };

    // 注意：load_assembly_and_get_function_pointer 走反射解析，用的是托管方法名
    // （EntryPoint 名仅影响导出符号，不影响此处查找）
    auto bootstrap = (int (*)())GetExport("Bootstrap");
    auto csLoad = (int (*)(const char*))GetExport("Load");
    auto csUnload = (int (*)())GetExport("Unload");
    auto csTick = (double (*)(void*, int, float))GetExport("ScriptTick");
    auto csLifecycle = (double (*)(int, int))GetExport("LifecycleDemo");
    auto csSelfTest = (int (*)(const char*))GetExport("UnloadSelfTest");

    // 隔离实验：托管内部 load→unload（不跨 native 边界）
    std::printf("[lemon] managed selftest load->unload: %s\n",
                csSelfTest(LEMON_DOTNET_DIR "/Script.dll") ? "OK" : "TIMEOUT");

    const int boot = bootstrap();
    if (boot != 0x1E0F) {
        std::fprintf(stderr, "[lemon] bootstrap mismatch: %x\n", boot);
        return 1;
    }
    std::printf("[lemon] CoreCLR up, bootstrap OK\n");

    // ---- 2) 干净对照实验：加载后立即卸载（不调用任何脚本函数） -----------------
    {
        if (!csLoad(LEMON_DOTNET_DIR "/Script.dll")) return 1;
        double tclean = NowMs();
        int cleanOk = csUnload();
        double cleanMs = NowMs() - tclean;
        std::printf("[lemon] clean load->unload (no calls): %s (%.0f ms)\n",
                    cleanOk ? "OK" : "TIMEOUT", cleanMs);
    }

    // ---- 3) 正式加载脚本程序集（Collectible ALC） -------------------------------
    if (!csLoad(LEMON_DOTNET_DIR "/Script.dll")) return 1;
    std::printf("[lemon] Script.dll loaded into collectible ALC\n");

    // ---- 3) 数据准备 ----------------------------------------------------------
    constexpr int kCount = 100000;
    std::vector<Instance> instances(kCount);

    // C++ 基准：与 Script.Tick 相同的数学
    auto cppTick = [&](float t) {
        constexpr float Cx = 640.0f, Cy = 360.0f;
        double checksum = 0;
        for (int i = 0; i < kCount; i++) {
            float a = (i % 360) * 0.017453293f + t;
            instances[i].posX = Cx + std::cos(a) * (8.0f + (i % 500));
            instances[i].posY = Cy + std::sin(a) * (8.0f + (i % 500)) * 0.62f;
            instances[i].rot = a * 2.0f;
            checksum += instances[i].posX;
        }
        return checksum;
    };

    // ---- 4) EnTT 固定步长预算 -------------------------------------------------
    entt::registry reg;
    struct Position { float x, y; };
    struct Velocity { float dx, dy; };
    std::vector<entt::entity> ents(kCount);
    for (int i = 0; i < kCount; i++)
        ents[i] = reg.create();
    for (int i = 0; i < kCount; i++) {
        reg.emplace<Position>(ents[i], 0.f, 0.f);
        reg.emplace<Velocity>(ents[i], 1.5f, -0.3f);
    }

    constexpr int kBenchTicks = 1000;
    volatile double sink = 0;

    // EnTT 运动系统
    double t0 = NowMs();
    for (int k = 0; k < kBenchTicks; k++) {
        auto view = reg.view<Position, const Velocity>();
        for (auto [e, p, v] : view.each()) {
            p.x += v.dx;
            p.y += v.dy;
        }
    }
    double enttMs = (NowMs() - t0) / kBenchTicks;

    // C++ 原生批量
    t0 = NowMs();
    for (int k = 0; k < kBenchTicks; k++) sink += cppTick(k * (1.0f / 60.0f));
    double cppMs = (NowMs() - t0) / kBenchTicks;

    // C# 批量（同一负载）
    [[maybe_unused]] double csCheck = 0;
    t0 = NowMs();
    for (int k = 0; k < kBenchTicks; k++) sink += csTick(instances.data(), kCount, k * (1.0f / 60.0f));
    double csMs = (NowMs() - t0) / kBenchTicks;

    // C# 生命周期（脚本组件档① 雏形）
    double lifeUs = csLifecycle(1000, 100);

    // ---- 5) 热重载：卸载 → 重载 → 继续工作 ------------------------------------
    t0 = NowMs();
    int unloaded = csUnload();
    double unloadMs = NowMs() - t0;
    double deadTick = csTick(instances.data(), kCount, 0.0f);   // 卸载后应返回 NaN
    t0 = NowMs();
    int reloaded = csLoad(LEMON_DOTNET_DIR "/Script.dll");
    double reloadMs = NowMs() - t0;
    double liveTick = csTick(instances.data(), kCount, 1.0f);   // 重载后恢复工作

    // ---- 6) 60Hz 步进 3 秒（真节奏验证：EnTT C++ 步 + C# 批量步） ---------------
    const double dt = 1.0 / 60.0;
    double tickSum = 0;
    uint64_t ticks = 0;
    double minTick = 1e9, maxTick = 0;
    {
        const double start = NowMs();
        double next = start;
        while (true) {
            double now = NowMs();
            if (now - start >= 3000.0) break;
            if (now < next) continue;
            next += dt * 1000.0;
            if (next < now) next = now;   // 防追帧螺旋
            double s = NowMs();
            sink += cppTick((float)(ticks * dt));
            sink += csTick(instances.data(), kCount, (float)(ticks * dt));
            double ms = NowMs() - s;
            tickSum += ms;
            minTick = std::min(minTick, ms);
            maxTick = std::max(maxTick, ms);
            ++ticks;
        }
    }

    // ---- 汇总 -------------------------------------------------------------------
    std::printf("\n[lemon] ===== M0-W3 实测结果 =====\n");
    std::printf("  EnTT 100k (pos+vel) 积分     : %.3f ms/tick\n", enttMs);
    std::printf("  C++ 批量更新 100k            : %.3f ms/tick\n", cppMs);
    std::printf("  C#  批量更新 100k (同一负载) : %.3f ms/tick  (开销比 C#/C++ = %.2fx)\n", csMs,
                csMs / cppMs);
    std::printf("  C#  脚本组件 OnUpdate        : %.3f us/call (MethodInfo.Invoke 反射)\n", lifeUs);
    std::printf("  ALC 卸载                     : %s (%.0f ms)\n", unloaded ? "OK" : "TIMEOUT",
                unloadMs);
    std::printf("  卸载后调用                    : %s\n", std::isnan(deadTick) ? "NaN(正确隔离)" : "异常返回值!");
    std::printf("  重新加载                      : %s (%.0f ms)，恢复工作: %s\n", reloaded ? "OK" : "FAIL",
                reloadMs, std::isnan(liveTick) ? "否" : "是");
    std::printf("  60Hz 步进实测                 : %llu ticks, tick avg %.3f ms (min %.3f / max %.3f)\n",
                (unsigned long long)ticks, ticks ? tickSum / ticks : 0, minTick, maxTick);
    (void)sink;
    std::printf("[lemon] spike-03 exit OK\n");
    return 0;
}
