// Lemon 编辑器 — 冒烟夹具共享面（smoke-anim 固定 guid 常量 + 程序化素材落盘 +
// bench-survivor 播种）。批③a 2026-09-29 自 EditorApp.cpp 匿名命名空间外迁：
// EditorAppSmoke（Seed 系）与 Run 内联采样链两 TU 共用，内部链接保不住。
// kSmokePngGuid（smoke.png 未切片整图）自批①起无引用，随迁删除（批① DevLog
// 登记的处置项）。
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

// ---- M5 批④ --smoke-template 证据状态（批③b 2026-09-29 文件级 g_tpl* 标量收敛为
// 单结构体实例；批③c-4 随函数族外迁：定义在 EditorAppSmokeTpl.cpp，此处 extern
// 共享给 EditorApp.cpp 两处原位读点——渲染段 capReq 捕获请求 + FeedGameUiInput
// 的 pointerHold 让位窗。保持文件级存储两处刚需：无捕获 event sink lambda 的
// 计数改写（SmokeTplPlaySetup 装配）+ 指针保持窗跨函数读取）----
struct TplSmokeState {
    int waveStarts = 0, levelUps = 0, deaths = 0;
    int gems = 0, mobs = 0; // 峰值快照（帧内采样）
    // 批③d 前置 T5 → 批③d-1 随迁：模板场景现挂 2 UIDocument（HUD/cards）——零装载
    // 护栏升级为"通道 A 装载恰 2"（装载点单一性防线的同型收紧；bench 场景仍零装载）
    int uiLoads = -1;
    // 批③d-1：文档化断言态（原 RtUi 行/卡片探针随迁）+ 层序三拍状态机
    bool hudDocOk = false;
    int layerStage = 0; // 0 基线请求→1 取回→2 等卡片→3 取回→4 等隐藏→5 取回→6 完
    int hudPixN0 = -1, hudPixDuring = -1, hudPixAfter = -1;
    bool capReq = false, capPending = false;
    int capPix = -1;
    int clickPhase = 0, clickCooldown = 0; // 直灌点击三帧（定位/down/up）+ 冷却
    bool pointerHold = false; // 指针保持窗（FeedGameUiInput 让位——Update 建悬停用）
    float clickX = 0, clickY = 0;
    char hudRows[64] = "";
    bool bestLoaded = false, waveRow = false; // g_tplHudOk → g_tplHudDocOk（批③d-1 随迁）
    // T8 后修：进度条断言（文本探针测不出"样式写了布局没生效"——bar-fill 曾因
    // RmlUi 默认 inline 宽高被忽略而恒 0，文本六行全绿）。后修② 条改原生 progress：
    // 断言 = 轨道盒（120dp×10dp×ratio）+ value 属性回读对文本行数值
    bool hudBarBox = false;
    // M6a 批①：受击切段链（怪 clipId 曾 = monster-hit 段）+ fx 通道（飘字/血条在场）
    bool mobHitClip = false, fxText = false, fxBar = false;
    bool cardsSeen = false, picked = false, cardsHidden = false;
    bool deathSeen = false, revived = false, scriptOk = true; // 批④后修④死亡链
    bool deathArmed = false; // 压血一shot（站桩下自动炮火清怪快于刷怪，磨不死）
    // M6a 批② T4：数值表载入断言（weapons 4 行 × upgrades 7 行 × balance 2 行——
    // 含列头行；PlayerCombat.Start 读、EnterPlay 快照建 TableStore）
    bool tablesOk = false;
};
extern TplSmokeState g_tplSmoke;

} // namespace lemon::editor
