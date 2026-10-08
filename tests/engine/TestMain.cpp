// Lemon 引擎单测入口（M7c 批⓪ T2：域 TU 汇总调用；调用序 = Core→Renderer→ECS→
// Scene→Gameplay→Assets→Editor→Audio，域内保持原文件定义序）
#include "TestFramework.h"

int g_checks = 0;

int main() {
    RunCoreTests();
    RunRendererTests();
    RunEcsTests();
    RunSceneTests();
    RunGameplayTests();
#ifdef LEMON_EDITOR_CORE
    RunAssetsTests();
#endif
#ifdef LEMON_EDITOR_CORE
    RunEditorTests();
#endif
    RunAudioTests();
    LEMON_LOG("engine-tests: %d checks OK", g_checks);
    return 0;
}
