// Lemon 编辑器 — 冒烟夹具共享面（smoke-anim 固定 guid 常量 + 程序化素材落盘 +
// bench-survivor 播种）。批③a 2026-09-29 自 EditorApp.cpp 匿名命名空间外迁：
// EditorAppSmoke（Seed 系）与 Run 内联采样链两 TU 共用，内部链接保不住。
// kSmokePngGuid（smoke.png 未切片整图）自批①起无引用，随迁删除（批① DevLog
// 登记的处置项）。
// 批④ 2026-09-30：TplSmokeState 退回 EditorAppSmokeTpl.cpp 匿名命名空间
//（g_tplSmoke 单 TU 化）——本头只余跨 TU 共享符号（常量/两函数/CountPixelsNear）。
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace lemon::editor {

class EditorContext;

// ---- M5 批③：--smoke-anim 固定 guid（程序化 4 帧表 + clip；yami 包同段命名）----
constexpr uint64_t kAnimSheetGuid = 0x5bd31a7c30000001ull; // anim-sheet.png（128×32，4×32×32 格）
constexpr uint64_t kAnimClipGuid = 0x5bd31a7c30000002ull;  // anim.anim（fps10 × cells 0..3）
constexpr uint64_t kAnimHitClipGuid = 0x5bd31a7c30000003ull; // anim-hit.anim（M6a 批①：
// fps12 × cells [3,2] loop=0——切段断言的受击段）
constexpr uint64_t kAnimEditClipGuid = 0x5bd31a7c30000004ull; // anim-edit.anim（M6a 批②
// T3：AnimationPanel 数据面编辑链断言——写→改 fps/增帧→存→roundtrip→Play 快照）
constexpr uint64_t kAnimWholeClipGuid = 0x5bd31a7c30000005ull; // anim-whole.anim（T3b-1
// 整图引用断言——未切片 smoke.png cell0 → 本体号，Play 快照 1 帧）
constexpr uint64_t kAnimSetGuid = 0x5bd31a7c30000006ull; // anim-set.override（T3c：动画集
// 按名解析断言——段引用上述三段 clip，集内按名/GUID hex 回退的反查锚点）
constexpr uint64_t kAnimGraphGuid = 0x5bd31a7c30000007ull; // anim-graph.controller（T3d：
// whole→walk 条件边 / walk→hit trigger 边 / hit→whole exitTime 边——graph 链断言锚点）
constexpr uint64_t kYamiHeroSheetGuid = 0x5bd31a7c10000001ull; // Samples yami-dungeon hero_1（在场即验）
constexpr uint64_t kYamiHeroClipGuid = 0x5bd31a7c20000001ull;  // hero-walk.anim（9 帧 @8fps）

/// 程序化动画素材三件套（sheet png + grid meta + clip + clip meta）落 assetsDir。
/// smoke-anim（SeedSmokeProject）与 bench-survivor（万怪动画化）共用；须在
/// OpenProject/Rescan 前落盘（切片记账/导入随扫描走）。
void WriteAnimSheetAssets(const std::filesystem::path& assetsDir);

/// bench-survivor 压测场景播种（tempdir 项目 + 1 万怪导演拉满；--bench-survivor）
bool SeedBenchSurvivorScene(EditorContext& ctx);

/// 冒烟像素断言辅助：overlay 渲染可见性（数像素不数包）。定义在 EditorApp.cpp
///（smoke 主链 overlay 断言留驻），批③c-5 起供 EditorAppSmokeUirml.cpp 共用。
int CountPixelsNear(const std::vector<uint8_t>& px, uint32_t w, uint32_t h, int r, int g,
                    int b, int tol);

} // namespace lemon::editor
