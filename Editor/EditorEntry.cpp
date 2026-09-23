// Lemon 编辑器 — 入口（M4-Editor-Plan §1.1：lemon-editor 可执行目标；同源双入口 ADR-005）
// 用法：
//   lemon-editor                                   # 交互编辑
//   lemon-editor --validate                        # Vulkan 验证层常开
//   lemon-editor --smoke --frames 120 --validate --screenshot out.png [--demo]
//                  # 无头冒烟：跑 N 帧 + 自检断言 + 截屏（§6 #13；进 CI）
//   lemon-editor --project <dir>                   # 打开项目（M4.4：资产管线根目录）
//   lemon-editor --project <dir> --smoke --play --frames 240 [--script <Game.dll>]
//                  # 资产链/脚本刷怪冒烟（PNG 导入 + 热替换 + Instantiate.Spawn）
//   lemon-editor --final [--project <父目录>] [--frames 260]
//                  # M4.5 终验：向导建项目 → 判据场景 → Play/热重载/备份全量化
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "App/EditorApp.h"

namespace {

// 参数非法拒启（测试报告观察 1/2：此前 --frames abc/-5 静默按 0 = 无限运行、
// --smoke-close 乱值无告警跑普通模式——CI 手滑即挂起/错跑）
int usageExit(const char* why) {
    std::printf("%s\n", why);
    std::printf("usage: lemon-editor [--smoke] [--frames N] [--validate] [--demo] "
                "[--screenshot out.png] [--project dir] [--scene f.scene] "
                "[--save-scene f.scene] [--play] [--script Game.dll] [--final] "
                "[--no-reopen] [--smoke-close clean|dirty] [--smoke-drag] "
                "[--smoke-ui] [--smoke-anim] [--bench-survivor]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    lemon::editor::EditorLaunch launch;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) {
            const char* v = argv[++i];
            char* end = nullptr;
            const long n = std::strtol(v, &end, 10);
            if (!v[0] || !end || *end != '\0' || n < 0)
                return usageExit("--frames 非法值：非数字或负数（应为 ≥0 整数；0 = 无限运行）");
            launch.frames = (int)n;
        } else if (!std::strcmp(argv[i], "--validate"))
            launch.validate = true;
        else if (!std::strcmp(argv[i], "--smoke"))
            launch.smoke = true;
        else if (!std::strcmp(argv[i], "--demo"))
            launch.demoWindow = true;
        else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc)
            launch.screenshot = argv[++i];
        else if (!std::strcmp(argv[i], "--project") && i + 1 < argc)
            launch.projectDir = argv[++i];
        else if (!std::strcmp(argv[i], "--scene") && i + 1 < argc)
            launch.openScene = argv[++i];
        else if (!std::strcmp(argv[i], "--save-scene") && i + 1 < argc)
            launch.saveScene = argv[++i];
        else if (!std::strcmp(argv[i], "--play"))
            launch.playTest = true;
        else if (!std::strcmp(argv[i], "--final"))
            launch.finalTest = launch.smoke = launch.playTest = true; // 终验（含冒烟+Play）
        else if (!std::strcmp(argv[i], "--script") && i + 1 < argc)
            launch.script = argv[++i];
        else if (!std::strcmp(argv[i], "--no-reopen"))
            launch.noReopen = true; // M4.6：跳过"自动重开上次项目"
        else if (!std::strcmp(argv[i], "--smoke-close") && i + 1 < argc) {
            const char* v = argv[++i];
            if (std::strcmp(v, "clean") && std::strcmp(v, "dirty"))
                return usageExit("--smoke-close 非法值（应为 clean|dirty）");
            launch.smokeClose = v; // clean|dirty：关闭状态机交互冒烟
        } else if (!std::strcmp(argv[i], "--smoke-drag"))
            launch.smokeDrag = true; // 视口拖拽注入冒烟（M4.7c 交互回归）
        else if (!std::strcmp(argv[i], "--smoke-ui"))
            launch.smokeUi = true; // 真人会话注入冒烟（快捷键/Undo/保存/重命名/导航等）
        else if (!std::strcmp(argv[i], "--smoke-anim"))
            launch.smokeAnim = launch.smoke = launch.playTest = true; // 动画链冒烟（M5 批③）
        else if (!std::strcmp(argv[i], "--bench-survivor"))
            launch.benchSurvivor = true; // M5 压测基线（1 万怪刷怪 + Immediate + 帧时）
        else
            return usageExit((std::string("unknown arg: ") + argv[i]).c_str());
    }
    lemon::editor::EditorApp app;
    return app.Run(launch);
}
