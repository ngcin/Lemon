# M8 批③ 完工 —— lemon-game 光贴图装配 + SpriteMask 蒙版 + blob 椭圆 + Lemon.Lighting 门面（机器面收口）

- 日期：2026-10-11（当日开工当日机器面收口）
- 批文件：[Plans/M8/2026-10-11-b3-game-mask-blob-sdk.md](../Plans/M8/2026-10-11-b3-game-mask-blob-sdk.md)（文件/行级变更单 + 四设计裁决见其 §2/§3）
- 设计权威：[ADR-019](../ADR/ADR-019-Light2D-Lightmap-Component.md) M1–M5 + [02 §8](../EngineDesign/02-Rendering-Vulkan.md)（蒙版/blob/两壳装配语义本批落注）

## 落地清单（机器面）

- **引擎渲染件（两壳共用）**：`LightBatcher::Bake(atlas, lights, blobs)`——SpriteMask 蒙版填 D 行 UV + typeBlend 低 24 位页槽（新打包 type<<28|blend<<24|atlasIdx，唯一消费点核证后改）；blob 走 Point 径向路径零 shader 改动（`FillBlobInstance` 纯函数：白×opacity、range=radius、仿射 edgeX=2·radius·aspect/edgeY=2·radius、平移 +offsetY 与 gizmo 同约定）计入 Subtract 桶尾。`LightCompositor::Prepare(items, blobs, budget, atlas*)`——SpriteMask 改按 maskSprite 有效性把关（无效 → droppedLights + 一次性 WARN，drawLights 与落位严格一致）；blob 独立截断计数（drawBlobs/droppedBlobs，同 budget 数值——灯满员不挤暗斑，bench-lights 64 灯 + 8 blob 形态互不侵扰）。`light.frag` type3 落地：蒙版因子 = `texel.rgb × texel.a`、无径向衰减（贴图即造型）、`uAtlases[nonuniformEXT(page)]` 按实例取页。
- **RHI 特性位**：`shaderSampledImageArrayNonUniformIndexing`（want12 + assert）——蒙版按实例索引图集数组的机制前提（descriptorIndexing 同族；SPIR-V vulkan1.1 目标下 ShaderNonUniform 能力位核证）。回退预案（按灯单 draw + push 常量页槽）登记批文件未启用。
- **SceneExtractor**：blob `opacity<=0` 滤除——"opacity=0 = 关"（BlobShadow2D 无 flags 字段的关灯口径，镜像 kLightEnabled；blob-only 场景可全关 = 关灯帧 gate 正确回落原路径）。
- **lemon-game 装配（GameEntry）**：`sceneBatcher`（RGBA8Unorm 环 1，场景段渲 sceneRT）+ `LightCompositor`（环 2，composite 目标 = 交换链格式——头注预留位兑现）+ `QualityManager` 渲染帧钟 EMA（游戏壳首接，两壳齐）+ `EnsureLightTargets` 懒建/按档重建 + RecreateGuard RT 句柄复位；开灯帧 = sceneRT → lightRT → **合成段直写交换链** → 豁免段（交换链格式 batcher，两台各 Bake 一次无 append）→ RmlUi 同块内；**关灯帧 else 分支逐字保留**（零额外 pass）。程序化 glow 蒙版页（槽 249，白 RGB + LightRadialAttenuation 同式 alpha 渐变；顶部段 249/250/251 三槽 #L20 记账）。
- **`--smoke-light` 游戏壳冒烟**：播种 Global/蒙版红/点光蓝/blob（guid 段 4730…，全关落场）→ f70 布位锚当帧相机（菜单态相机随场景挂靠点漂移至原点——固定世界坐标会整体出视野，实测修正）→ f100 lightRT 捕获 → f102 断言 → 三档 SetTier 尺寸 → f200 全关（含 blob opacity=0）→ RESULT + **fps≥60 门禁**（pkg-smoke 同款口径）。回归新步（full 23→24）。
- **编辑器 smoke-light 扩（断言 7→9）**：+MaskAdd 蒙版灯（白方块蒙版 = 视口白精灵——内亮外暗同证采样链）；blob 挪位（原 760,420 夹在两光域间，邻域对照点无净空）+ 启停随 f70/f200（**关灯帧恢复真·无光照语义**——批③ 前基线/关灯帧都带 blob 的一致性巧合不再依赖）；f103 同捕获三断言（光斑 maxΔ + 蒙版Δ + 暗斑Δ）。
- **`Lemon.Lighting` 纯 C# 门面**（`Lemon.SDK/Lighting.cs`，零 vtable/零引擎改动）：`SetGlobalAmbient`（整写全字段显式 = 镜像默认值坑三连正面解法；组件缺失走 op2 暂存返回 false——**两拍时序**语义）+ `Enable/Disable/Toggle/IsEnabled`（返回操作后启用态）+ `SetColor/SetIntensity/SetRange` + `DayNight`（= Tween.To\<Light2D\> color+intensity 双补间，入 StateHash 可回放）+ `BlobShadow.Set/SetOpacity` + `Rgba`/`Kind`/`Blend`。script-tests TestLightSdk 扩帧链 f4-f11（op2 两拍/整写落地/开关翻转/昼夜逐拍在途与终值，号段 1900..1919 内续用，探针计数 27 不动）。

