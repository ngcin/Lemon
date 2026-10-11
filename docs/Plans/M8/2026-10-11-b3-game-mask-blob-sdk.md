# M8 批③ —— lemon-game 装配 + SpriteMask 蒙版 + blob 椭圆 + Lemon.Lighting 门面 + QualityManager 游戏壳

Status: done（2026-10-11 开工，机器面当日收口；[DevLog](../../DevLog/2026-10-11-m8-b3-game-mask-blob-sdk.md)；真人观感归批⑤ 组合走查）

- 总览行：[M8.md](./M8.md) §2 批③；设计权威：[ADR-019](../../ADR/ADR-019-Light2D-Lightmap-Component.md) M1–M5
- 前置：批① 组件落位 ✅ / 批② 编辑器光贴图竖切 ✅（含 gizmo 前移与真人观感过）
- 预估 2 日；出口判据 = M8.md §2 ③ 行

## 1. 输入条件（2026-10-11 探测核证）

批② 已落编辑器侧全链：`LightCompositor`/`LightBatcher`/light+composite shader、光/blob 提取通道（Renderable.h:118-134）、GameView 三段式（ViewportRenderer.cpp:556-584）、QualityManager 编辑器壳、bindless 250/251。批③ 缝隙全部有代码内标注：LightCompositor.cpp:72-78（SpriteMask 滤除）、LightBatcher.cpp:119（maskU*=0 占位）、light.frag:49-53（type3 零输出）、Renderable.h:128（blob 渲染消费归批③）、ViewportRenderer.cpp:554-556（gate 只看灯）、GameEntry.cpp:1030-1035（零离屏直渲交换链）、Quality.h:4（游戏壳注记）。`LightInstance` D 行蒙版 UV 与 light.vert 的 vMaskUV 插值已预铺；RenderExtractSystem 已在 GameEntry 装配（GameEntry.cpp:706-708，批② 移交件）——渲染侧只需消费。

## 2. 文件/行级变更单

### A 引擎渲染件（两壳共用，零第三套）

- [x] **A1 LightTypes.h**：槽位常量 `kSceneRtTexSlot=250`/`kLightRtTexSlot=251` 从 ViewportRenderer.h:71-72 移入 + `kGlowTexSlot=249`（#L20 记账顶部段 3 槽）；`PackLightTypeBlend` 扩 `(type<<28)|(blend<<24)|(atlasIdx&0xFFFFFF)`（唯一消费点 LightBatcher.cpp:113 已核证）；新增纯函数 `ResolveLightMask`/`FillBlobInstance`。
- [x] **A2 LightBatcher**：`Bake(const AtlasRegistry&, lights, blobs)`——blob 实例 = Point 路径（typeBlend=Point|Subtract、color 白、intensity=opacity、range=radius、仿射 edgeX=2·radius·aspect/edgeY=2·radius、平移 +offsetY，与 gizmo ViewportPanels.cpp:821-822 同约定）计入 Subtract 桶尾；mask 灯 ResolveLightMask 填 D 行 UV + 页槽，失败跳过 + 一次性 WARN。
- [x] **A3 LightCompositor**：`Prepare(items, blobs, budget, atlas*)`——SpriteMask 不再无条件滤除，改按 maskSprite 有效性把关（无效→droppedLights+WARN，不入 filtered_）；blob 独立截断计数 `drawBlobs/droppedBlobs`；`RenderLightPass` 透传 atlas+blobs。
- [x] **A4 light.frag**：type3 落地——`texture(sampler2D(uAtlases[nonuniformEXT(vTypeBlend & 0xFFFFFF)], uSamplers[0]), vMaskUV)`，`oColor = color.rgb × intensity × m.rgb × m.a`（无径向衰减，贴图即造型；light.vert 零改动）。
- [x] **A5 RHI.cpp:419-424**：`shaderSampledImageArrayNonUniformIndexing` 特性位 + assert（回退预案 = 蒙版灯按灯单 draw + push 常量页槽，不入 v1）。
- [x] **A6 SceneExtractor blob 循环**：opacity≤0 滤除（"opacity=0 = 关"，镜像 kLightEnabled）。
- [x] **A7 ViewportRenderer**：gate 扩 `|| AliveBlobCount()>0`；Prepare/RenderLightPass 调用点适配。

### B lemon-game 装配（GameEntry.cpp）

- [x] **B1** 装配区 ：835 后：sceneBatcher（RGBA8Unorm, ringSlot 1）、LightCompositor（ringSlot 2, 槽 250/251, composite=SwapchainFormat()）、QualityManager + 渲染帧钟、EnsureLightTargets（懒建+按档重建）；RecreateGuard 补 RT 句柄置空。
- [x] **B2** BuildProceduralPages 尾补 glow 页（128×128 径向渐变，LightRadialAttenuation 同式）→ 槽 249 + glowSprite。
- [x] **B3** 帧循环 ：1027-1042：质量钟 + 开灯帧三段式（豁免段走交换链格式 batcher，两 batcher 各 Bake 一次）；**关灯 else 分支逐字保留**（零成本机械口径）；三方 AdvanceFrame。
- [x] **B4** `--smoke-light`：播种 floor + SpriteMask 灯（glow 蒙版）+ Point 灯 + blob；lightRT 捕获断言（蒙版亮斑 Δ>30 / blob 暗斑 Δ>30 / drawLights+drawBlobs 计数 / 三档 lightRT 尺寸）；RESULT 行 + **fps≥60 门禁**。

