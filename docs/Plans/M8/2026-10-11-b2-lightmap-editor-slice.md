# 批② —— 编辑器光贴图竖切（三段式渲染链 + SceneView 灯 gizmo + 编辑器面收口）

Status: 机器面 ✅ 2026-10-11 当日（单测 **34,859**（+62）/ ctest 4/4 / 回归 full **23 步全绿**（smoke-light 新步首跑绿；smoke-ui retry 1 = 注入抖动家族隔离归因）/ smoke-light 七断言 PASS（--validate）/ bench-lights **515fps** PASS / 金回放零重录 / vtable 59·id 33·系统 21 不变 / 构建零警告，[DevLog](../../DevLog/2026-10-11-m8-b2-lightmap-editor-slice.md)）——**done 2026-10-11（真人观感同日通过）**；观感支持期追加两项（light.scene ambient 修复 + Light2D 启用复选框，见 DevLog 追记）

> 主题：GameView 光贴图渲染链竖切——LightCompositor + LightBatcher + light/composite 着色器 + SceneExtractor 光/blob 提取通道 + sceneRT/lightRT 三段式装配 + 段豁免（251/252/254 划合成后）+ **SceneView 灯范围圈/Spot 楔形/Blob 椭圆虚框 gizmo（2026-10-11 评审自批③ 前移）** + QualityManager 编辑器壳接入 + smoke-light + bench-lights。设计权威 = [ADR-019](../../ADR/ADR-019-Light2D-Lightmap-Component.md) M1/M3/M5；渲染语义见 [02 §8](../../EngineDesign/02-Rendering-Vulkan.md)。SpriteMask 蒙版渲染与 blob 阴影渲染、lemon-game 装配归批③（本批提取通道先行、渲染消费留③）。

## 0. 开工核证（2026-10-11 实证）

1. **RT 格式与色彩空间（评审拍板口径核证通过）**：`EnsureRenderTarget` 未传 format = 默认 RGBA8Unorm（ViewportRenderer.cpp:442-443）；sceneRT/lightRT/gameRT 与 gameBatcher_ 现有管线（ViewportRenderer.cpp:374-376）全为 RGBA8Unorm；shader 无 sRGB 转换、RT 非 SRGB 变体、ImGui 采样照旧——gamma 空间乘法合成链路无冲突。**零新增 Format 枚举**（RGBA16Float 留 v1.1 HDR bloom）。02 §8 L192 注记与实现一致，无需翻案。
2. **BlendMode 缺口**：现枚举 Opaque/Alpha/Additive/Multiply（RHI.h:37），lightRT 三混合需 **Screen、Subtract 尾插**（映射见 §2 #1；SpriteBatcher pipelines_[4] 按 BlendKind 索引不受影响）。
3. **实例环槽位**：kRingSsboSlots=4，编辑器 0/1 已被双视口 sprite 合批器占用 → LightBatcher 用 **slot 2**（sprite.vert rings[4] 同长度声明，M4.7-P0 语义维持）。
4. **bindless 槽位**：sceneRT/lightRT 各占 1 槽（建 RT 时绑一次，避 M4.7-P0 同帧改槽）——取高位 **250/251**（导入图集页在低位段；#L20 记账 +2）。
5. **Bake 分段前提**：SpriteBatcher::Bake 每次清表从偏移 0 重写实例（SpriteBatcher.cpp:94-95）——开灯帧两次烘焙会覆盖前段实例，需 **append 模式 + Record 批区间**（§2 #8）。

## 1. 帧图（开灯帧，仅 GameView idx=1；SceneView 零改动）

```
[场景段] 精灵/粒子（层 ≤250）→ sceneRT（全分辨率，RGBA8Unorm）
[光照段] lightRT（质量档缩放）CLEAR = ambient（Global 灯 color×intensity，无 Global = 白）
         → 光源四边形按混合叠加（LightBatcher 3 管线：Additive/Screen/Subtract）
[合成段] gameRT：全屏四边形 sceneRT × lightRT（composite.frag，gamma 直乘）
[UI 段]  豁免层（血条 251/飘字 252/位图文本 254）+ RmlUi 同块内画（不受光）
```