## 实测数字

| 项 | 结果 |
|---|---|
| 单测 | **34,888**（+24：ResolveLightMask/FillBlobInstance 两新组 + Compositor Plan 新语义（mask 把关/blob 独立截断/null 图集防御）+ 提取通道 blob 滤除 + 新打包断言） |
| script-tests | **1,911**（+6：facade 帧链引擎侧对拍 6 断言） |
| ctest | 4/4 |
| 回归 full | **24 步全绿**（23→24，light-smoke 新步首跑绿；本轮无 retry 红加倍） |
| light-smoke（游戏壳） | 全绿 + --validate 干净：蒙版 Δ=166 / 暗斑 Δ=85（理论 0.65×255≈166 与 ambient−钳底精确吻合）/ 三档 320×180→640×360→1280×720 逐档精确 / drawLights=2·drawBlobs=1 / f200 后 Alive 双零回原路径 / fps≈913–1363（≥60 门禁） |
| smoke-light（编辑器） | 九断言全 PASS：光斑 maxΔ=165 / **蒙版Δ=170**（85+255 钳顶−85 精确）/ **暗斑Δ=85**（ambient g − 钳底精确）/ 关灯帧逐位同（真·无光照基线） |
| bench-survivor 门禁 | fps=89 ≥ 76.5（回归内） |
| bench-lights | **919/924fps PASS**（blob 渲染生效后冷机态实测；批② 532 为构建热节流态——软门禁 55 不变，数字以本轮为锚；GPU 分解归批⑥） |
| vtable / 组件 / 系统 | 59 / id 至 33 / 21 不变（零槽改动代码面；script-tests 布局断言证组件面——vtable 槽数无独立断言） |
| 金回放 | 零重录（无新组件、渲染面 + 纯 sugar 零哈希流改动；record/replay 自洽经 engine/script 双测绿） |
| 构建 | 零警告（编译器面；ld 重复库 ×2 = 既登记项不变） |

## 过程记录

1. smoke-light（编辑器）批③ 首跑即过但语义已变：blob 渲染在场后基线/关灯帧都走三段式（一致性巧合 PASS）——D2 显式收口：blob 启停随灯，基线恢复真·无光照路径，"零灯零 blob 才走原路径"机械口径成立。
2. `--smoke-light` 布位两修：捕获请求帧尾设置晚于当帧渲染（差一帧 → f99 预约 f100）；固定世界坐标出视野（菜单态相机随挂靠点漂移至原点 → f70 按当帧相机中心布位 + f102 按实体实际变换取样——相机漂移免疫）。
3. script-tests 三修（探针读写顺序/Disable 返回值语义/补间完成拍算术）：facade 语义两处顺手定形——Disable 返回**操作后启用态**（幂等查询语义非成功布尔，头注写死）；`SetGlobalAmbient` 两拍时序（op2 暂存语义的正面文档化）。
4. 自审轮发现 LightBatcher 防御层桶计数/落位不一致（计数遍跳过无效蒙版、落位遍照放 = 串桶腐败；compositor 正道不触发，纯防御层 bug）——落位遍同判 continue 修复；该修复引发回归混版本重跑（构建与回归并发 = 后续步用新二进制），废弃首轮 17 步记录、干净重跑全绿。
5. ctest 一次瞬时红（与并行 ctest 写 Testing/ 临时目录撞）——三连复跑 4/4 稳定绿，非代码面。