### C Lemon.Lighting 门面（零 vtable / 零引擎改动）

- [x] **C1** `Lemon.SDK/Lighting.cs`：SetGlobalAmbient（整写全字段显式）、Enable/Disable/Toggle/IsEnabled、SetColor/SetIntensity/SetRange、BlobShadow.Set/SetOpacity、Rgba/Kind/Blend 助手、DayNight（= Tween.To\<Light2D\> color+intensity 双补间）。
- [x] **C2** script-tests：扩 LightProbeBehaviour（号段 1900-1919 内续用，不加新 behaviour）——facade 写 → mark → C++ 断言组件态 + StateHash；DayNight 逐帧数值断言。

### D 测试与回归

- [x] **D1** LightTests：TestLightCompositorPlan 更新 + TestLightMaskResolve + TestBlobInstanceMapping + 提取通道 blob 滤除 + 新打包断言。
- [x] **D2** 编辑器 smoke-light 扩：+蒙版灯、blob 启停随 f70/f200、断言 7→9（SmokeLightState +2 字段）。
- [x] **D3** editor-regression.sh 插 game light smoke 步 → **full 23→24 步**；09 §8/§9 口径同步；:237 陈旧步数注释顺手修。
- [x] **D4** bench-lights 基线刷新（blob 渲染生效）+ bench-survivor 门禁复核。
- [x] **D5** 基线核对：单测 34,864+Δ / script-tests 1,905+Δ / ctest 4/4 / **vtable 59 实证不变** / 构建 0 警告 / 金回放零重录。

### E 文档落账

- [x] 02 §8（SpriteMask 语义/blob 映射/游戏壳装配/nonuniform 注）、04 §3（Lighting 门面登记块）、09 §8/§9、Quality.h:3-4 头注、AGENTS.md:5 基线、M8.md ③ 行勾销、DevLog 条目。

## 3. 设计裁决（默认执行，用户可否决）

1. **nonuniform 特性位**（A5）：蒙版按实例取页的唯一机制前提；回退已备。
2. **蒙版因子 = rgb×a、无径向衰减**（A4）：两类素材通吃，Unity 同构。
3. **blob = Point 路径实例 + Subtract + aspect 仿射**（A2）：零 shader 改动，桶序天然"暗斑最后扣"。
4. **blob opacity=0 = 关**（A6）：无 flags 字段组件的关灯口径，保关灯帧逐位同断言成立。

## 4. 排程（2 日）

日1上午 A1–A5 + LightTests → 日1下午 A6/A7 + B 全部（手动跑 light.scene 过目）→ 日2上午 C → 日2下午 D 全量 + E 勾销。超期预案：C 门面可独立后挪；A+B 为不可拆主干。

## 5. 完工记录（2026-10-11 机器面）

### 实测数字

| 项 | 结果 |
|---|---|
| 单测 | **34,888**（+24） |
| script-tests | **1,911**（+6） |
| ctest | 4/4 |
| 回归 full | **24 步全绿**（23→24，light-smoke 首跑绿） |
| light-smoke（游戏壳） | OK + --validate 干净：蒙版Δ=166/暗斑Δ=85/三档精确/fps≥60 门禁 |
| smoke-light（编辑器） | 九断言 PASS：蒙版Δ=170·暗斑Δ=85 与理论精确吻合 |
| bench-survivor / bench-lights | 89≥76.5 / 919fps PASS（blob 渲染生效，冷机态新锚） |
| vtable/组件/系统 | 59 / 33 / 21 不变；金回放零重录；构建零警告 |

### 四设计裁决落点（§3 逐项兑现）

1. nonuniform 特性位：RHI.cpp want12 + assert 落地；light.frag `nonuniformEXT(vTypeBlend & 0xFFFFFF)`；SPIR-V vulkan1.1 目标 ShaderNonUniform 能力核证。
2. 蒙版因子 rgb×a、无径向衰减：02 §8 落注；编辑器（白方块蒙版）与游戏壳（glow 渐变）双夹具像素断言过。
3. blob = Point 路径实例：`FillBlobInstance` 纯函数 + Subtract 桶尾；暗斑Δ=85 与 ambient−钳底理论精确吻合。
4. blob opacity=0 = 关：提取滤除；编辑器 smoke-light 基线/关灯帧恢复真·无光照语义（f200 连 blob 一起关）。

### 过程与移交

过程五条（smoke 语义收口/布位两修/script-tests 三修/防御层串桶修复 + 回归混版本重跑/ctest 瞬时红三连绿）与移交批④⑤ 三项见 [DevLog](../../DevLog/2026-10-11-m8-b3-game-mask-blob-sdk.md)。