- **关灯零成本**：零 Light2D 且零 BlobShadow2D ⇒ 三段链整体跳过，走今日原路径（单 Bake 单 Record + gameUi），命令流逐字节同——smoke-light 断言基线。
- ambient = Global 灯（至多一个生效，重复取首个 + 一次性 WARN）color×intensity 钳 [0,1]。

## 2. 触点清单（文件/行级）

**RHI（2 文件）**

| # | 文件 | 动作 | 锚 |
|---|---|---|---|
| 1 | `Engine/Renderer/RHI.h` | BlendMode 尾插 `Screen`、`Subtract`（Count 前） | :37 |
| 2 | `Engine/Renderer/RHI.cpp` | 混合映射：Screen = src ONE / dst ONE_MINUS_SRC_COLOR（op ADD，shader 出预乘贡献）；Subtract = src ONE / dst ONE（op **REVERSE_SUBTRACT**，out = dst − src）；colorBlendOp 改按模式分派 | :80-89、:1248-1258 |

**提取（3 文件）**

| # | 文件 | 动作 | 锚 |
|---|---|---|---|
| 3 | `Engine/Renderer/Renderable.h/.cpp` | 镜像 Entry 增光/blob 双缓冲数组（Light2D/BlobShadow2D 快照 + prev/cur 世界变换 + seq/alive）；模拟侧整包推送 + SnapPrev；渲染侧 `ExtractLights(alpha)`（插值 + range 圆视口剔除）——blob 通道同款（渲染消费归③，先建数据面） | Entry :127-134 |
| 4 | `Engine/Renderer/SceneExtractor.cpp/.h` | 加 `View<Transform2D, Light2D>` / `View<Transform2D, BlobShadow2D>` 双通道（位索引/version 防串槽/纪元差集/禁用跳过；freshSlot 建槽 + SetAll + SnapPrev 镜像 sprite 语义；悬空 maskSprite 不阻断提取） | :31 sprite 先例 |

**渲染件（5 新文件 + 2 改）**

| # | 文件 | 动作 | 锚 |
|---|---|---|---|
| 5 | `Engine/Renderer/LightTypes.h` 新 | GPU LightInstance（vec4 对齐 4×vec4=64B：仿射 a/b/c/d/e/f + type/blend；color/intensity/range/angle/innerRatio + 预留 mask UV）；CPU LightItem（插值世界变换 + 组件快照） | SpriteTypes.h 样板 |
| 6 | `Engine/Renderer/Shaders/light.vert|frag` 新 | Point：径向 att = t²(3−2t)，t=clamp(1−d/range)；Spot：径向 × 角向 soft（cos(外半角)→cos(外半角×innerRatio) 的 smoothstep，方向 = 实例旋转）；SpriteMask 分支占位留③；uAtlases[256] 与 sprite.frag 同源（D10） | sprite.vert/frag 样板 |
| 7 | `Engine/Renderer/Shaders/composite.vert|frag` 新 | 全屏四边形（角点 VB + push 常量双纹理/采样器索引）；out = scene.rgb × light.rgb | — |
| 8 | `Engine/CMakeLists.txt` | `lemon_embed_shader` ×4 + D10 护栏扩 light/composite frag | :91-112 |
| 9 | `Engine/Renderer/LightBatcher.h/.cpp` 新 | 镜像 SpriteBatcher：Init（RGBA8Unorm 3 管线 Additive/Screen/Subtract + 实例环 slot 2）/设备丢失回调/弹性扩容/Bake/Record/AdvanceFrame | SpriteBatcher.cpp 全样板 |
| 10 | `Engine/Renderer/LightCompositor.h/.cpp` 新 | Prepare（Global 去重 + WARN、ambient 计算、光数上限截断 + 一次性 WARN）/ RenderLights（lightRT BeginOffscreenPass(CLEAR=ambient) → Record）/ RenderComposite（全屏乘法 → gameRT 块内） | — |
| 11 | `Engine/Renderer/SpriteBatcher.h/.cpp` | Bake 增 append 模式（不清 batches_、实例偏移续接段内累计）+ Record 增批区间重载（开灯帧两段录制；关灯帧原单段调用零改动） | :85-143、:145-176 |

