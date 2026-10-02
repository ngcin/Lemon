// Lemon 编辑器 — vs-survivor 模板生成器实现（批① 2026-09-29 自 EditorApp.cpp
// 机械外迁——vs_template 命名空间块 + GenerateVsTemplate 原样迁此，行为零变化；
// 匿名命名空间 → 具名 vs_template（EditorApp 经头文件引用 RunGuidSmokeChain/常量））
// ---- M5 批④：vs-survivor 模板（Templates/vs-survivor 生成器 + 冒烟）------------
// 生成器 = 开发工具（--gen-vs-template 跑一次、产物入库随仓库版本管理）；
// --smoke-template = 面向回归的模板链冒烟（向导复制 → build → Play → 断言）。
// GUID 全固定：模板内引用锚点（06 §7"模板即项目"——资产 .meta 随行、
// project.lemon 的项目 GUID 由向导复制时重生成 = 存档隔离键）。
#include "Templates/VsTemplateGen.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "stb_image_write.h"

#include "Core/Process.h" // CurrentProcessId（tempdir 唯一名；Windows 阻断项①，07 §3.6）

#include "Assets/AssetDatabase.h"
#include "Assets/ProjectWizard.h"
#include "Components/BehaviorComponents.h" // Health/Shooter/WaveDirector/WaveDef
#include "Components/CoreComponents.h"
#include "Components/GameplayComponents.h"
#include "Components/RenderComponents.h"
#include "Components/UiComponents.h"
#include "Core/Log.h"
#include "EditorContext.h"
#include "Serialization/SceneArchive.h"

