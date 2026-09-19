// Lemon 编辑器 — 入口（M4-Editor-Plan §1.1：lemon-editor 可执行目标；同源双入口 ADR-005）
// 用法：
//   lemon-editor                                   # 交互编辑
//   lemon-editor --validate                        # Vulkan 验证层常开
//   lemon-editor --smoke --frames 120 --validate --screenshot out.png [--demo]
//                  # 无头冒烟：跑 N 帧 + 自检断言 + 截屏（§6 #13；进 CI）
//   lemon-editor --project <dir>                   # 打开项目（M4.1 生效）
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
        else {
            std::printf("unknown arg: %s\n", argv[i]);
            std::printf("usage: lemon-editor [--smoke] [--frames N] [--validate] [--demo] "
                        "[--screenshot out.png] [--project dir] [--scene f.scene] "
                        "[--save-scene f.scene] [--play]\n");
            return 2;
        }
    }
    lemon::editor::EditorApp app;
    return app.Run(launch);
}