## 追记（2026-10-11 commit 前评审轮）

三片并行深审（引擎渲染件 / 游戏壳+冒烟 / C#+测试+文档），**零 P0、P1×2（均文档面）+ P2×13，全即修**：

- **P1×2**：① Lighting.cs 两拍时序的中间态描述纠错——op2 落场的是**引擎 C++ 默认值**（Light2D = Point/白/启用，第二拍前该灯已可见），非"C# default 无害禁用态"（SetGlobalAmbient/BlobShadow.Set/WriteOrAdd 三处头注 + main.cpp 同根注释四处齐修）；② 批文件 §E 漏勾 + §5 标题重复（收尾脚本拼接笔误）。
- **P2 涉码四修**：图集页分配上限 `kAtlasSlotCeiling=249`（Atlas.h 常量 + TextureStore/AtlasStore 两处钳制——顶部保留段 249..251 此前仅 #L20 记账无硬执行，247+ 页项目会撞 RegisterAtlas slot-reused 断言）；`--smoke-light` 裁决档位期望改 TierParams 派生（防重调档位常量假红）+ 补 H 维门禁（原只门 W）；播种实体补 SceneMembership 盖章（防组合 smoke 态孤儿计数污染）+ 组合旗标装载期 WARN；CLI 头注补 `--smoke-light`。另 glow 槽常量移 Atlas.h 本位（kGlowTexSlot 在 LightTypes.h 引发的包含环）。
- **P2 注释/文档九修**：light.vert×2 + LightTypes.h×2 旧 `type<<16|blend` 打包注释残留、blob range 死字段注（"勿以调 blob 尺寸"）、TestScript 补间完成拍算术注（帧8 非帧9）、main.cpp 注册序注释精确化（DestroyCommit 为 Essential 段）、04 §3 通道等式（op2/op3 是结构命令，Native.Read/Write 才是即时数据面）、DevLog vtable"实证"措辞收敛（59 = 零槽改动代码面；布局断言证组件面，vtable 槽数无独立断言）、批文件 C1 API 面补全（SetOpacity/Rgba/Kind/Blend）。
- 评审确认干净（抽样）：nonuniformEXT/SPIR-V vulkan1.1/特性链完整；Subtract = REVERSE_SUBTRACT dst−src 且链式钳 0 序无关（桶尾暗斑安全）；blob 椭圆边界 dn=1 精确落椭圆边、offsetY/半轴与 gizmo 逐字一致；Bake 两遍一致性；关灯 else 分支逐字原路径；帧序/生命期/AdvanceFrame-continue 语义；两壳光域距离表全外。
- 复测全绿：构建零警告 / 单测 34,888 / script-tests 1,911 / ctest 4/4 / light-smoke OK（fps=321 本轮热机态——≥60 门禁口径不变）/ smoke-light 九断言 PASS（--validate）/ asset-chain smoke PASS（盖住 TextureStore/AtlasStore 改动面）。

## 待办（移交）

- 批④：后处理链两壳接线（编辑器 gameRT 终点照旧，lemon-game 合成终点"→交换链"重构为"→FX 链→交换链"——本批合成段直写交换链的装配点即重构位）；含光场压测 A 干跑；#M4 Windows 真机轮窗口随 W 系。
- 批⑤：vs-survivor 夜战/火把 + svr-test 昼夜循环（`Lighting.DayNight` 首个真实消费者；light.scene 的 `BlobHero_批3渲染` 标注位本批起已实际渲染）。
- bench-lights 冷机态 919 为新锚（热节流态历史 515/532 留档）；GPU 段分解归批⑥。