namespace lemon::editor {

namespace vs_template {

/// 批①受击段两件套（clip + meta 落 assetsDir；引用 hero/monster 切片表尾帧）
void WriteHitClips(const std::filesystem::path& assetsDir) {
    struct Spec {
        const char* file;
        uint64_t guid;
        const char* name;
        const char* sheet;
        int c0, c1;
    };
    const Spec specs[] = {
        {"hero-hit.anim", kHeroHitClip, "hero-hit", "5bd31a7c10000001", 8, 7},
        {"monster-hit.anim", kMonsterHitClip, "monster-hit", "5bd31a7c10000003", 7, 6},
    };
    for (const Spec& sp : specs) {
        {
            std::ofstream f(assetsDir / sp.file, std::ios::trunc);
            f << "{\n  \"schemaVersion\": 1,\n  \"name\": \"" << sp.name
              << "\",\n  \"fps\": 12,\n  \"loop\": false,\n  \"frames\": [\n"
              << "    { \"sheet\": \"" << sp.sheet << "\", \"cell\": " << sp.c0 << " },\n"
              << "    { \"sheet\": \"" << sp.sheet << "\", \"cell\": " << sp.c1 << " }\n"
              << "  ]\n}\n";
        }
        std::ofstream m(assetsDir / (std::string(sp.file) + ".meta"), std::ios::trunc);
        m << "{\n  \"guid\": \"" << AssetDatabase::GuidToHex(sp.guid)
          << "\",\n  \"type\": \"clip\",\n  \"hash\": 0,\n  \"importedAt\": 0\n}\n";
    }
}

/// M6c 批④：模板音频七件（Samples/Assets/cc0-audio CC0 包 → Assets/Audio/；
/// .meta 随行 = 固定 guid 7e574 段。BGM >1MiB 流式（meta preload:false）、全曲
/// 循环 loop:[0,0]；六件 SFX 整载。来源登记见包内 README + THIRD_PARTY.md）。
bool WriteAudioAssets(const std::filesystem::path& assets) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(assets / "Audio", ec);
    const fs::path src = fs::path(LEMON_TEMPLATE_DIR).parent_path() /
                         "Samples/Assets/cc0-audio";
    for (const char* f : {"bgm.ogg",     "bgm.ogg.meta",     "hit.ogg",
                          "hit.ogg.meta", "kill.ogg",        "kill.ogg.meta",
                          "pickup.ogg",  "pickup.ogg.meta", "levelup.ogg",
                          "levelup.ogg.meta", "wave.ogg",    "wave.ogg.meta",
                          "ui-click.ogg", "ui-click.ogg.meta"}) {
        if (!fs::is_regular_file(src / f, ec)) {
            LEMON_ERROR("gen-vs-template：cc0-audio 素材包缺失 %s/%s",
                        src.string().c_str(), f);
            return false; // 缺件即断——"全程有声"判据依赖七件全
        }
        fs::copy(src / f, assets / "Audio" / f, fs::copy_options::overwrite_existing,
                 ec);
        // review 2026-10-02 #26：拷贝失败（磁盘满/权限）即断——此前不查 ec 无条件
        // 返回 true，会产出缺音频的"成功"模板入库，违背「全程有声」判据
        if (ec) {
            LEMON_ERROR("gen-vs-template：音频拷贝失败 %s → %s（%s）",
                        (src / f).string().c_str(),
                        (assets / "Audio" / f).string().c_str(), ec.message().c_str());
            return false;
        }
    }
    return true;
}

/// M6a 批② T4：数值配置表三件（Assets/tables/；ADR-012 D1 全字符串格）。
/// 列契约（首行 = 列头；PlayerCombat 读——语义详见其头注释）：
///   weapons：id label prefabGuid interval speed pierce count radius angle
///     ——弹体参数（伤害/弹速/穿透）归 prefab；表配发射参数（interval/散射
///     count+angle/环绕 speed 角速+count 刃数+radius 轨道半径）。
///   upgrades：id label kind value（kind 0-5 派发；value = 倍率/换弹种行 id/点数）
///   balance：id label value（xpCurveK = XP 曲线系数，GameMain 侧写 World）
void WriteTableAssets(const std::filesystem::path& assetsDir) {
    struct TabSpec {
        const char* file;
        uint64_t guid;
        const char* name;
        std::vector<std::vector<std::string>> rows;
    };
    const TabSpec tabs[] = {
        {"weapons.tab", kWeaponsTab, "weapons",
         {{"id", "label", "prefabGuid", "interval", "speed", "pierce", "count",
           "radius", "angle"},
          {"shoot", "直射", "7e57100000000003", "0.12", "", "", "", "", ""},
          {"pierce", "穿透", "7e57100000000004", "0.12", "", "", "", "", ""},
          {"blade", "环绕之刃", "7e57100000000006", "", "2.2", "", "2", "90", ""}}},
        {"upgrades.tab", kUpgradesTab, "upgrades",
         {{"id", "label", "kind", "value"},
          {"u0", "移速 +10%", "0", "1.10"},
          {"u1", "磁力 +25%", "1", "1.25"},
          {"u2", "射速 +15%", "2", "0.85"},
          {"u3", "穿透弹", "3", "pierce"},
          {"u4", "生命上限 +25", "4", "25"},
          {"u5", "环绕之刃 +1", "5", "1"}}},
        {"balance.tab", kBalanceTab, "balance",
         {{"id", "label", "value"}, {"xpCurveK", "XP 曲线系数", "1.25"}}},
    };
    for (const TabSpec& tb : tabs) {
        {
            nlohmann::json doc;
            doc["schemaVersion"] = 1;
            doc["name"] = tb.name;
            doc["rows"] = tb.rows;
            std::ofstream f(assetsDir / "tables" / tb.file, std::ios::trunc);
            f << doc.dump(2) << "\n";
        }
        std::ofstream m(assetsDir / "tables" /
                            (std::string(tb.file) + ".meta"),
                        std::ios::trunc);
        m << "{\n  \"guid\": \"" << AssetDatabase::GuidToHex(tb.guid)
          << "\",\n  \"type\": \"table\",\n  \"hash\": 0,\n  \"importedAt\": 0\n}\n";
    }
}

/// M6b 批③d-1：游戏 UI 三资产（Assets/UI/）。theme.rcss = L2 主题 token 单源
/// （换肤 = 只改 token 区，组件区全引 var()）；hud/cards 两屏 = 六屏中的样板双屏
///（ADR-014 一屏一文档 + UIDocument 场景挂载）。字号/间距/偏移全 dp（ratio =
/// 画布高/720，B1 坐标系）；布局 flex/百分比。数字键通道退役——点击选择（设计
/// 定案 5，批文件落账）。
void WriteUiAssets(const std::filesystem::path& assets) {
    namespace fs = std::filesystem;
    fs::create_directories(assets / "UI");
    struct Spec {
        const char* file;
        uint64_t guid;
        const char* type;
        const char* body;
    };
    const Spec specs[] = {
        {"theme.rcss", kThemeRcss, "rcss", R"RCSS(/* theme.rcss — L2 最小默认皮 · 主题 token 单源（M6b 批③d-1，ADR-014 D7）
 * 换肤 = 只改 token 区（组件区不出现裸色值/裸字号，全引 var()）。
 * 字号/间距全 dp：ratio = 画布高 / 720（720dp 设计基准画布）；px 值不随 ratio 缩放。 */

body {
    /* 色板 */
    --c-bg:     #141a22;    /* 屏底 */
    --c-panel:  #22303fdd;  /* 面板底（半透明） */
    --c-border: #5a7a9a;    /* 面板描边 */
    --c-text:   #e8e8e8;    /* 正文 */
    --c-dim:    #9aa3ad;    /* 次要文字（hint） */
    --c-accent: #ffcc44;    /* 强调（标题/hover） */
    --c-hp:     #e05a56;    /* 血条红 */
    --c-xp:     #f0c040;    /* 经验金 */
    --c-time:   #f0f0f0;    /* 计时白 */
    --c-kill:   #f09840;    /* 击杀橙 */
    --c-wave:   #60e0a0;    /* 波次绿 */
    --c-bar-bg: #000000aa;  /* 进度条底 */
    --c-scrim:  #000000a0;  /* 模态暗罩 */
    --c-slider: #ffcc44;    /* 滑条拖点（= 强调色系；hover 亮阶见组件区） */

    /* 品级色（.card.rare/.epic 等 class 消费；③e 图鉴规模化用） */
    --rarity-common: #9aa3ad;
    --rarity-rare:   #4d9fff;
    --rarity-epic:   #b04dff;

    /* 字号（720dp 基准；464px 烟测画布 ×0.644 —— 20dp ≈ 12.9px） */
    --fs-huge: 40dp;
    --fs-title: 26dp;
    --fs-hud:   20dp;
    --fs-body:  18dp;
    --fs-hint:  14dp;

    /* 间距 */
    --sp-1: 4dp;
    --sp-2: 8dp;
    --sp-3: 16dp;

    font-family: Noto Sans SC;
    color: var(--c-text);

    /* 画布约定：body = 定位包含块 + 满画布。RmlUi 偏差——静态 body 下的
       absolute/fixed 子元素百分比尺寸解析到 RootBox（非盒）= 零包含块（实现期
       实证 scrim 0×0）；relative 后以 body 盒为包含块。高 720dp × dp 比率
       （= 画布高/720）恒等于画布全高——全屏覆盖元的标准形态 */
    position: relative;
    width: 100%;
    height: 720dp;
}

/* ---- L2 组件首件套（样板两屏所需最小集）---- */

.panel {                       /* 居中面板壳（卡片/对话框） */
    display: block;            /* L2 组件显式 display：RmlUi 默认 inline（flex 子项
                                  虽被块化，显式声明意图 + 防挪出 flex 场景复现
                                  bar-fill 教训） */
    background: var(--c-panel);
    border: 2dp var(--c-border);
    padding: var(--sp-3);
}
.scrim {                       /* 全屏模态暗罩 + flex 居中（尺寸百分比吃 body 盒——
                                   见 body 画布约定注记；right/bottom 拉伸 RmlUi 不支持） */
    position: absolute;
    display: flex;
    align-items: center;
    justify-content: center;
    left: 0; top: 0;
    width: 100%; height: 100%;
    background: var(--c-scrim);
}
.title {
    display: block;            /* inline 上 text-align 无效（bar-fill 同源教训） */
    font-size: var(--fs-title);
    color: var(--c-accent);
    text-align: center;
    margin: var(--sp-1) 0 var(--sp-2) 0;
}
.hint {
    display: block;
    font-size: var(--fs-hint);
    color: var(--c-dim);
    text-align: center;
    margin-top: var(--sp-2);
}

/* HUD 行（左上纵列：文本 + 可选进度条同行） */
.hud-row {
    font-size: var(--fs-hud);
    margin-bottom: var(--sp-1);
    display: block;
}
.bar {                         /* 进度条 = 原生 <progress>（T8 后修②：value/max 走
                                  属性通道 C# SetAttr 驱动；fill 为引擎定位的非 DOM
                                  子元素——display 布局坑天然免疫；direction/fill-image
                                  留作 ③e 表盘/竖条） */
    display: inline-block;
    width: 120dp;
    height: 10dp;
    margin-left: var(--sp-2);
    background: var(--c-bar-bg);
}
.bar.hp fill { background-color: var(--c-hp); }
.bar.xp fill { background-color: var(--c-xp); }

/* 卡片按钮（升级三选一 / 对话框单条形态） */
.card {
    display: block;
    width: 220dp;
    height: 64dp;
    margin: var(--sp-2) 0;
    padding: var(--sp-2);
    background: var(--c-panel);
    border: 2dp var(--rarity-common);
    color: var(--c-text);
    font-size: var(--fs-body);
    text-align: center;
}
.card:hover  { border-color: var(--c-accent); background: #2d4056dd; }
.card:active { border-color: var(--c-accent); background: #35507add; }
.card.rare { border-color: var(--rarity-rare); }
.card.epic { border-color: var(--rarity-epic); }

/* ---- 批③d-2：流程四屏组件（主菜单/暂停/设置/结算）---- */

.menu-bg {                      /* 主菜单实心底全屏（覆盖 HUD——菜单态非叠加态） */
    position: absolute;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    left: 0; top: 0;
    width: 100%; height: 100%;
    background: var(--c-bg);
}
.menu-title {                   /* 主菜单大标题 */
    display: block;
    font-size: var(--fs-huge);
    color: var(--c-accent);
    margin-bottom: var(--sp-3);
}
.menu-list {                    /* 按钮列容器（主菜单/暂停/结算共用） */
    display: block;
}
.btn-lg {                       /* 流程大按钮（菜单/暂停/结算主操作） */
    display: block;
    width: 240dp;
    height: 52dp;
    margin: var(--sp-2) 0;
    background: var(--c-panel);
    border: 2dp var(--c-border);
    color: var(--c-text);
    font-size: var(--fs-body);
    text-align: center;
}
.btn-lg:hover  { border-color: var(--c-accent); background: #2d4056dd; }
.btn-lg:active { border-color: var(--c-accent); background: #35507add; }
.set-row {                      /* 设置行：label + 值按钮两端对齐 */
    display: flex;
    align-items: center;
    justify-content: space-between;
    width: 300dp;
    margin: var(--sp-2) 0;
    font-size: var(--fs-body);
}
.set-label { display: block; color: var(--c-text); }
.btn-set {                      /* 设置开关按钮（文案 [开]/[关]，Click 翻转） */
    display: block;
    width: 88dp;
    height: 40dp;
    background: var(--c-panel);
    border: 2dp var(--c-border);
    color: var(--c-text);
    font-size: var(--fs-body);
    text-align: center;
}
.btn-set:hover  { border-color: var(--c-accent); }
.btn-set:active { border-color: var(--c-accent); background: #35507add; }

/* ---- M6c 批④：音量滑条（设置屏四行；<input type="range">）----
 * 伪元素子结构由 RmlUi WidgetSlider 实例化（slidertrack/sliderbar 非 DOM 子，
 * 几何 = RCSS 定尺寸 + widget 算偏移——bar 纵向 = margin-top，轨道横向吃满本体）。 */
.vol-slider {                   /* 本体：可点行程区（20dp 高 = 20dp 触达带） */
    display: block;
    width: 160dp;
    height: 20dp;
}
.vol-slider slidertrack {       /* 轨道 */
    height: 6dp;
    width: auto;
    background: var(--c-bar-bg);
    border: 1dp var(--c-border);
}
.vol-slider sliderbar {         /* 拖点（14dp 方块，margin-top 居中于本体） */
    width: 14dp;
    height: 14dp;
    margin-top: 3dp;
    background: var(--c-slider);
    border: 2dp var(--c-border);
}
.vol-slider sliderbar:hover { background: #ffe27a; }
.vol-value {                    /* 滑条右侧百分数（SetText 回显） */
    display: block;
    width: 44dp;
    color: var(--c-dim);
    text-align: right;
}
.stat-row {                     /* 结算统计行：label 左值右 */
    display: flex;
    justify-content: space-between;
    width: 280dp;
    margin: var(--sp-1) 0;
    font-size: var(--fs-body);
}
.stat-label { display: block; color: var(--c-dim); }
.stat-value { display: block; color: var(--c-text); }
)RCSS"},
        {"hud.rml", kHudRml, "rml", R"RML(<rml>
<head><title>hud</title>
<link type="text/rcss" rel="stylesheet" href="theme.rcss"/>
<style>
/* 屏幕几何（L3 层——组件与配色在 theme.rcss） */
#hud { position: absolute; display: block; left: 12dp; top: 10dp; }
#hp  { color: var(--c-hp); }
#xp  { color: var(--c-xp); }
#time   { color: var(--c-time); }
#kills  { color: var(--c-kill); }
#best   { color: var(--c-dim); }
#wave   { color: var(--c-wave); }
</style>
</head>
<body>
<div id="hud">
  <div id="hp" class="hud-row"><span id="hp-text"/><progress id="hp-bar" class="bar hp" max="100"/></div>
  <div id="xp" class="hud-row"><span id="xp-text"/><progress id="xp-bar" class="bar xp" max="60"/></div>
  <div id="time" class="hud-row"/>
  <div id="kills" class="hud-row"/>
  <div id="best" class="hud-row"/>
  <div id="wave" class="hud-row"/>
</div>
</body>
</rml>
)RML"},
        {"cards.rml", kCardsRml, "rml", R"RML(<rml>
<head><title>cards</title>
<link type="text/rcss" rel="stylesheet" href="theme.rcss"/>
<style>
/* 屏幕几何（L3 层）。scrim 全屏暗罩 = 模态标准形态（兼层序观察窗）；
     死亡对话框 = 同文档 SetItems 单条（纸面验证 ⓪ 单条形态） */
#cards { display: block; }
</style>
</head>
<body>
<div id="cards-modal" class="scrim">
  <div id="cards-panel" class="panel">
    <div id="cards-title" class="title"/>
    <div id="cards" data-template="card">
      <ui-template data-name="card">
        <button class="card" data-event="pick"><span data-field="label"/></button>
      </ui-template>
    </div>
    <div id="cards-hint" class="hint">点击卡片选择</div>
  </div>
</div>
</body>
</rml>
)RML"},
        // 批③d-2：流程四屏。data-event 语义名经 UI.Click 事件（key = 元素 id）；
        // 值文本（设置开关文案/结算统计）= C# SetText 驱动
        {"main.rml", kMainRml, "rml", R"RML(<rml>
<head><title>main</title>
<link type="text/rcss" rel="stylesheet" href="theme.rcss"/>
<style>
/* 主菜单 = 实心底全屏（menu-bg；覆盖 HUD——菜单态非叠加态）。按钮 id 供
     smoke 点击定位（TryGetElementBox 中心直灌） */
#main { display: flex; }
</style>
</head>
<body>
<div id="main" class="menu-bg">
  <div id="main-title" class="menu-title">LEMON SURVIVORS</div>
  <div id="main-list" class="menu-list">
    <button id="btn-start" class="btn-lg" data-event="start">开始游戏</button>
    <button id="btn-msettings" class="btn-lg" data-event="settings">设置</button>
  </div>
  <div id="main-hint" class="hint">移动 WASD · 攻击 自动 · 暂停 Esc/P</div>
</div>
</body>
</rml>
)RML"},
        {"pause.rml", kPauseRml, "rml", R"RML(<rml>
<head><title>pause</title>
<link type="text/rcss" rel="stylesheet" href="theme.rcss"/>
<style>
/* 暂停 = 半透 scrim 叠加（HUD 在下保留 = 战况可视）。Esc 与「继续」同义 */
#pause-panel { width: 300dp; }
</style>
</head>
<body>
<div id="pause-modal" class="scrim">
  <div id="pause-panel" class="panel">
    <div id="pause-title" class="title">暂停</div>
    <div id="pause-list" class="menu-list">
      <button id="btn-resume" class="btn-lg" data-event="resume">继续</button>
      <button id="btn-psettings" class="btn-lg" data-event="settings">设置</button>
      <button id="btn-tomenu" class="btn-lg" data-event="tomenu">回主菜单</button>
    </div>
  </div>
</div>
</body>
</rml>
)RML"},
        {"settings.rml", kSettingsRml, "rml", R"RML(<rml>
<head><title>settings</title>
<link type="text/rcss" rel="stylesheet" href="theme.rcss"/>
<style>
/* 设置 = scrim 叠加，入口双源（主菜单/暂停）——返回目标由 GameFlow 记忆。
     开关 = 按钮翻文案（Click 通道）；音量 = 滑条（M6c 批④：input range 的
     Change 通道——payload = 值；SetAttr 回显同值不回发 Change，免抑制位） */
#settings-panel { width: 360dp; }
#settings-vol { margin-top: var(--sp-3); }
#btn-back { margin-top: var(--sp-2); }
</style>
</head>
<body>
<div id="settings-modal" class="scrim">
  <div id="settings-panel" class="panel">
    <div id="settings-title" class="title">设置</div>
    <div class="set-row"><span class="set-label">伤害飘字</span><button id="btn-fxtext" class="btn-set" data-event="toggle-fxtext">开</button></div>
    <div class="set-row"><span class="set-label">世界血条</span><button id="btn-fxbar" class="btn-set" data-event="toggle-fxbar">开</button></div>
    <div id="settings-vol">
      <div class="set-row"><span class="set-label">主音量</span><input id="vol-master" class="vol-slider" type="range" min="0" max="100" step="5" value="80"/><span id="vol-master-val" class="vol-value">80</span></div>
      <div class="set-row"><span class="set-label">音乐</span><input id="vol-bgm" class="vol-slider" type="range" min="0" max="100" step="5" value="80"/><span id="vol-bgm-val" class="vol-value">80</span></div>
      <div class="set-row"><span class="set-label">音效</span><input id="vol-sfx" class="vol-slider" type="range" min="0" max="100" step="5" value="80"/><span id="vol-sfx-val" class="vol-value">80</span></div>
      <div class="set-row"><span class="set-label">界面</span><input id="vol-ui" class="vol-slider" type="range" min="0" max="100" step="5" value="80"/><span id="vol-ui-val" class="vol-value">80</span></div>
    </div>
    <button id="btn-back" class="btn-lg" data-event="back">返回</button>
  </div>
</div>
</body>
</rml>
)RML"},
        {"results.rml", kResultsRml, "rml", R"RML(<rml>
<head><title>results</title>
<link type="text/rcss" rel="stylesheet" href="theme.rcss"/>
<style>
/* 结算 = 半透 scrim（末帧 HUD 在下 = 战况定格可视）。统计行 SetText 四路 */
#results-panel { width: 360dp; }
#results-list { margin-top: var(--sp-3); }
</style>
</head>
<body>
<div id="results-modal" class="scrim">
  <div id="results-panel" class="panel">
    <div id="results-title" class="title"/>
    <div class="stat-row"><span class="stat-label">得分</span><span id="res-score" class="stat-value"/></div>
    <div class="stat-row"><span class="stat-label">存活</span><span id="res-time" class="stat-value"/></div>
    <div class="stat-row"><span class="stat-label">击杀</span><span id="res-kills" class="stat-value"/></div>
    <div class="stat-row"><span class="stat-label">最高</span><span id="res-best" class="stat-value"/></div>
    <div id="results-list" class="menu-list">
      <button id="btn-restart" class="btn-lg" data-event="restart">再战一局</button>
      <button id="btn-rtomenu" class="btn-lg" data-event="tomenu">回主菜单</button>
    </div>
  </div>
</div>
</body>
</rml>
)RML"},
    };
    for (const Spec& sp : specs) {
        {
            std::ofstream f(assets / "UI" / sp.file, std::ios::trunc);
            f << sp.body;
        }
        std::ofstream m(assets / "UI" / (std::string(sp.file) + ".meta"),
                        std::ios::trunc);
        m << "{\n  \"guid\": \"" << AssetDatabase::GuidToHex(sp.guid)
          << "\",\n  \"type\": \"" << sp.type
          << "\",\n  \"hash\": 0,\n  \"importedAt\": 0\n}\n";
    }
}

/// 程序化小图（16×16：宝石/直射弹/穿透弹/环绕刃）+ 固定 guid meta
void WriteProceduralAssets(const std::filesystem::path& assets) {
    namespace fs = std::filesystem;
    struct Spec { const char* file; uint64_t guid; uint8_t r, g, b; };
    const Spec specs[] = {
        {"gem.png", kGemPng, 70, 160, 255},     // 宝石蓝
        {"bullet.png", kBulletPng, 255, 220, 80}, // 直射黄
        {"pierce.png", kPiercePng, 255, 140, 60}, // 穿透橙
        {"blade.png", kBladePng, 180, 240, 255},  // 刃青白
    };
    for (const Spec& sp : specs) {
        const fs::path png = assets / sp.file;
        if (!fs::exists(png)) {
            std::vector<uint8_t> px(16 * 16 * 4);
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 16; ++x) {
                    const float dx = x - 7.5f, dy = y - 7.5f;
                    const float r = dx * dx + dy * dy;
                    uint8_t* q = &px[((size_t)y * 16 + x) * 4];
                    if (r > 7.5f * 7.5f) {
                        q[3] = 0; // 圆外透明
                    } else if (r > 5.5f * 5.5f) {
                        q[0] = sp.r / 2; q[1] = sp.g / 2; q[2] = sp.b / 2; q[3] = 255;
                    } else {
                        q[0] = sp.r; q[1] = sp.g; q[2] = sp.b; q[3] = 255;
                    }
                }
            stbi_write_png(png.string().c_str(), 16, 16, 4, px.data(), 16 * 4);
        }
        const fs::path meta = fs::path(png.string() + ".meta");
        if (!fs::exists(meta)) {
            std::ofstream f(meta, std::ios::trunc);
            f << "{\n  \"guid\": \"" << AssetDatabase::GuidToHex(sp.guid)
              << "\",\n  \"type\": \"sprite\",\n  \"hash\": 0,\n  \"importedAt\": 0\n}\n";
        }
    }
}

/// 模板 Game/ 脚本工程（csproj 固定名 Game → dll = Game.dll；HintPath 创建期锚）
void WriteGameSources(const std::filesystem::path& game, const std::string& sdkDir) {
    namespace fs = std::filesystem;
    {
        std::ofstream f(game / "Game.csproj", std::ios::trunc);
        f << "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
          << "  <!-- Lemon vs-survivor 模板脚本工程（M5 批④） -->\n"
          << "  <PropertyGroup>\n"
          << "    <TargetFramework>net10.0</TargetFramework>\n"
          << "    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n"
          << "    <Nullable>enable</Nullable>\n"
          << "    <AssemblyName>Game</AssemblyName>\n"
          << "    <RootNamespace>Game</RootNamespace>\n"
          << "    <GenerateRuntimeConfigurationFiles>false</GenerateRuntimeConfigurationFiles>\n"
          << "  </PropertyGroup>\n"
          << "  <ItemGroup>\n"
          << "    <Reference Include=\"Lemon.SDK\">\n"
          << "      <HintPath>" << (fs::path(sdkDir) / "Lemon.SDK.dll").string()
          << "</HintPath>\n"
          << "    </Reference>\n"
          << "  </ItemGroup>\n"
          << "</Project>\n";
    }
    {
        std::ofstream f(game / "GameMain.cs", std::ios::trunc);
        f << R"CS(using System.Collections.Generic;
using Lemon;

public static class GameMain
{
    /// <summary>一局共享态（M6a 批⓪ T4 三拆）：战斗写（计时/击杀/纪录/死亡），
    /// HUD/移动读。静态随 A 线换域重建、不经 StateBag 通道——迁移由 PlayerCombat
    /// 的 OnHotReloadOut/In 代收代还。</summary>
    public static class Run
    {
        public static float Time;  // 本局秒（死亡冻结）
        public static int Kills;   // 本局击杀
        public static int Best;    // 历史最高（Save "vs.best" @ Chan.Meta 持久）
        public static bool Dead;   // 死亡结算相位（三脚本共用的闸）
        public static bool ReviveUsed; // 批③d-2：复活已用（死亡策略游戏侧示例——每局一次；道具化/表驱动只动 PlayerCombat.Die 分叉）
    }

    /// <summary>设置态（批③d-2 D3：Settings 档持久化 version=1 + fx.text/fx.bar；
    /// 批④ 音量四路 vol.*。GameFlow 载入/写回，PlayerCombat.OnHit 消费门控）。
    /// 静态随域重建——GameFlow 热重载代收代还。</summary>
    public static class Settings
    {
        public static bool FxText = true; // 伤害飘字
        public static bool FxBar = true;  // 世界血条
        // M6c 批④：音量四路（0..1；默认 0.8 = 滑条 value 80。引擎应用 =
        // Audio.MasterVolume/SetGroupVolume，进 Play 装载后 + 滑条 Change 即时）
        public static float MasterVol = 0.8f;
        public static float BgmVol = 0.8f;
        public static float SfxVol = 0.8f;
        public static float UiVol = 0.8f;
    }

    /// <summary>UI 文档名（批③d-1 cards + 批③d-2 流程四屏）。UI 资产建后
    /// 不挪不改名（relPath 寻址约定）。</summary>
    internal const string CardsDoc = "Assets/UI/cards.rml";
    internal const string MainDoc = "Assets/UI/main.rml";
    internal const string PauseDoc = "Assets/UI/pause.rml";
    internal const string SettingsDoc = "Assets/UI/settings.rml";
    internal const string ResultsDoc = "Assets/UI/results.rml";

    // M6c 批④：UI 组按钮音（Assets/Audio/ui-click.ogg——全体 Click 统一打点，
    // 暂停中仍可响 = Ui 组不挂起语义的消费实证）
    private const string kSfxUi = "7e57400000000007";

    // ---- 卡片屏文档态（静态：Configure 订阅不持实例；PlayerCombat 写/消费）----
    internal static string? CardPickPending; // 待选条目 key（"cards/<id>"；读后即清 = 消费式）
    internal static string CardTitle = "";
    internal static List<UiItem>? CardItems; // 最近一次条目集（热重载重灌用）
    internal static bool CardsShown;

    public static void Configure()
    {
        // 注册序 = 跨类型 Update 执行序（04 §3.2）：流程 → 移动 → 战斗 → HUD
        //（批③d-2：GameFlow 首个——状态闸先于玩法 tick）
        Lemon.Behaviours.Register<GameFlow>();
        Lemon.Behaviours.Register<PlayerMovement>();
        Lemon.Behaviours.Register<PlayerCombat>();
        Lemon.Behaviours.Register<PlayerHud>();
        // 档② 清场批量系统（批③d-2：GameFlow.EnterRun/ReturnToMenu 消费）
        Lemon.Scripting.Register(new RunSweeper());
        // 批③d-1：UI 事件静态订阅（Configure 每域一次，跨局存活——③c 先例）。
        // Click(pick) → 待选 key（PlayerCombat.Update 消费式读取）；DocumentReloaded
        // → shown 态重灌（M2 契约——隐藏态不重放，防凭空亮屏）。批③d-2 起流程
        // 四屏事件（start/resume/settings/...）一并路由 GameFlow
        Lemon.UI.Events.Subscribe(OnUiEvent);
    }

    private static void OnUiEvent(Lemon.UiEvent e)
    {
        if (e.Kind == (byte)Lemon.UiEventKind.DocumentReloaded) {
            if (e.DocStr == CardsDoc && CardsShown) ReplayCards();
            else GameFlow.OnDocReloaded(e.DocStr); // 流程屏 shown 态重放 + 设置标签重灌
            return;
        }
        // M6c 批④：滑条值落定（payload = "%f" 值串；key = 滑条 id）→ 音量应用
        if (e.Kind == (byte)Lemon.UiEventKind.Change) {
            GameFlow.OnVolumeChange(e.KeyStr, e.PayloadStr);
            return;
        }
        if (e.Kind != (byte)Lemon.UiEventKind.Click) return;
        Audio.PlayOneShot(kSfxUi, 0.5f, AudioGroup.Ui); // 批④：UI 组按钮音全体打点
        if (e.DocStr == CardsDoc) {
            if (e.EvStr == "pick") CardPickPending = e.KeyStr;
        } else {
            GameFlow.HandleUiEvent(e); // 流程四屏（main/pause/settings/results）
        }
    }

    /// <summary>显示卡片屏（模态：模拟已 Time.Scale=0 冻结，M7 游戏侧让出；
    /// 层序 = 最近 Show 序自然压 HUD）。</summary>
    internal static void ShowCardsDoc(string title, List<UiItem> items)
    {
        CardTitle = title;
        CardItems = items;
        CardsShown = true;
        UI.Show(CardsDoc, modal: true); // Show 先行——同批后续 op 可达（③c 顺序契约）
        UI.SetText(CardsDoc, "cards-title", title);
        UI.SetItems(CardsDoc, "cards", "card", items);
        UI.Apply();
    }

    internal static void HideCardsDoc()
    {
        CardsShown = false;
        UI.Hide(CardsDoc);
        UI.Apply();
    }

    private static void ReplayCards() // 热重载重灌（title + 条目；shown 态保持）
    {
        UI.SetText(CardsDoc, "cards-title", CardTitle);
        if (CardItems != null) UI.SetItems(CardsDoc, "cards", "card", CardItems);
        UI.Apply();
    }
}
)CS";
    }
    {
        std::ofstream f(game / "GameFlow.cs", std::ios::trunc);
        f << R"CS(using System;
using Lemon;
using Lemon.Interop;

/// <summary>流程状态机（M6b 批③d-2 档1：单场景零引擎改动）。只提供流程原语：
/// EnterRun（清场 + 重挂双 prefab）/ ShowResults / ReturnToMenu / SetPaused——
/// **死亡策略归游戏侧**（何时复活/何时结算由 PlayerCombat.Die 决定；本模板示例
/// = 每局一次复活，改复活道具/表驱动只动那一处分叉）。
/// 重开 = C# 自律清场：RunSweeper 按 tag 扫场销毁 run 实体（SceneOps 命令
/// 次帧首应用）→ Spawning 握手（SweepObserved）后重挂 Player/Director prefab——
/// 脚本/表载/Start 与 WaveDirector 运行态随重挂自然归零，无手工复位清单。</summary>
public sealed class GameFlow : LemonBehaviour
{
    // 模板资产 GUID（生成期固定——引用锚点，勿改）
    private const string kPlayerPrefab = "7e57100000000007";
    private const string kDirectorPrefab = "7e57100000000008";
    // M6c 批④：BGM（Assets/Audio/bgm.ogg——开局起播，单槽交叉淡出 = 重开不叠曲）
    private const string kBgm = "7e57400000000001";

    internal enum State { Menu, Spawning, Run, Paused, Results, Settings }

    // ---- 流程态（静态：UI 事件经 GameMain 单点订阅路由，不持实例）----
    internal static State St = State.Menu;
    private static State settingsFrom = State.Menu; // 设置屏返回目标（入口双源）
    private static bool prevPause;                  // Esc 边沿（按住只切一次）
    // 清场握手：EnterRun/ReturnToMenu 置 Armed → RunSweeper 批扫（同帧或次帧，
    // 取决于事件派发时点）置 Observed → GameFlow 观察 Observed 后清对、开局。
    // 命令入队与 spawn 之间恒有 Essential 提交拍（#15 内固定序：Update → 批量）。
    internal static bool SweepArmed, SweepObserved;
    // 结算数据（ShowResults 落板 + 热重载重灌）
    private static string resTitle = "", resScore = "", resTime = "", resKills = "",
                         resBest = "";

    protected override void Start()
    {
        // 进 Play 即菜单（装载与首 tick 间可能有一帧 sim——首波 startTime≥5s 兜底）。
        // 显式 re-Show：装载通道的 Show 序随场景实体迭代序（EnTT 逆序——先建者
        // 后显 = 置顶），不重排则 HUD 压住实底菜单；D1 语义下流程屏显隐归本类
        Time.Scale = 0f;
        St = State.Menu;
        UI.Show(GameMain.MainDoc);
        UI.Apply();
        LoadSettings();
    }

    protected override void Update()
    {
        bool pauseEdge = Input.Pause && !prevPause; // 边沿语义（按住 Esc 不连切）
        switch (St) {
        case State.Menu:
            if (SweepObserved) { SweepArmed = false; SweepObserved = false; } // 回菜单清场收尾
            break;
        case State.Spawning:
            if (!SweepObserved) break;                 // 清场批未过——等握手
            SweepArmed = false;
            SweepObserved = false;
            Instantiate.Prefab(kPlayerPrefab, new Vec2(0f, 0f));
            Instantiate.Prefab(kDirectorPrefab, new Vec2(0f, 0f));
            GameMain.Run.Time = 0f;
            GameMain.Run.Kills = 0;
            GameMain.Run.Dead = false;
            GameMain.Run.ReviveUsed = false;
            Time.Scale = 1f;
            Audio.PlayBgm(kBgm, 0.55f); // M6c 批④：开战 BGM（再战重入同曲 = 单槽顶停旧曲）
            St = State.Run;                            // 入口屏已在 EnterRun 即隐
            break;
        case State.Run:
            if (pauseEdge) SetPaused(true);            // Esc/P（bit6，批③d-2 D4）
            break;
        case State.Paused:
            if (pauseEdge) SetPaused(false);
            break;
        }
        prevPause = Input.Pause;
    }

    // ---- 流程原语（PlayerCombat 死亡分叉 / UI 事件调用）----

    /// <summary>开始/重开一局：清场 →（握手后）重挂双 prefab → Run。入口屏即隐
    ///（菜单/结算——点击即走，不留残屏盖在新局上）；Spawning 中重入忽略。</summary>
    internal static void EnterRun()
    {
        if (St == State.Spawning) return;
        SweepArmed = true;
        SweepObserved = false;
        St = State.Spawning;
        Time.Scale = 0f; // 清场期冻结（无玩家在场防导演空转）
        UI.Hide(GameMain.MainDoc);
        UI.Hide(GameMain.ResultsDoc);
        UI.Apply();
    }

    /// <summary>结算屏（死亡策略的第二半——何时调由游戏侧决定）。</summary>
    internal static void ShowResults(string title, string score, string time,
                                     string kills, string best)
    {
        resTitle = title; resScore = score; resTime = time;
        resKills = kills; resBest = best;
        Time.Scale = 0f;
        UI.Show(GameMain.ResultsDoc);
        UI.SetText(GameMain.ResultsDoc, "results-title", title);
        UI.SetText(GameMain.ResultsDoc, "res-score", score);
        UI.SetText(GameMain.ResultsDoc, "res-time", time);
        UI.SetText(GameMain.ResultsDoc, "res-kills", kills);
        UI.SetText(GameMain.ResultsDoc, "res-best", best);
        UI.Apply();
        St = State.Results;
    }

    /// <summary>回主菜单：清场（在途动态屏一并收）+ 实底菜单。</summary>
    internal static void ReturnToMenu()
    {
        Audio.StopBgm(0.5f); // M6c 批④：回菜单静场（0.5s 淡出）
        SweepArmed = true;
        SweepObserved = false;
        Time.Scale = 0f;
        UI.Hide(GameMain.PauseDoc);
        UI.Hide(GameMain.SettingsDoc);
        UI.Hide(GameMain.ResultsDoc);
        GameMain.HideCardsDoc();
        UI.Show(GameMain.MainDoc);
        UI.Apply();
        St = State.Menu;
    }

    /// <summary>暂停对（Run↔Paused；卡片冻结期 Input 已被模态让出，天然不响应）。
    /// 批④：Audio.Paused 先挂起再冻结（恢复反向）——D5 显式语义，Ui 组免疫
    ///（暂停屏按钮音仍可响）。</summary>
    internal static void SetPaused(bool on)
    {
        Audio.Paused = on;
        if (on) {
            Time.Scale = 0f;
            UI.Show(GameMain.PauseDoc);
            UI.Apply();
            St = State.Paused;
        } else {
            UI.Hide(GameMain.PauseDoc);
            UI.Apply();
            Time.Scale = 1f;
            St = State.Run;
        }
    }

    // ---- UI 事件路由（GameMain.OnUiEvent 分发；Click 通道）----

    internal static void HandleUiEvent(Lemon.UiEvent e)
    {
        if (e.DocStr == GameMain.MainDoc) {
            if (e.EvStr == "start") EnterRun();
            else if (e.EvStr == "settings") OpenSettings(State.Menu);
        } else if (e.DocStr == GameMain.PauseDoc) {
            if (e.EvStr == "resume") SetPaused(false);
            else if (e.EvStr == "settings") OpenSettings(State.Paused);
            else if (e.EvStr == "tomenu") ReturnToMenu();
        } else if (e.DocStr == GameMain.SettingsDoc) {
            if (e.EvStr == "toggle-fxtext") GameMain.Settings.FxText = !GameMain.Settings.FxText;
            else if (e.EvStr == "toggle-fxbar") GameMain.Settings.FxBar = !GameMain.Settings.FxBar;
            else if (e.EvStr == "back") { CloseSettings(); return; }
            else return;
            SaveSettings(); // 开关翻转即持久化 + 刷新标签
        } else if (e.DocStr == GameMain.ResultsDoc) {
            if (e.EvStr == "restart") EnterRun();
            else if (e.EvStr == "tomenu") ReturnToMenu();
        }
    }

    /// <summary>热重载重放（DocumentReloaded——shown 态重放，隐藏态不重放防凭空
    /// 亮屏；③d-1 卡片同款契约）。设置屏重载 = 标签重灌（SaveSettings 顺带）。</summary>
    internal static void OnDocReloaded(string doc)
    {
        if (doc == GameMain.MainDoc) {
            if (St == State.Menu) { UI.Show(GameMain.MainDoc); UI.Apply(); }
        } else if (doc == GameMain.PauseDoc) {
            if (St == State.Paused) { UI.Show(GameMain.PauseDoc); UI.Apply(); }
        } else if (doc == GameMain.SettingsDoc) {
            if (St == State.Settings) { UI.Show(GameMain.SettingsDoc); UI.Apply(); }
            SaveSettings(); // DOM 重建——开关标签重灌
        } else if (doc == GameMain.ResultsDoc && St == State.Results) {
            ShowResults(resTitle, resScore, resTime, resKills, resBest);
        }
    }

    // ---- 设置（批③d-2 D3：两真实开关，Settings 档版本化 KV）----

    private static void OpenSettings(State from)
    {
        settingsFrom = from;
        UI.Show(GameMain.SettingsDoc);
        UI.Apply();
        St = State.Settings;
    }

    private static void CloseSettings()
    {
        UI.Hide(GameMain.SettingsDoc);
        UI.Apply();
        St = settingsFrom; // 底层屏（菜单实底/暂停 scrim）未动——回即见
    }

    private static void LoadSettings()
    {
        GameMain.Settings.FxText = Save.GetString("fx.text", Save.Chan.Settings) != "0";
        GameMain.Settings.FxBar = Save.GetString("fx.bar", Save.Chan.Settings) != "0";
        // M6c 批④：音量四路（vol.* int 0..100 字串，缺省 80）→ 引擎应用 + 回显
        GameMain.Settings.MasterVol = VolOf("vol.master");
        GameMain.Settings.BgmVol = VolOf("vol.bgm");
        GameMain.Settings.SfxVol = VolOf("vol.sfx");
        GameMain.Settings.UiVol = VolOf("vol.ui");
        ApplyVolumes();
        SaveSettings(); // 首开建档（version=1）+ 标签/滑条刷新
    }

    // ---- M6c 批④：音量四路（设置屏滑条；Settings 档 vol.* 持久化）----

    private static float VolOf(string key)
        => int.TryParse(Save.GetString(key, Save.Chan.Settings), out int v)
               && v >= 0 && v <= 100 ? v / 100f : 0.8f;

    /// 引擎应用（Master + 三组；staging 写当帧提交，装载前后重复调 = 幂等终态）。
    /// （Settings 为静态类——成员全限定访问，无实例别名。）
    private static void ApplyVolumes()
    {
        Audio.MasterVolume = GameMain.Settings.MasterVol;
        Audio.SetGroupVolume(AudioGroup.Bgm, GameMain.Settings.BgmVol);
        Audio.SetGroupVolume(AudioGroup.Sfx, GameMain.Settings.SfxVol);
        Audio.SetGroupVolume(AudioGroup.Ui, GameMain.Settings.UiVol);
    }

    /// 滑条值落定（key = 滑条 id；payload = "%f" 值串 0..100）。同值早退——
    /// SetAttr 回显自回环防线（RmlUi 值未变不派发，回显等值也免二次落盘）。
    internal static void OnVolumeChange(string key, string payload)
    {
        if (!float.TryParse(payload, System.Globalization.NumberStyles.Float, IC,
                            out float v)) return;
        v = Math.Clamp(v / 100f, 0f, 1f);
        if (key == "vol-master") { if (Math.Abs(v - GameMain.Settings.MasterVol) < 0.001f) return; GameMain.Settings.MasterVol = v; }
        else if (key == "vol-bgm") { if (Math.Abs(v - GameMain.Settings.BgmVol) < 0.001f) return; GameMain.Settings.BgmVol = v; }
        else if (key == "vol-sfx") { if (Math.Abs(v - GameMain.Settings.SfxVol) < 0.001f) return; GameMain.Settings.SfxVol = v; }
        else if (key == "vol-ui") { if (Math.Abs(v - GameMain.Settings.UiVol) < 0.001f) return; GameMain.Settings.UiVol = v; }
        else return;
        ApplyVolumes();
        SaveSettings(); // 落盘 + 滑条/百分数回显
    }

    private static string Pct(float v) => ((int)Math.Round(v * 100f)).ToString(IC);
    private static readonly System.Globalization.CultureInfo IC =
        System.Globalization.CultureInfo.InvariantCulture;

    private static void SaveSettings()
    {
        Save.SetString("version", "1", Save.Chan.Settings);
        Save.SetString("fx.text", GameMain.Settings.FxText ? "1" : "0", Save.Chan.Settings);
        Save.SetString("fx.bar", GameMain.Settings.FxBar ? "1" : "0", Save.Chan.Settings);
        Save.SetString("vol.master", Pct(GameMain.Settings.MasterVol), Save.Chan.Settings);
        Save.SetString("vol.bgm", Pct(GameMain.Settings.BgmVol), Save.Chan.Settings);
        Save.SetString("vol.sfx", Pct(GameMain.Settings.SfxVol), Save.Chan.Settings);
        Save.SetString("vol.ui", Pct(GameMain.Settings.UiVol), Save.Chan.Settings);
        Save.Flush();
        UI.SetText(GameMain.SettingsDoc, "btn-fxtext", GameMain.Settings.FxText ? "开" : "关");
        UI.SetText(GameMain.SettingsDoc, "btn-fxbar", GameMain.Settings.FxBar ? "开" : "关");
        // 滑条 value 属性 + 右侧百分数（隐藏态可写——装载文档 DOM 常在，③d-2 先例）
        UI.SetAttr(GameMain.SettingsDoc, "vol-master", "value", Pct(GameMain.Settings.MasterVol));
        UI.SetAttr(GameMain.SettingsDoc, "vol-bgm", "value", Pct(GameMain.Settings.BgmVol));
        UI.SetAttr(GameMain.SettingsDoc, "vol-sfx", "value", Pct(GameMain.Settings.SfxVol));
        UI.SetAttr(GameMain.SettingsDoc, "vol-ui", "value", Pct(GameMain.Settings.UiVol));
        UI.SetText(GameMain.SettingsDoc, "vol-master-val", Pct(GameMain.Settings.MasterVol));
        UI.SetText(GameMain.SettingsDoc, "vol-bgm-val", Pct(GameMain.Settings.BgmVol));
        UI.SetText(GameMain.SettingsDoc, "vol-sfx-val", Pct(GameMain.Settings.SfxVol));
        UI.SetText(GameMain.SettingsDoc, "vol-ui-val", Pct(GameMain.Settings.UiVol));
        UI.Apply();
    }

    // 热重载状态迁移（流程态 + 设置——静态随域重建必须经包走）
    protected override void OnHotReloadOut(Lemon.StateBag bag)
    {
        bag.Set("st", (int)St);
        bag.Set("from", (int)settingsFrom);
        bag.Set("fxtext", GameMain.Settings.FxText);
        bag.Set("fxbar", GameMain.Settings.FxBar);
        bag.Set("vmaster", GameMain.Settings.MasterVol); // 批④：音量四路随包
        bag.Set("vbgm", GameMain.Settings.BgmVol);
        bag.Set("vsfx", GameMain.Settings.SfxVol);
        bag.Set("vui", GameMain.Settings.UiVol);
    }

    protected override void OnHotReloadIn(Lemon.StateBag bag)
    {
        if (bag.TryGet("st", out int st)) St = (State)st;
        if (bag.TryGet("from", out int from)) settingsFrom = (State)from;
        if (bag.TryGet("fxtext", out bool ft)) GameMain.Settings.FxText = ft;
        if (bag.TryGet("fxbar", out bool fb)) GameMain.Settings.FxBar = fb;
        if (bag.TryGet("vmaster", out float vm)) GameMain.Settings.MasterVol = vm;
        if (bag.TryGet("vbgm", out float vb)) GameMain.Settings.BgmVol = vb;
        if (bag.TryGet("vsfx", out float vs)) GameMain.Settings.SfxVol = vs;
        if (bag.TryGet("vui", out float vu)) GameMain.Settings.UiVol = vu;
        ApplyVolumes(); // 引擎侧随域重建归默认——热进即回设
    }
}

/// <summary>清场批量系统（档②；GameFlow.EnterRun/ReturnToMenu 消费）：SweepArmed
/// 时按 tag 销毁 run 实体（SceneOps 命令缓冲——次帧首应用；tag 命中集 = 清场
/// 清单，新 run 内容带 Meta.tag 进集即被清；UI_*/Flow 常驻件不在集 = 天然豁免）。
/// 常驻注册 + 门控早退：非 armed 拍 C# 零工作（With&lt;Meta&gt; 枚举成本 = C++ 构块，
/// bench 红线见批文件 T9）。</summary>
public sealed class RunSweeper : IForEachSystem
{
    private static readonly string[] kRunTags = {
        "Player", "Director", "Mob", "BossMob", "Gem", "Bullet", "PierceBullet", "Blade",
    };

    public string Name => "RunSweeper";
    public Query Query => Query.With<Meta>();

    public unsafe void ForEach(ref readonly Chunk chunk)
    {
        if (!GameFlow.SweepArmed) return;
        var meta = chunk.Span<Meta>();
        for (int i = 0; i < chunk.Length; ++i) {
            if (TagIs(ref meta[i], kRunTags)) SceneOps.Destroy(chunk.Entities[i]);
        }
        GameFlow.SweepObserved = true; // 握手位（GameFlow 观察后清对——本批全块扫完）
    }

    private static unsafe bool TagIs(ref Meta m, string[] tags)
    {
        foreach (string t in tags) {
            fixed (byte* p = m.Tag) {
                bool same = true;
                for (int i = 0; i < t.Length && i < 24; ++i)
                    if (p[i] != (byte)t[i]) { same = false; break; }
                if (same && (t.Length >= 24 || p[t.Length] == 0)) return true;
            }
        }
        return false;
    }
}
)CS";
    }
    {
        std::ofstream f(game / "PlayerMovement.cs", std::ios::trunc);
        f << R"CS(using Lemon;
using Lemon.Interop;

/// <summary>移动（M6a 批⓪ T4 拆分）：8 向 + 软竞技场钳制；死亡相位停走。</summary>
public sealed class PlayerMovement : LemonBehaviour
{
    private const float kArenaHalf = 1000f; // 软竞技场边界（脚本层钳制）

    protected override void Update()
    {
        if (GameMain.Run.Dead) return;
        var tf = gameObject.GetComponent<Transform2D>();
        var stats = gameObject.GetComponent<Stats>();
        Vec2 axis = Input.Axis;
        tf.Pos = new Vec2(
            System.Math.Clamp(tf.Pos.X + axis.X * stats.MoveSpeed * Time.DeltaTime,
                              -kArenaHalf, kArenaHalf),
            System.Math.Clamp(tf.Pos.Y + axis.Y * stats.MoveSpeed * Time.DeltaTime,
                              -kArenaHalf, kArenaHalf));
        gameObject.SetComponent(tf);
    }
}
)CS";
    }
    {
        std::ofstream f(game / "PlayerCombat.cs", std::ios::trunc);
        f << R"CS(using System;
using System.Collections.Generic;
using Lemon;
using Lemon.Interop;

/// <summary>战斗（M6a 批⓪ T4 拆分）：击杀/宝石掉落 + 环绕刃自愈 + 升级三选一
///（固定序轮换，零 RNG）+ 死亡结算/复活（死亡对话框点击）+ 受击表现（批①：
/// Anim 受击段 Play+Queue 回行走 + Fx 飘字/世界血条）。一局共享态写
/// GameMain.Run（HUD 读）。
/// 批③d-1：三选一/死亡对话框迁 .rml 文档（GameMain.ShowCardsDoc——UI.Click
/// 事件消费式回读，数字键通道退役）。
/// 批② T4 数值表化（ADR-012）：升级池/武器参数读 Assets/tables/upgrades.tab +
/// weapons.tab（列头即列契约；缺表 = 空池 + warn——三选一不弹 = 与"无升级"
/// 语义一致，模板永不因表缺炸 Play）；XP 曲线系数读 balance.tab 写
/// Lemon.Balance（World 级，缺省 = 引擎默认 1.25）。</summary>
public sealed class PlayerCombat : LemonBehaviour
{
    // 模板资产 GUID（Templates/vs-survivor 生成期固定——引用锚点，勿改）
    private const string kGemPrefab = "7e57100000000005";
    // M6c 批④：事件音四件（Assets/Audio/；命中高频小音量——引擎重触发节流兜底）
    private const string kSfxHit = "7e57400000000002";     // 怪受击
    private const string kSfxKill = "7e57400000000003";    // 击杀
    private const string kSfxPickup = "7e57400000000004";  // 宝石拾取
    private const string kSfxLevelUp = "7e57400000000005"; // 升级
    // 数值表 GUID（Assets/tables/；生成期固定，PlayerCombat 读）
    private const string kWeaponsTable = "7e57200000100001";
    private const string kUpgradesTable = "7e57200000100002";
    private const string kBalanceTable = "7e57200000100003";

    // 批①受击段 clip（Anim.ClipId = GUID 低 32 位自算；Assets/monster-hit.anim）
    private static readonly uint kMobWalk = Anim.ClipId("5bd31a7c20000002");
    private static readonly uint kMobHit = Anim.ClipId("5bd31a7c20000004");

    // ---- 批② T4 表载缓存（Start 一次载入；Play 中改表下一局生效——快照语义）----
    private sealed class WeaponRow
    {
        public string Id = "", Prefab = "";
        public float Interval, Speed, Count, Radius;
    }
    private sealed class UpgradeRow
    {
        public string Id = "", Label = "", Value = "";
        public int Kind;
    }
    private readonly List<WeaponRow> _weapons = new();   // weapons.tab 行缓存
    private readonly List<UpgradeRow> _upgrades = new(); // 升级池（空 = 三选一不弹）
    private string _bladePrefab = "7e57100000000006"; // Blade.prefab（blade.prefabGuid）
    private float _bladeSpeed = 2.2f; // 环绕角速 rad/s（blade.speed；缺表 = 原硬编码值）
    private float _bladeRadius = 90f; // 环绕轨道半径 px（blade.radius）

    private int _pendingLevels; // LevelUp 事件累计的待选次数
    private bool _cardsShown;
    private int _pickRotation;  // 三选一轮换序（确定性）
    private int _bladeCount = 2;
    private readonly List<ulong> _blades = new();
    private float _bladeAngle;

    public PlayerCombat()
    {
        // Subscribe 助手（M15）：实例销毁自动退订（裸 Events.Subscribe 只增不删，
        // 死亡→复活重挂会逐局累积订阅）
        Subscribe(GameEvent.LevelUp, m => {
            if (m.Src.Id == gameObject.Entity.Id) {
                ++_pendingLevels;
                Audio.PlayOneShot(kSfxLevelUp, 0.7f); // M6c 批④：升级音
            }
        });
        Subscribe(GameEvent.Death, OnDeath);
        Subscribe(GameEvent.Pickup, _ => Audio.PlayOneShot(kSfxPickup, 0.5f)); // 批④：宝石拾取音
        // 批①受击表现：怪受击 = 受击段（Play+Queue 播完回行走）+ 伤害飘字 + 世界
        // 血条；玩家受击 = 世界血条刷新（常显——每击续命，HUD 文字条仍是权威）
        Subscribe(GameEvent.Hit, OnHit);
    }

    private void OnHit(GameEventMsg m)
    {
        var victim = GameObject.From(m.Dst);
        if (!victim.Alive || !victim.TryGetComponent<Meta>(out var meta)) return;
        if (meta.Team == 1) { // 怪受击
            Audio.PlayOneShot(kSfxHit, 0.45f); // M6c 批④：受击音（高命中率小音量）
            Anim.Play(victim, kMobHit, false); // 受击段立即打断
            Anim.Queue(victim, kMobWalk);      // 播完（0.1667s）自动回行走
            // 批③d-2 D3：飘字/血条 = 设置开关门控（Settings 档持久化，即时生效）
            if (GameMain.Settings.FxText &&
                victim.TryGetComponent<Transform2D>(out var tf))
                Fx.Text(m.P0, new Vec2(tf.Pos.X - 4f, tf.Pos.Y - 10f), 0xFF5060F0u); // 暖红（RGBA）
            if (GameMain.Settings.FxBar &&
                victim.TryGetComponent<Health>(out var hp))
                Fx.Bar(victim, hp.Cur / hp.Max, 0xFF30B0F0u, 24f);
        } else if (m.Dst.Id == gameObject.Entity.Id) { // 玩家受击
            if (GameMain.Settings.FxBar &&
                gameObject.TryGetComponent<Health>(out var hp))
                Fx.Bar(gameObject, hp.Cur / hp.Max, 0xFF60D060u, 32f);
        }
    }

    private void OnDeath(GameEventMsg m)
    {
        var src = GameObject.From(m.Src);
        if (!src.Alive || !src.TryGetComponent<Meta>(out var meta)) return;
        if (meta.Team == 1) {
            ++GameMain.Run.Kills;
            Audio.PlayOneShot(kSfxKill, 0.6f); // M6c 批④：击杀音
            if (src.TryGetComponent<Transform2D>(out var tf)) // 两阶段销毁：当帧可读
                Instantiate.Prefab(kGemPrefab, new Vec2(tf.Pos.X, tf.Pos.Y));
        } else if (m.Src.Id == gameObject.Entity.Id) {
            Die();
        }
    }

    protected override void Start()
    {
        LoadTables();
        GameMain.Run.Best =
            int.TryParse(Save.GetString("vs.best", Save.Chan.Meta), out var b) ? b : 0; // 上一局纪录（跨局归 meta 档）
        // 批③d-1：跨局归位对齐——EnterPlay 大扫除已 Hide 上局 stale 卡片文档，
        // 静态标志此处同步清（待选槽清空防上局残事件复活）
        GameMain.CardsShown = false;
        GameMain.CardPickPending = null;
    }

    // ---- 批② T4 表载（ADR-012 D1 全字符串格；坏行跳过 + warn、缺表保底——
    // 数值与生成器写表前硬编码一致 = 行为等价变换）----

    private void LoadTables()
    {
        if (Table.Has(kWeaponsTable)) {
            int cId = ColOf(kWeaponsTable, "id"), cGuid = ColOf(kWeaponsTable, "prefabGuid"),
                cInt = ColOf(kWeaponsTable, "interval"), cSpd = ColOf(kWeaponsTable, "speed"),
                cCnt = ColOf(kWeaponsTable, "count"), cRad = ColOf(kWeaponsTable, "radius");
            for (int r = 1; r < Table.Rows(kWeaponsTable); ++r) {
                string? id = At(kWeaponsTable, r, cId);
                if (string.IsNullOrEmpty(id)) { WarnBadRow(kWeaponsTable, r); continue; }
                _weapons.Add(new WeaponRow {
                    Id = id!, Prefab = At(kWeaponsTable, r, cGuid) ?? "",
                    Interval = F(kWeaponsTable, r, cInt), Speed = F(kWeaponsTable, r, cSpd),
                    Count = F(kWeaponsTable, r, cCnt), Radius = F(kWeaponsTable, r, cRad),
                });
            }
            if (ById(_weapons, "blade") is { } blade) { // 环绕参数行
                if (blade.Prefab.Length == 16) _bladePrefab = blade.Prefab;
                if (blade.Speed > 0f) _bladeSpeed = blade.Speed;
                if (blade.Radius > 0f) _bladeRadius = blade.Radius;
                if (blade.Count > 0f) _bladeCount = (int)blade.Count;
            }
            if (ById(_weapons, "shoot") is { Interval: > 0f } shoot) { // 初始射速写 Shooter
                var sh = gameObject.GetComponent<Shooter>();
                sh.Interval = shoot.Interval;
                gameObject.SetComponent(sh);
            }
        } else {
            Console.Error.WriteLine("[lemon][warn] PlayerCombat：weapons.tab 缺失"
                                    + "——环绕刃用保底参数（90px / 2.2rad/s），换弹种无效");
        }
        if (Table.Has(kUpgradesTable)) {
            int cId = ColOf(kUpgradesTable, "id"), cLabel = ColOf(kUpgradesTable, "label"),
                cKind = ColOf(kUpgradesTable, "kind"), cVal = ColOf(kUpgradesTable, "value");
            for (int r = 1; r < Table.Rows(kUpgradesTable); ++r) {
                string? label = At(kUpgradesTable, r, cLabel);
                if (string.IsNullOrEmpty(label) || cKind < 0) {
                    WarnBadRow(kUpgradesTable, r);
                    continue;
                }
                _upgrades.Add(new UpgradeRow {
                    Id = At(kUpgradesTable, r, cId) ?? "", Label = label!,
                    Kind = Table.Int(kUpgradesTable, r, cKind),
                    Value = At(kUpgradesTable, r, cVal) ?? "",
                });
            }
        } else {
            Console.Error.WriteLine("[lemon][warn] PlayerCombat：upgrades.tab 缺失"
                                    + "——升级池为空（三选一不弹 = 与\"无升级\"语义一致）");
        }
        if (Table.Has(kBalanceTable)) { // XP 曲线（缺表/缺行/坏值 = 引擎默认 1.25，静默）
            int cId = ColOf(kBalanceTable, "id"), cVal = ColOf(kBalanceTable, "value");
            if (cId >= 0 && cVal >= 0)
                for (int r = 1; r < Table.Rows(kBalanceTable); ++r)
                    if (At(kBalanceTable, r, cId) == "xpCurveK") {
                        float v = Table.Float(kBalanceTable, r, cVal);
                        if (v > 0f) Balance.XpCurveK = v;
                        break;
                    }
        }
    }

    private static int ColOf(string table, string name) // 列头名 → 索引（-1 = 无此列）
    {
        for (int c = 0; c < Table.Cols(table); ++c)
            if (Table.Str(table, 0, c) == name) return c;
        return -1;
    }
    private static string? At(string table, int row, int col)
        => col >= 0 ? Table.Str(table, row, col) : null;
    private static float F(string table, int row, int col)
    {
        if (col < 0) return 0f;
        string? s = Table.Str(table, row, col);
        if (string.IsNullOrEmpty(s)) return 0f; // 空格 = 未配置（宽表留空常态），免 warn
        return Table.Float(table, row, col);
    }
    private static void WarnBadRow(string table, int row)
        => Console.Error.WriteLine($"[lemon][warn] PlayerCombat：{table} 第 {row} 行坏——跳过");
    private static WeaponRow? ById(List<WeaponRow> list, string id)
    {
        foreach (var w in list)
            if (w.Id == id) return w;
        return null;
    }
    /// 倍率容错：非正值/坏格式 = 保底原值（表值写坏不把数值清零）。
    private static float MulVal(string s, float fallback)
        => float.TryParse(s, System.Globalization.CultureInfo.InvariantCulture,
                          out var v) && v > 0f ? v : fallback;
    private static float AddVal(string s, float fallback)
        => float.TryParse(s, System.Globalization.CultureInfo.InvariantCulture,
                          out var v) && v != 0f ? v : fallback;
    /// 16 位 GUID hex → 低 32 位（引擎 prefabId 口径；WaveTableLoader 同款）。
    private static uint GuidLow32(string hex)
        => hex.Length == 16 ? (uint)Convert.ToUInt64(hex, 16) : 0u;

    protected override void Update()
    {
        if (GameMain.Run.Dead) {
            // 死亡对话框：点击"复活"卡 → UI.Click 事件（key = "cards/ok"）复活。
            // 消费式回读（读后即清）与升级卡片同通道；在途升级选择一并丢弃
            // （Die 已弃置在途卡）；数字键通道已退役（批③d-1 设计定案 5）
            string? pending = GameMain.CardPickPending;
            GameMain.CardPickPending = null;
            if (pending == "cards/ok") Revive();
            return;
        }
        GameMain.Run.Time += Time.DeltaTime;

        UpdateBlades(gameObject.GetComponent<Transform2D>());
        UpdateCards();
    }

    private void UpdateBlades(Transform2D playerTf)
    {
        _bladeAngle += _bladeSpeed * Time.DeltaTime;
        while (_blades.Count < _bladeCount) { // 自愈：热重装/丢失即补（挂玩家当前位置）
            var g = Instantiate.Prefab(_bladePrefab,
                                       new Vec2(playerTf.Pos.X, playerTf.Pos.Y));
            _blades.Add(g.Entity.Id);
        }
        _blades.RemoveAll(id => !GameObject.From(new EntityHandle { Id = id }).Alive);
        for (int i = 0; i < _blades.Count; ++i) {
            var b = GameObject.From(new EntityHandle { Id = _blades[i] });
            if (!b.TryGetComponent<Transform2D>(out var bt)) continue;
            float a = _bladeAngle + i * (6.2831853f / _blades.Count);
            bt.Pos = new Vec2(playerTf.Pos.X + _bladeRadius * System.MathF.Cos(a),
                              playerTf.Pos.Y + _bladeRadius * System.MathF.Sin(a));
            bt.Rot = a;
            b.SetComponent(bt);
        }
    }

    private void UpdateCards()
    {
        if (_cardsShown) {
            string? pick = GameMain.CardPickPending; // "cards/<升级 id>"（UI.Click）
            GameMain.CardPickPending = null;         // 消费式：同一选择只报一次
            if (pick == null) return;
            if (pick.StartsWith("cards/")) pick = pick.Substring("cards/".Length);
            int idx = _upgrades.FindIndex(u => u.Id == pick); // 条目 key = 升级行 id
            if (idx < 0) return; // 非本池 key（对话框在途等）——忽略，不误吞升级轮次
            ApplyOption(idx);
            --_pendingLevels;
            _cardsShown = false;
            GameMain.HideCardsDoc();
            if (_pendingLevels <= 0) Time.Scale = 1f; // 选完恢复（多级连选继续冻结）
            return;
        }
        if (_pendingLevels > 0 && _upgrades.Count > 0) { // 空池不弹也不冻结（缺表语义）
            Time.Scale = 0f; // 卡片期间冻结（RNG 不消耗，批① D5 语义）
            int n = _pickRotation++;
            int m = _upgrades.Count;
            GameMain.ShowCardsDoc("升级！三选一", new List<UiItem> {
                new() { Key = _upgrades[n % m].Id,       Fields = { ["label"] = _upgrades[n % m].Label } },
                new() { Key = _upgrades[(n + 2) % m].Id, Fields = { ["label"] = _upgrades[(n + 2) % m].Label } },
                new() { Key = _upgrades[(n + 4) % m].Id, Fields = { ["label"] = _upgrades[(n + 4) % m].Label } },
            });
            _cardsShown = true;
        }
    }

    /// 升级应用（批② T4：switch → upgrades.tab kind 派发；数值/弹种全表读，
    /// 语义与原硬编码逐项等价）。kind：0 移速 / 1 磁力 / 2 射速 / 3 换弹种
    /// （value = weapons 行 id）/ 4 生命上限 / 5 环绕+1。
    private void ApplyOption(int o)
    {
        if (o < 0 || o >= _upgrades.Count) return;
        var up = _upgrades[o];
        switch (up.Kind) {
        case 0: { // 移速（value = 倍率）
            var st = gameObject.GetComponent<Stats>();
            st.MoveSpeed *= MulVal(up.Value, 1.10f);
            gameObject.SetComponent(st);
            break;
        }
        case 1: { // 磁力（value = 倍率）
            var st = gameObject.GetComponent<Stats>();
            st.PickupRadius *= MulVal(up.Value, 1.25f);
            gameObject.SetComponent(st);
            break;
        }
        case 2: { // 射速（value = 倍率，Interval ×）
            var sh = gameObject.GetComponent<Shooter>();
            sh.Interval = System.Math.Max(0.05f, sh.Interval * MulVal(up.Value, 0.85f));
            gameObject.SetComponent(sh);
            break;
        }
        case 3: { // 换弹种（value = weapons 行 id → ProjectileId）
            var w = ById(_weapons, up.Value);
            var sh = gameObject.GetComponent<Shooter>();
            if (w != null && w.Prefab.Length == 16) {
                sh.ProjectileId = GuidLow32(w.Prefab);
                gameObject.SetComponent(sh);
            } else {
                Console.Error.WriteLine("[lemon][warn] PlayerCombat：换弹种 '" + up.Value
                                        + "' 无 weapons 行/prefab guid——保底不改");
            }
            break;
        }
        case 4: { // 生命上限（value = 点数）
            var hp = gameObject.GetComponent<Health>();
            float add = AddVal(up.Value, 25f);
            hp.Max += add;
            hp.Cur += add;
            gameObject.SetComponent(hp);
            break;
        }
        case 5: ++_bladeCount; break; // 环绕 +1（UpdateBlades 自愈补挂）
        }
    }

    private void Die()
    {
        if (GameMain.Run.Dead) return; // 多源 Death（弹道/区域）只结算一次
        GameMain.Run.Dead = true;
        _cardsShown = false; // 弃置在途升级卡（_pendingLevels 保留，复活后重弹）
        Time.Scale = 0f;
        int score = GameMain.Run.Kills * 10 + (int)GameMain.Run.Time;
        bool newBest = score > GameMain.Run.Best;
        if (newBest) {
            GameMain.Run.Best = score;
            Save.SetString("vs.best", score.ToString(), Save.Chan.Meta);
            Save.Flush(); // 立即落盘（ExitPlay 兜底之外的显式路径；全档）
        }
        // 批③d-2 D1：死亡策略归游戏侧——本模板示例 = 每局一次复活（ReviveUsed），
        // 二死进结算屏（GameFlow 只提供原语；改复活道具/表驱动只动本分叉）
        if (!GameMain.Run.ReviveUsed) {
            GameMain.Run.ReviveUsed = true;
            string title = newBest ? $"★ 新纪录 {score} 分！"
                                   : $"本局 {score} 分（最高 {GameMain.Run.Best}）";
            // 批③d-1：死亡对话框 = 卡片文档单条形态（key "ok" → "cards/ok" 事件回传）
            GameMain.ShowCardsDoc(title, new List<UiItem> {
                new() { Key = "ok", Fields = { ["label"] = "复活" } },
            });
        } else {
            int sec = (int)GameMain.Run.Time;
            GameFlow.ShowResults(newBest ? $"★ 新纪录 {score} 分！" : "本局结束",
                                 score.ToString(),
                                 $"{sec / 60:D2}:{sec % 60:D2}",
                                 GameMain.Run.Kills.ToString(),
                                 GameMain.Run.Best.ToString());
        }
    }

    private void Revive()
    {
        GameMain.Run.Dead = false;
        var hp = gameObject.GetComponent<Health>();
        hp.Cur = hp.Max;
        hp.IFrames = 2f; // 复活无敌 2s（StatSystem 递减）
        gameObject.SetComponent(hp);
        Time.Scale = 1f;
        GameMain.HideCardsDoc();
    }

    // 热重载状态迁移（数值面，含 GameMain.Run 共享态——静态随域重建必须经包走；
    // 刃实体经 UpdateBlades 自愈重建轨道）
    protected override void OnHotReloadOut(StateBag bag)
    {
        bag.Set("time", GameMain.Run.Time);
        bag.Set("kills", GameMain.Run.Kills);
        bag.Set("best", GameMain.Run.Best);
        bag.Set("dead", GameMain.Run.Dead);
        bag.Set("revive", GameMain.Run.ReviveUsed);
        bag.Set("pending", _pendingLevels);
        bag.Set("rotation", _pickRotation);
        bag.Set("blades", _bladeCount);
    }

    protected override void OnHotReloadIn(StateBag bag)
    {
        if (bag.TryGet("time", out float t)) GameMain.Run.Time = t;
        if (bag.TryGet("kills", out int k)) GameMain.Run.Kills = k;
        if (bag.TryGet("best", out int b)) GameMain.Run.Best = b;
        if (bag.TryGet("dead", out bool d)) GameMain.Run.Dead = d;
        if (bag.TryGet("revive", out bool rv)) GameMain.Run.ReviveUsed = rv;
        if (bag.TryGet("pending", out int p)) _pendingLevels = p;
        if (bag.TryGet("rotation", out int r)) _pickRotation = r;
        if (bag.TryGet("blades", out int n)) _bladeCount = n;
    }
}
)CS";
    }
    {
        std::ofstream f(game / "PlayerHud.cs", std::ios::trunc);
        f << R"CS(using Lemon;
using Lemon.Interop;

/// <summary>HUD（M6b 批③d-1 文档化；T8 后修② 条改原生 progress）：Assets/UI/
/// hud.rml 六行 + 血/经双进度条，UI.SetText/SetAttr 每帧一次批量提交（UI.Apply
/// ——M2 单一口）。进度条 = RmlUi 原生 &lt;progress&gt;：value/max 走属性通道（语义
/// 数据非样式，C# 不做百分比数学）；fill 引擎定位，免布局坑。配色/字号/间距全在
/// theme.rcss token（换肤 = 改 token 不改代码）；RtUi 通道（Lemon.Ui）本屏退役
/// ——RmlUi 文档即编辑器 GameView 与 M8 打包的同一呈现者。读 GameMain.Run 共享态；
/// 死亡相位冻结末帧（结算标题由战斗侧写入卡片屏）。</summary>
public sealed class PlayerHud : LemonBehaviour
{
    private const string kDoc = "Assets/UI/hud.rml";
    // M6c 批④：波次横幅音（Assets/Audio/wave.ogg）
    private const string kSfxWave = "7e57400000000006";

    public PlayerHud()
    {
        Subscribe(GameEvent.WaveStart, m => {
            UI.SetText(kDoc, "wave", $"—— 第 {(int)m.P0 + 1} 波 ——");
            Audio.PlayOneShot(kSfxWave, 0.6f); // M6c 批④：波次横幅音
            UI.Apply();
        });
    }

    protected override void Update()
    {
        if (GameMain.Run.Dead) return;
        var hp = gameObject.GetComponent<Health>();
        var xp = gameObject.GetComponent<XpProgress>();
        UI.SetText(kDoc, "hp-text", $"HP {(int)hp.Cur}/{(int)hp.Max}");
        Bar(kDoc, "hp-bar", hp.Cur, hp.Max);
        UI.SetText(kDoc, "xp-text", $"LV {xp.Level} {(int)xp.Xp}/{(int)xp.XpToNext}");
        Bar(kDoc, "xp-bar", xp.Xp, xp.XpToNext);
        int t = (int)GameMain.Run.Time;
        UI.SetText(kDoc, "time", $"{t / 60}:{t % 60:00}");
        UI.SetText(kDoc, "kills", $"击杀 {GameMain.Run.Kills}");
        UI.SetText(kDoc, "best", $"最高纪录 {GameMain.Run.Best}");
        UI.Apply();
    }

    /// 原生 progress 驱动（value/max 属性对；max 每帧同写——升级换挡零特判）。
    /// (int) 舍入与文本行同口径（"0" 格式会四舍五入 → 两行数字不一致）。
    private static void Bar(string doc, string id, float cur, float max) {
        UI.SetAttr(doc, id, "value", ((int)cur).ToString(IC));
        UI.SetAttr(doc, id, "max", ((int)max).ToString(IC));
    }
    private static readonly System.Globalization.CultureInfo IC =
        System.Globalization.CultureInfo.InvariantCulture;
}
)CS";
    }
}

// --smoke-guid（M6a 批⓪ T5）：sprite 引用 GUID 稳定性链。三难并发——导入新图
//（字典序插队 = 全体 spriteId 重排）+ 资产改名（.meta 随行）+ 删 manifest
//（跨进程重开 = 无记账 fresh 分配）→ 重开后逐实体断言 guid 不断链、spriteId
// 归一到新号、零悬空、改名资产引用存活。T2 单测（TestSpriteGuidResolve）的
// 编辑器全链复证；纯资产/场景链（不编译脚本，独立 EditorContext 模拟重开进程）。
bool RunGuidSmokeChain(uint32_t spriteIdBase) {
    namespace fs = std::filesystem;
    using ecs::Entity;
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path() /
                         ("lemon-smoke-guid-" + std::to_string(lemon::CurrentProcessId()));
    fs::remove_all(tmp, ec);
    ProjectDesc d;
    d.parentDir = tmp.string();
    d.name = "GuidSmoke";
    d.engineVersion = "0.5.0-m5";
    d.templateName = "vs-survivor";
    d.templateDir = std::string(LEMON_TEMPLATE_DIR) + "/vs-survivor";
    const std::string root = ProjectWizard::Create(d);
    if (root.empty()) {
        LEMON_ERROR("smoke-guid：向导复制失败（模板缺失/不可写）");
        return false;
    }

    // 局1：开项目 + 场景，编辑态实例化六 prefab（T2 的 Instantiate 解析路径）
    // → 存档 → 记基线（tag → guid/id）
    EditorContext ctx;
    if (!ctx.Assets().OpenProject(root, spriteIdBase)) {
        LEMON_ERROR("smoke-guid：open project 失败");
        return false;
    }
    if (!ctx.OpenScene(root + "/Scenes/Main.scene")) {
        LEMON_ERROR("smoke-guid：open scene 失败");
        return false;
    }
    struct Row {
        std::string tag;
        uint64_t guid;
        uint32_t id;
    };
    auto collect = [](EditorContext& c, std::vector<Row>& out) {
        out.clear();
        c.EditScene().Each([&](Entity e) {
            if (const ecs::SpriteRenderer* sr = c.EditScene().TryGet<ecs::SpriteRenderer>(e))
                out.push_back(
                    {std::string(c.EditScene().Get<ecs::Meta>(e).tag), sr->spriteGuid,
                     sr->spriteId});
        });
    };
    const Vec2 spawnAt[7] = {{-300, 0}, {300, 0}, {0, -300},
                             {0, 300},  {-300, -300}, {300, 300}, {0, 0}};
    const uint64_t prefabGuids[7] = {kMobPf,   kBossPf, kBulletPf,
                                     kPiercePf, kGemPf, kBladePf,
                                     kPlayerPf}; // 批③d-2：Player 迁 prefab——第 7 根
    for (int i = 0; i < 7; ++i)
        if (ctx.InstantiatePrefabAsset(prefabGuids[i], spawnAt[i]).IsNull()) {
            LEMON_ERROR("smoke-guid：prefab %d 实例化失败", i);
            return false;
        }
    if (!ctx.SaveScene()) {
        LEMON_ERROR("smoke-guid：基线存档失败");
        return false;
    }
    std::vector<Row> base;
    collect(ctx, base);
    if (base.size() != 7) { // 七 prefab 根（批③d-2：Player 自场景迁 prefab）
        LEMON_ERROR("smoke-guid：基线实体数 %zu ≠ 7", base.size());
        return false;
    }
    for (const Row& r : base)
        if (r.guid == 0) {
            LEMON_ERROR("smoke-guid：基线实体 '%s' 无 spriteGuid（生成器未双写？）",
                        r.tag.c_str());
            return false;
        }

    // 三难并发：插队导入（字典序最前 → 全体 spriteId 后移）+ hero 表改名
    //（.meta 随行 = guid 存活）+ 删 manifest（重开 = 无记账 fresh 分配）
    {
        std::ofstream f(root + "/Assets/aaa_insert.png", std::ios::binary);
        f << "png-insert"; // DB 只哈希不解码，内容任意
    }
    fs::rename(root + "/Assets/dungeon_hero_1.png", root + "/Assets/hero_renamed.png", ec);
    fs::rename(root + "/Assets/dungeon_hero_1.png.meta", root + "/Assets/hero_renamed.png.meta",
               ec);
    fs::remove(root + "/.lemon/manifest.json", ec);

    // 局2：新 EditorContext = 模拟重开进程（DB 空表 + 无 manifest = 全体重排）
    EditorContext ctx2;
    if (!ctx2.Assets().OpenProject(root, spriteIdBase)) {
        LEMON_ERROR("smoke-guid：reopen project 失败");
        return false;
    }
    if (!ctx2.OpenScene(root + "/Scenes/Main.scene")) {
        LEMON_ERROR("smoke-guid：reopen scene 失败");
        return false;
    }
    std::vector<Row> after;
    collect(ctx2, after);
    if (after.size() != base.size()) {
        LEMON_ERROR("smoke-guid：重开实体数 %zu ≠ %zu", after.size(), base.size());
        return false;
    }
    int drifted = 0, guidBroken = 0, idWrong = 0, dangling = 0;
    for (const Row& b : base) {
        const Row* a = nullptr;
        for (const Row& r : after)
            if (r.tag == b.tag) a = &r;
        if (!a) {
            ++guidBroken;
            continue;
        }
        if (a->guid != b.guid) ++guidBroken;
        const AssetEntry* en = ctx2.Assets().FindByGuid(b.guid);
        if (!en || en->missing) {
            ++dangling;
            continue;
        }
        const uint32_t expect = en->sliceCount > 0 ? en->sliceBase : en->spriteId;
        if (a->id != expect) {
            ++idWrong;
            LEMON_WARN("smoke-guid row '%s': id %u → %u (expect %u, %s slices=%u base=%u)",
                       b.tag.c_str(), b.id, a->id, expect, en->relPath.c_str(),
                       en->sliceCount, en->sliceBase);
        }
        if (a->id != b.id) ++drifted; // 漂移确证（链非空转）
    }
    // 改名资产存活：guid → 新路径（引用不断链的直接证据）
    const AssetEntry* hero = ctx2.Assets().FindByGuid(kHeroSheet);
    const bool renamedOk =
        hero && hero->relPath == "Assets/hero_renamed.png" && !hero->missing;
    if (guidBroken || idWrong || dangling || !renamedOk || drifted < 3) {
        LEMON_ERROR("smoke-guid：guidBroken=%d idWrong=%d dangling=%d renamed=%d "
                    "drifted=%d（重排未生效 = 链空转）",
                    guidBroken, idWrong, dangling, renamedOk ? 1 : 0, drifted);
        return false;
    }
    std::printf("[lemon] smoke-guid: entities=%zu drift=%d/7 renamed=OK dangling=0 => OK\n",
                after.size(), drifted);
    fs::remove_all(tmp, ec);
    return true;
}
} // namespace vs_template