**装配（1 文件）**

| # | 文件 | 动作 | 锚 |
|---|---|---|---|
| 12 | `Editor/Interaction/ViewportRenderer.h/.cpp` | idx=1 三段式分支（sceneRT_/lightRT_ 成员、尺寸稳定节流复用、设备丢失回调一并失效）；lightRT = gameRT × 质量档缩放（≥8px）；bindless 250/251 建时绑一次；开灯帧序 = Bake(≤250)→sceneRT Record→lightRT→gameRT(composite→Bake append(豁免段)→Record 区间→gameUi)；AdvanceFrame 推进 lightBatcher_ | 装配点 :464-552、EnsureRenderTarget :411-446 |

**编辑器面（2 文件）**

| # | 文件 | 动作 | 锚 |
|---|---|---|---|
| 13 | `Editor/Panels/ViewportPanels.cpp` | 主选块：Light2D → Point/Spot 画 range 折线圈（32 段）+ Spot 楔形两边线（张角 angle、朝向 = 实体旋转）；Global 不画；BlobShadow2D → 椭圆虚框（radius×aspect、offsetY、虚线段间隔）；色复用 overlay:: 常量 | 选框先例 :276-292、旋转圈 :748-759 |
| 14 | `Editor/Panels/InspectorPanel.cpp` | AssetRef+UInt32 分派泛化出按 (meta,field) 写 u32 变体；Light2D.maskSprite 接 sprite 下拉（批① ED_TIP 裸值升级，走既有属性 undo 轨） | :530-533 |

**质量（2 文件）**

| # | 文件 | 动作 | 锚 |
|---|---|---|---|
| 15 | `Engine/Renderer/Quality.h` | QualityParams + lightRtScale（0.25/0.5/1.0）+ lightBudget（32/64/64）；头注"M8 扩展"兑现 | :14-25 |
| 16 | `Editor/Interaction/ViewportRenderer` | QualityManager 实例 + 每渲染帧 Update（EMA 降档）；档变 → lightRT 按新缩放重建（lemon-game 壳随批③ 装配，今日不动 GameEntry） | — |

**测试与回归（4 处）**

| # | 位置 | 动作 | 锚 |
|---|---|---|---|
| 17 | `tests/engine/` 新 LightRenderTests（或并入现有 TU） | 衰减/角向 softstep/Global 去重/lightRT 尺寸计算（纯函数化）+ 提取通道（建槽/插值/禁用/销差集/悬空 maskSprite 不阻断） | EcsTests 样板 |
| 18 | `lemon-editor --smoke-light`（EditorAppSmoke 邻接） | 无灯基线帧 → 开灯帧（三类型×三混合 + ambient + gizmo 圈像素）→ 关灯帧**逐位同**断言（读回口 ViewportRenderer.h:151-154）→ 三档 lightRT 尺寸实测 | smoke 先例 |
| 19 | `tools/editor-regression.sh` | 加 smoke-light 步（full **22→23**） | 步骤表 |
| 20 | `lemon-editor --bench-lights` + `tools/ci-daily.sh` | 64 灯场 fps（BenchVerdict 聚合，bench-survivor 形态）；数字入 ci-daily 报告 | ci-daily.sh |

## 3. 出口判据（对照 M8.md §2 ② 行）

