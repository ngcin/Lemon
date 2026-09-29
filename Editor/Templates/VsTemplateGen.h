// Lemon 编辑器 — vs-survivor 模板生成器接口（批① 2026-09-29 自 EditorApp.cpp
// 外迁；GUID 常量块上移此文件单源——生成器写 .meta 与 Run 内 smoke-template
// 采样（表计数/受击段判定）共用，模板 GUID 锚点不再散落实现文件）
#pragma once

#include <cstdint>
#include <string>

namespace lemon::editor {

class EditorContext; // 前置声明（实现文件含 EditorContext.h）

namespace vs_template {
constexpr uint64_t kGemPng = 0x7e57000000000001ull;       // gem.png（程序化 16×16）
constexpr uint64_t kBulletPng = 0x7e57000000000002ull;    // bullet.png
constexpr uint64_t kPiercePng = 0x7e57000000000003ull;    // pierce.png
constexpr uint64_t kBladePng = 0x7e57000000000004ull;     // blade.png
constexpr uint64_t kMobPf = 0x7e57100000000001ull;        // Mob.prefab
constexpr uint64_t kBossPf = 0x7e57100000000002ull;       // BossMob.prefab
constexpr uint64_t kBulletPf = 0x7e57100000000003ull;     // Bullet.prefab
constexpr uint64_t kPiercePf = 0x7e57100000000004ull;     // PierceBullet.prefab
constexpr uint64_t kGemPf = 0x7e57100000000005ull;        // Gem.prefab
constexpr uint64_t kBladePf = 0x7e57100000000006ull;      // Blade.prefab
// yami 素材沿用批③入库 guid（Templates 侧拷贝即引用同源）
constexpr uint64_t kHeroSheet = 0x5bd31a7c10000001ull;
constexpr uint64_t kMonsterSheet = 0x5bd31a7c10000003ull;
constexpr uint64_t kBossSheet = 0x5bd31a7c10000005ull;
constexpr uint64_t kHeroClip = 0x5bd31a7c20000001ull;
constexpr uint64_t kMonsterClip = 0x5bd31a7c20000002ull;
// M6a 批①：受击段（yami 表尾两帧 @12fps loop=0——自研 authored clip 引用 yami
// 切片，不入 yami 包目录防污染；生成器直写模板 Assets）
constexpr uint64_t kHeroHitClip = 0x5bd31a7c20000003ull;
constexpr uint64_t kMonsterHitClip = 0x5bd31a7c20000004ull;
// M6a 批② T4：数值配置表（Assets/tables/；独立 7e572000 段区别 png/prefab 段。
// 消费面 = PlayerCombat（武器参数/升级池）+ GameMain 侧 balance 写 xpCurveK）
constexpr uint64_t kWeaponsTab = 0x7e57200000100001ull;  // weapons.tab
constexpr uint64_t kUpgradesTab = 0x7e57200000100002ull; // upgrades.tab
constexpr uint64_t kBalanceTab = 0x7e57200000100003ull;  // balance.tab
// M6b 批③d-1：游戏 UI 文档/样式资产（Assets/UI/；7e5730 段。HUD/卡片两屏样板 +
// L2 token 单源——六屏其余四屏 ③d-2 铺量）
constexpr uint64_t kHudRml = 0x7e57300000100001ull;    // hud.rml（HUD 六行）
constexpr uint64_t kCardsRml = 0x7e57300000100002ull;  // cards.rml（升级三选一/死亡对话）
constexpr uint64_t kThemeRcss = 0x7e57300000100003ull; // theme.rcss（L2 主题 token 单源）

/// --smoke-guid（M6a 批⓪ T5）：sprite 引用 GUID 稳定性链——插队导入/资产
/// 改名/删 manifest 三难并发 → 重开逐实体断言（详注见 VsTemplateGen.cpp）
bool RunGuidSmokeChain(uint32_t spriteIdBase);

} // namespace vs_template

/// --gen-vs-template <dir>：产出 vs-survivor 模板项目文件（跑一次、入库）。
/// 结构 = 完整项目：yami 素材 + 程序化小图（.meta 固定 guid）+ Prefabs（固定 guid
/// 占位 meta → 扫描后覆写内容）+ Game/ 脚本 + Main.scene（玩家/导演 + 波表）。
bool GenerateVsTemplate(EditorContext& ctx, uint32_t spriteIdBase,
                        const std::string& outRoot);

} // namespace lemon::editor