/// --gen-vs-template <dir>：产出 vs-survivor 模板项目文件（跑一次、入库）。
/// 结构 = 完整项目：yami 素材 + 程序化小图（.meta 固定 guid）+ Prefabs（固定 guid
/// 占位 meta → 扫描后覆写内容）+ Game/ 脚本 + Main.scene（玩家/导演 + 波表）。
bool GenerateVsTemplate(EditorContext& ctx, uint32_t spriteIdBase,
                        const std::string& outRoot) {
    namespace fs = std::filesystem;
    using namespace vs_template;
    std::error_code ec;
    const fs::path root = fs::absolute(outRoot, ec);
    // 2026-09-24 审查 F-02：remove_all 前先验目标"是本生成器产物"——仅认领含
    // project.lemon + Game/ 的 lemon 工程；任意已有目录一律拒绝删除。重新生成
    // （Templates/vs-survivor 入库目录满足标记）不受影响。
    if (fs::exists(root, ec)) {
        const bool lemonProject = fs::is_regular_file(root / "project.lemon", ec) &&
                                  fs::is_directory(root / "Game", ec);
        if (!lemonProject) {
            LEMON_ERROR("gen-vs-template：目标已存在且不是 lemon 工程（缺 project.lemon/"
                        "Game/ 标记），拒绝删除：%s",
                        root.string().c_str());
            return false;
        }
        fs::remove_all(root, ec);
    }
    for (const char* dir : {"Assets", "Assets/tables", "Assets/Audio", "Prefabs",
                            "Scenes", "Game", "Data", ".lemon/editor"})
        fs::create_directories(root / dir, ec);

    // 1) yami 素材拷贝（批③入库包 → 模板自带；.meta 随行 = guid/切片稳定）
    const fs::path yamiSrc = fs::path(LEMON_TEMPLATE_DIR).parent_path() /
                             "Samples/Assets/yami-dungeon";
    if (!fs::is_directory(yamiSrc, ec)) {
        LEMON_ERROR("gen-vs-template：yami 素材包缺失 %s", yamiSrc.string().c_str());
        return false;
    }
    for (const char* f : {"dungeon_hero_1.png", "dungeon_hero_1.png.meta",
                          "dungeon_monster_2.png", "dungeon_monster_2.png.meta",
                          "dungeon_boss_1.png", "dungeon_boss_1.png.meta",
                          "hero-walk.anim", "hero-walk.anim.meta",
                          "monster-walk.anim", "monster-walk.anim.meta"})
        fs::copy(yamiSrc / f, root / "Assets" / f, fs::copy_options::overwrite_existing, ec);
    WriteHitClips(root / "Assets"); // 批①受击段（模板自带，PlayerCombat 受击切段用）
    WriteTableAssets(root / "Assets"); // 批② T4 数值三表（weapons/upgrades/balance）
    WriteUiAssets(root / "Assets"); // 批③d-1：UI 三资产（theme/hud/cards——样板双屏）
    if (!WriteAudioAssets(root / "Assets")) // M6c 批④：CC0 音频七件（缺件红字断生成）
        return false;

    // 2) 程序化小图 + Game/ 脚本工程 + prefab 占位（固定 guid meta 先行——
    //    OpenProject 扫描按 meta 记账，之后覆写 .prefab 内容 guid 不动）
    WriteProceduralAssets(root / "Assets");
#ifdef LEMON_SCRIPT_DIR
    WriteGameSources(root / "Game", LEMON_SCRIPT_DIR);
#endif
    struct Pf { const char* file; uint64_t guid; };
    for (const Pf& pf : {Pf{"Mob.prefab", kMobPf}, Pf{"BossMob.prefab", kBossPf},
                         Pf{"Bullet.prefab", kBulletPf}, Pf{"PierceBullet.prefab", kPiercePf},
                         Pf{"Gem.prefab", kGemPf}, Pf{"Blade.prefab", kBladePf},
                         Pf{"Player.prefab", kPlayerPf},     // 批③d-2：流程双 prefab
                         Pf{"Director.prefab", kDirectorPf}}) {
        {
            std::ofstream f(root / "Prefabs" / pf.file, std::ios::trunc);
            f << "{\"entities\":[],\"name\":\"tpl\",\"schemaVersion\":1}\n"; // 占位（后覆写）
        }
        std::ofstream m(fs::path((root / "Prefabs" / pf.file).string() + ".meta"),
                        std::ios::trunc);
        m << "{\n  \"guid\": \"" << AssetDatabase::GuidToHex(pf.guid)
          << "\",\n  \"type\": \"prefab\",\n  \"hash\": 0,\n  \"importedAt\": 0\n}\n";
    }
    {
        std::ofstream f(root / "project.lemon", std::ios::trunc);
        f << "{\n  \"schemaVersion\": 1,\n  \"name\": \"vs-survivor\",\n"
             "  \"engineVersion\": \"0.5.0-m5\",\n  \"guid\": \"tpl-placeholder\"\n}\n";
    }
    {
        std::ofstream f(root / "README.md", std::ios::trunc);
        f << "# vs-survivor 模板（M5 批④）\n\n"
             "吸血鬼幸存者式开局模板：8 向移动 + 直射弹（可升级穿透）+ 环绕刃 +\n"
             "经验宝石磁吸 + 升级三选一（数字键 1/2/3 或点击卡片）+ 16 波导演\n"
             "（t=565s Boss 波）+ HUD 四要素 + 死亡结算/最高分存档（死亡对话框点击复活）。\n\n"
             "由 `lemon-editor --gen-vs-template <dir>` 生成（改玩法请改 Game/ 下\n"
             "脚本或场景后重新生成，勿手改 .prefab 内 guid）。\n\n"
             "## 素材来源与许可\n\n"
             "- `dungeon_*` 精灵表与 `*.anim`：yami-rpg-editor（MIT，Copyright (c) 2025\n"
             "  Yami & Xuran & Contributors）——随模板再分发需在发布物保留版权声明\n"
             "  （仓库根 THIRD_PARTY.md 已登记）。\n"
             "- `gem/bullet/pierce/blade.png`：程序化生成（无版权负担）。\n"
             "- `Assets/Audio/*.ogg`：Kenney 各 CC0 包 + OpenGameArt CC0（M6c 批④；\n"
             "  逐件来源表见 `Samples/Assets/cc0-audio/README.md`，THIRD_PARTY.md\n"
             "  已登记——CC0 无署名义务，登记仅为溯源）。\n\n"
             "## 玩法锚点\n\n"
             "- 流程（M6b 批③d-2 档1）：**单场景** `Main.scene` 只放常驻件（UI 六文档\n"
             "+ Flow 实体）；玩家/导演在 `Prefabs/Player.prefab`/`Director.prefab`，\n"
             "GameFlow 开局/重开时清场重挂（RunSweeper 按 tag 扫场销毁——重开 = C#\n"
             "自律清场，零引擎改动）。流程 = 主菜单 → 一局 → Esc 暂停/设置 → 死亡\n"
             "（首死复活对话/二死结算）→ 重开/回主菜单；**死亡策略归游戏侧**（模板\n"
             "示例 = 每局一次复活，见 PlayerCombat.Die 分叉——改复活道具/表驱动只动\n"
             "那一处）。\n"
             "- 玩家：`Player.prefab` 挂三脚本（M6a 批⓪ scripts[]：流程 → 移动 →\n"
             "战斗 → HUD 注册序 = 跨类型 Update 执行序）；一局共享态在 GameMain.Run。\n"
             "- 波次：`Director.prefab` WaveDirector（Inspector 数组段可调参）。\n"
             "- 数值表（M6a 批② T4）：升级池/武器参数/XP 曲线在 `Assets/tables/`\n"
             "（upgrades.tab / weapons.tab / balance.tab——PlayerCombat.Start 读，\n"
             "加升级项/换弹种/调环绕参数 = 改表不改代码；Excel/Numbers 改 CSV\n"
             "（导出 UTF-8）拖回 AssetBrowser 覆盖再导入）。列契约见各表列头与\n"
             "PlayerCombat.cs 头注释；表 GUID 生成期固定（改玩法勿动 .meta）。\n"
             "- 三选一池：upgrades.tab 行序（固定序轮换，零 RNG = 回放友好）。\n"
             "- 素材引用：.scene 双写 spriteGuid（真源）+ spriteId（进程内号）——\n"
             "改名/移位/manifest 重建后打开场景自动归一（M6a 批⓪ T2）。\n"
             "- 游戏 UI（M6b 批③d-1/③d-2）：六文档全走 .rml（HUD + 升级三选一/\n"
             "死亡对话 + 主菜单/暂停/设置/结算——`Assets/UI/`，theme.rcss 主题 token\n"
             "单源；场景 UI_* 实体挂 UIDocument 声明装载）。**换肤 = 改 theme.rcss 的\n"
             "token 区**（色板/字号/间距，全 dp——画布缩放时 UI 物理比例恒定，720dp\n"
             "设计基准）；改布局/文案 = 改 .rml/.rcss 资产，引擎零改动。数字键选择\n"
             "已退役（点击选择）；设置两开关（飘字/血条）+ 音量四滑条（主/音乐/\n"
             "音效/界面，M6c 批④）持久化于 Settings 档。\n"
             "- 音频（M6c 批④）：BGM 开局起播（单槽 = 重开不叠曲）/ 命中·击杀·拾取·\n"
             "升级·波次事件音 + UI 组按钮音（暂停中可响）；Esc 暂停 = BGM 声部级\n"
             "挂起续响（恢复不回跳）。**编辑器内注意**：Esc 是编辑器惯例 = 退出 Play，\n"
             "游戏内暂停请按 **P**（bit6 同映射别名；独立运行时 Esc 生效）。音频资产\n"
             "在 `Assets/Audio/`，换音 = 换文件保名（.meta guid 不动，脚本零改动）；\n"
             "音量即时生效 + Settings 档持久。工具栏暂停钮 = 编辑器检视冻结（音频同步\n"
             "挂起，M6c 批④ 起联动），与游戏内暂停是两回事。\n";
    }

    // 3) 打开项目（扫描记账）→ 播种场景 + 覆写 prefab 内容。
    // base = 调用方传入（OpenProjectPipeline 同规则——真实打开路径一致）
    if (!ctx.Assets().OpenProject(root.string(), spriteIdBase)) return false;
    ctx.NewScene();
    ecs::Scene& s = ctx.EditScene();

    // 批③d-2：场景只留常驻件——玩家/导演迁 Player.prefab/Director.prefab
    //（EnterRun 时 C# 重挂：重开 = 清场 + 重 spawn，脚本/表载/Start 与
    // WaveDirector 运行态随重挂自然归零——零手工复位清单）。
    // UI 六屏场景声明（通道 A——EnterPlay 扫描装载）：HUD/Main 进 Play 即显
    //（实底菜单覆盖 HUD；暂停/设置/结算 scrim 叠加时 HUD 在下 = 战况可视）；
    // 其余装载但隐藏（showOnStart=0，C# UI.Show 点亮——动态屏）。运行时显隐
    // 归 C#（组件字段只承载设计期声明态，Play 期写回不生效——③d 前置铁律）。
    {
        ecs::Entity hud = ctx.CreateEntity("UI_HUD");
        s.Emplace<ecs::UIDocument>(hud,
                                   ecs::UIDocument{.sourceAssetGuid = kHudRml});
        ecs::Entity cards = ctx.CreateEntity("UI_Cards");
        s.Emplace<ecs::UIDocument>(cards, ecs::UIDocument{.sourceAssetGuid = kCardsRml,
                                                          .showOnStart = 0});
        ecs::Entity main = ctx.CreateEntity("UI_Main");
        s.Emplace<ecs::UIDocument>(main,
                                   ecs::UIDocument{.sourceAssetGuid = kMainRml});
        ecs::Entity pause = ctx.CreateEntity("UI_Pause");
        s.Emplace<ecs::UIDocument>(pause,
                                   ecs::UIDocument{.sourceAssetGuid = kPauseRml,
                                                   .showOnStart = 0});
        ecs::Entity settings = ctx.CreateEntity("UI_Settings");
        s.Emplace<ecs::UIDocument>(settings,
                                   ecs::UIDocument{.sourceAssetGuid = kSettingsRml,
                                                   .showOnStart = 0});
        ecs::Entity results = ctx.CreateEntity("UI_Results");
        s.Emplace<ecs::UIDocument>(results,
                                   ecs::UIDocument{.sourceAssetGuid = kResultsRml,
                                                   .showOnStart = 0});
        // Flow 实体：档1 流程状态机（GameFlow——Configure 注册序首个 Update）
        ecs::Entity flow = ctx.CreateEntity("Flow");
        ctx.AttachScript(flow, 0, "GameFlow");
    }

    // prefab 内容（scratch 实体 → SaveEntityTree → 覆写 .prefab；导出后销毁）。
    // spriteGuid=0 = 无渲染分路（批③d-2：Director）
    auto exportPrefab = [&](const char* tag, uint32_t team, uint64_t spriteGuid,
                            auto build) -> bool {
        ecs::Entity e = spriteGuid != 0 ? ctx.CreateSpriteEntityByGuid(tag, spriteGuid)
                                        : ctx.CreateEntity(tag);
        s.Get<ecs::Meta>(e).team = team;
        build(e);
        const std::string json = ecs::SceneArchive::SaveEntityTree(s, e);
        ctx.DestroyEntityTree(e);
        if (json.empty()) return false;
        std::ofstream f(root / "Prefabs" / (std::string(tag) + ".prefab"),
                        std::ios::trunc);
        f << json;
        return true;
    };
    // 批③d-2：流程双 prefab（自场景迁出——构造原样移入，Player 含 scripts[]
    // 三拆；GameFlow EnterRun 经 Instantiate.Prefab 重挂，LoadEntityTree 重放脚本）
    if (!exportPrefab("Player", 0, kHeroSheet, [&](ecs::Entity e) {
            s.Emplace<ecs::Health>(e, ecs::Health{.max = 100.0f, .cur = 100.0f});
            s.Emplace<ecs::Stats>(e).pickupRadius = 96.0f;
            s.Emplace<ecs::XpProgress>(e, ecs::XpProgress{.xpToNext = 30.0f});
            ecs::Shooter& psh = s.Emplace<ecs::Shooter>(e);
            psh.projectileId = (uint32_t)kBulletPf;
            psh.interval = 0.12f;
            psh.range = 2000.0f;
            psh.targetTeam = 1;
            s.Emplace<ecs::Animator2D>(e).clipId = (uint32_t)kHeroClip;
            // M6a 批⓪ T4 三拆：槽序镜像注册序（移动 → 战斗 → HUD）；Game/ 不入资产扫描
            // → scriptGuid 恒 0（className 是持久键）
            ctx.AttachScript(e, 0, "PlayerMovement");
            ctx.AttachScript(e, 0, "PlayerCombat");
            ctx.AttachScript(e, 0, "PlayerHud");
        }))
        return false;
    if (!exportPrefab("Director", 0, 0, [&](ecs::Entity e) {
            // 16 波：15 波小怪递增 + t=565s Boss；后波接管语义下条目都在波内完成
            ecs::WaveDirector& wd = s.Emplace<ecs::WaveDirector>(e);
            wd.spawnTeam = 1;
            wd.capAlive = 300;
            wd.waveCount = 16;
            for (int w = 0; w < 15; ++w) {
                ecs::WaveDef& def = wd.waves[w];
                def.startTime = 5.0f + 35.0f * (float)w;
                def.rampMult = 1.0f;
                def.entryCount = 2;
                def.entries[0] = ecs::WaveEntry{.prefabId = (uint32_t)kMobPf,
                                                .count = (uint16_t)(30 + w * 4),
                                                .interval = 0.6f, .range = 560.0f};
                def.entries[1] = ecs::WaveEntry{.prefabId = (uint32_t)kMobPf,
                                                .count = (uint16_t)(15 + w * 2),
                                                .interval = 0.35f, .range = 320.0f};
            }
            ecs::WaveDef& def = wd.waves[15];
            def.startTime = 565.0f;
            def.entryCount = 1;
            def.entries[0] = ecs::WaveEntry{.prefabId = (uint32_t)kBossPf, .count = 1,
                                            .interval = 1.0f, .range = 80.0f};
        }))
        return false;
    if (!exportPrefab("Mob", 1, kMonsterSheet, [&](ecs::Entity e) {
            s.Emplace<ecs::Health>(e, ecs::Health{.max = 20.0f, .cur = 20.0f});
            s.Emplace<ecs::Knockback>(e);
            s.Emplace<ecs::Velocity>(e);
            s.Emplace<ecs::Animator2D>(e).clipId = (uint32_t)kMonsterClip;
            ecs::Chase& ch = s.Emplace<ecs::Chase>(e);
            ch.speed = 60.0f;
            ch.aggroRange = 2500.0f;
            ch.keepRange = 20.0f;
            ch.targetTeam = 0;
            ecs::Hazard& hz = s.Emplace<ecs::Hazard>(e); // 近身接触伤害
            hz.dps = 8.0f;
            hz.tickInterval = 0.8f;
            hz.radius = 24.0f;
        }))
        return false;
    if (!exportPrefab("BossMob", 1, kBossSheet, [&](ecs::Entity e) {
            s.Emplace<ecs::Health>(e, ecs::Health{.max = 600.0f, .cur = 600.0f});
            s.Emplace<ecs::Knockback>(e);
            s.Emplace<ecs::Velocity>(e);
            s.Emplace<ecs::Animator2D>(e).clipId = 0; // boss clip 帧率低，先静态
            ecs::Chase& ch = s.Emplace<ecs::Chase>(e);
            ch.speed = 32.0f;
            ch.aggroRange = 3000.0f;
            ch.keepRange = 26.0f;
            ch.targetTeam = 0;
            ecs::Hazard& hz = s.Emplace<ecs::Hazard>(e);
            hz.dps = 18.0f;
            hz.tickInterval = 0.5f;
            hz.radius = 40.0f;
        }))
        return false;
    auto bulletBody = [&](ecs::Entity e, float dmg, uint8_t pierce, float life) {
        s.Emplace<ecs::Velocity>(e);
        ecs::Projectile& pr = s.Emplace<ecs::Projectile>(e);
        pr.damage = dmg;
        pr.speed = 320.0f;
        pr.lifetime = life;
        pr.pierce = pierce;
    };
    if (!exportPrefab("Bullet", 0, kBulletPng, [&](ecs::Entity e) {
            bulletBody(e, 8.0f, 0, 2.5f);
        }))
        return false;
    if (!exportPrefab("PierceBullet", 0, kPiercePng,
                      [&](ecs::Entity e) { bulletBody(e, 6.0f, 3, 3.0f); }))
        return false;
    if (!exportPrefab("Gem", 2, kGemPng, [&](ecs::Entity e) {
            ecs::Collectible& c = s.Emplace<ecs::Collectible>(e);
            c.kind = 0;
            c.value = 1.0f;
        }))
        return false;
    if (!exportPrefab("Blade", 0, kBladePng, [&](ecs::Entity e) {
            ecs::Hazard& hz = s.Emplace<ecs::Hazard>(e);
            hz.dps = 6.0f;
            hz.tickInterval = 0.4f;
            hz.radius = 44.0f;
        }))
        return false;

    // 4) 场景落盘 + 终态记账（prefab 内容覆写后 hash 刷新）
    {
        std::ofstream f(root / "Scenes" / "Main.scene", std::ios::trunc);
        f << ecs::SceneArchive::Save(s);
    }
    ctx.Assets().Rescan();
    ctx.Assets().SaveManifest();
    LEMON_LOG("gen-vs-template：OK → %s（scene %zuB，%u 实体）", root.string().c_str(),
              ecs::SceneArchive::Save(s).size(), s.AliveCount());
    return true;
}

} // namespace lemon::editor
