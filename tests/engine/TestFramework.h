// Lemon 引擎单测共享断言件（M7c 批⓪ T2 自 engine_tests.cpp 原样抽出；
// 计数器跨 TU 汇总，TestMain.cpp 定义 g_checks）
#pragma once
#include "Core/Log.h"

#include <cmath>
#include <cstdlib>

extern int g_checks;

inline void Expect(bool cond, const char* what) {
    ++g_checks;
    if (!cond) {
        LEMON_LOG("FAIL: %s", what);
        std::abort();
    }
}
inline void ExpectNear(float a, float b, float eps, const char* what) {
    ++g_checks;
    if (std::fabs(a - b) > eps) {
        LEMON_LOG("FAIL: %s  (%f vs %f, eps %g)", what, a, b, eps);
        std::abort();
    }
}
inline bool ExpectNear0(float a, float b) { return std::fabs(a - b) < 1e-5f; }

// 各域 TU 的汇总出口（定义在各 TU 文件末尾；Assets/Editor 两 TU 无编辑器时函数体空转）
void RunCoreTests();
void RunRendererTests();
void RunEcsTests();
void RunSceneTests();
void RunGameplayTests();
void RunAssetsTests();
void RunEditorTests();
void RunAudioTests();