- [x] 三段式开灯帧可见 + **关灯帧像素断言与无光照基线逐位同**（smoke-light memcmp：开灯 diff=1,881,124B、灯下/灯外 maxΔ=165）+ 灯下/灯外像素差断言
- [x] Global/Point/Spot 三类型 + Additive/Screen/Subtract 三混合冒烟（drawLights=3 + ambient=(1.00,0.96,0.84) 断言；SpriteMask/blob 渲染归③——mask 滤除 WARN + blob 提取在场）
- [x] SceneView 选中灯画范围圈/Spot 楔形/椭圆虚框（Global 不画；gizmo 像素 1596 + blob 虚框 184）
- [x] 质量三档 lightRT 尺寸实测（327×184 / 654×368 / 1308×736 逐档精确 = ¼/½/全）
- [x] RT 格式/gamma 核证落账（本文件 §0.1：零新 Format，02 §8 注记与实现一致）
- [x] smoke-light 进回归 full 22→**23**；09 §8/§9 口径同步（两处 21→23 含轨迹；顺带 ci-daily.sh:6 注释）
- [x] bench-lights 数字入 ci-daily 报告（追加段 + 软门禁 55fps；实测 **515fps**）
- [x] **15 分钟真人观感过目**（灯池/三混合/ambient 底色/关灯切换）——✅ 2026-10-11 用户过目通过（light.scene Play 态四角灯池/暗池/暖锥光 + 启用开关现场切换）
- [x] vtable 59 / 组件 id 32/33 / 系统 21 不变；金回放零重录（无新组件）
- [x] 构建零警告；--validate 全程（smoke-light/回归 script-chain 步）

## 4. 开工拍板（2026-10-11 用户确认）

1. **QualityManager 两壳口径**：批② 编辑器壳先行（QualityManager 进编辑器帧循环 + lightRT 分档 + 光数上限）；lemon-game 壳随批③ 光照装配接入——两壳共用同一 QualityParams 扩展。
2. **gizmo 形态**：Point/Spot 画 range 折线圈；Spot 额外画锥光楔形两边线（张角+朝向直观性，Unity 2D 同款）；Global 不画（range 无意义）；BlobShadow2D 椭圆虚框。
3. **衰减定形（入 02 §8）**：t = clamp(1 − d/range)，att = t²(3−2t)（smoothstep 端点零导数，range 边界无亮环/硬切）；Spot 角向内外角同款 soft 边。

## 5. 超期预案（评审预声明）

- **②a（最小可验收链）**：三段式 + Point 光 + 关灯帧逐位同——S1–S4 落地即达；段豁免可先免（全层受光）后补。
- **②b（拆出项）**：三类型全表 + 段豁免 + QualityManager + benches。

## 6. 完工勾销（机器面 2026-10-11）

- 机器面全绿（数字见 Status 行与 [DevLog](../../DevLog/2026-10-11-m8-b2-lightmap-editor-slice.md)）；②a/②b 预案未触发（全量当日过）。
- 实现偏差三则（DevLog 过程记录）：gizmo 色全不透明化（Alpha 混合拉暗冒烟断言不可寻址）、smokeSeeded_ 播种后重定、Prepare/Bake 拆分（纯 CPU 化）。
- 真人观感 ✅ 2026-10-11（用户 verdict「可以了」）；**移交批③**：SpriteMask 蒙版渲染 + blob 椭圆渲染（数据面/着色器分支已备）+ lemon-game 装配 + `Lemon.Lighting` 门面。
- **评审轮（同日 done 后）**：三片并行深审 46 文件，九发现全即修——P0 light.frag Spot 内半角保护式 max→min（软边塌外缘 + angle=0 NaN）+ P1 双 Bake 删（515→532fps）+ P1 smoke-light 光斑断言改读 lightRT（原 gameRT 同点量的是场景内容，光源不渲也过）；复测 smoke-light 七断言/单测 34,864/bench-lights 532fps 全绿。细节见 DevLog 追记。
