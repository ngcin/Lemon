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
#include <cstring>

#include "App/EditorApp.h"

int main(int argc, char** argv) {
    lemon::editor::EditorLaunch launch;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--frames") && i + 1 < argc)
            launch.frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--validate"))
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
        else if (!std::strcmp(argv[i], "--smoke-close") && i + 1 < argc)
            launch.smokeClose = argv[++i]; // clean|dirty：关闭状态机交互冒烟
        else if (!std::strcmp(argv[i], "--smoke-drag"))
            launch.smokeDrag = true; // 视口拖拽注入冒烟（M4.7c 交互回归）
        else if (!std::strcmp(argv[i], "--smoke-ui"))
            launch.smokeUi = true; // 真人会话注入冒烟（快捷键/Undo/保存/重命名/导航等）
        else {
            std::printf("unknown arg: %s\n", argv[i]);
            std::printf("usage: lemon-editor [--smoke] [--frames N] [--validate] [--demo] "
                        "[--screenshot out.png] [--project dir] [--scene f.scene] "
                        "[--save-scene f.scene] [--play] [--script Game.dll] [--final] "
                        "[--no-reopen] [--smoke-close clean|dirty] [--smoke-drag] "
                        "[--smoke-ui]\n");
            return 2;
        }
    }
    lemon::editor::EditorApp app;
    return app.Run(launch);
}
