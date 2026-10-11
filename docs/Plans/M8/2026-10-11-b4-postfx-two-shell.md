# M8 批④ —— 后处理链两壳接线（bloom 双滤波 + 色调曲线 + 冲击波 UV 扰动）

Status: 机器面 ✅ 2026-10-11 当日（单测 **34,919**（+31）/ ctest 4/4 / 回归 full **26 步全绿**（两新步首跑绿；fx-smoke 首轮红 = 波寿终帧率顶破已修 ClearWaves 兜底 + 三连绿）/ smoke-fx 编辑器七断言 PASS（--validate）/ fx-smoke 游戏壳 OK（fps≈392–474 ≥60 门禁）/ smoke-light + light-smoke 复跑全绿（direct 路径零扰动）/ bench-survivor 门禁 fps=82 ≥76.5 / vtable 59·id 33·系统 21 不变 / 金回放零重录 / 构建零警告，[DevLog](../../DevLog/2026-10-11-m8-b4-postfx-two-shell.md)）；真人观感归批⑤ 组合走查

- 总览行：[M8.md](./M8.md) §2 批④；设计权威：02 §4 FX Pass 行 + 02 §8 RT 格式拍板（v1 LDR RGBA8Unorm，HDR bloom 列 v1.1）
- 前置：批② 全屏设施（composite 全屏四边形管线/bindless RT 槽/EnsureLightTargets 模式）✅ / 批③ lemon-game 装配 + QualityManager 两壳 ✅
- 预估 2 日；真人观感归批⑤ 组合走查（排程注记：走查须覆盖光+FX 合成观感）

## 1. 输入条件（2026-10-11 探测核证）

1. **两壳终点现状**：编辑器 = 合成/sprite 段渲 gameRT（RGBA8Unorm）→ 豁免段 + RmlUi 同块 → ImGui 采样 gameRT（ViewportRenderer.cpp:552-602）；lemon-game = 开灯帧合成段直写交换链（GameEntry.cpp:1304-1311），关灯帧 sprite 直渲交换链（:1312-1320）。**批④ 重构位 = 渲染目标换 fxSceneRT、显示目标块内录 fxFinal**。
2. **像素断言源**：编辑器 gameRT 读回口现成（GameRenderTarget + DebugRecordTextureCapture）；**lemon-game 交换链截屏现成**（`CommandList::DebugRecordCapture()` + `Device::DebugFetchCapture()`，RHI.h:150/259——BGRA→RGBA 交换已处理）——两壳 FX 后像素断言都有源。
3. **交换链格式偏好 SRGB 变体**（RHI.cpp:626-627）：fxFinal 管线目标格式参数化（LightCompositor compositeTargetFormat 同款）——两壳各维持既有终点语义，色彩空间行为与批②③ 逐字同链（02 §8 gamma 空间拍板覆盖）。
4. **全屏 pass 样板全现成**：composite.vert 角点直推 NDC + push 常量槽位索引；`CompositePushConstants` 16B；`BindQuadGeometry + BindGlobalDescriptors + BindStorageBuffer(环) + PushConstants + DrawQuadInstances(1,0)` 录制式（LightCompositor.cpp:130-139）；kMaxPushConstants=128（fxFinal 需 96B ✓）。
5. **bindless 顶部段**：glow 249 / sceneRT 250 / lightRT 251（Atlas.h kGlowTexSlot + LightTypes.h）；`kAtlasSlotCeiling=249` 硬钳资产页（批③ P2 落地）。批④ 扩 4 槽（fxScene 248 / bloomA/B/C 247-245）→ ceiling 降 245。
6. **QualityManager 两壳齐**（批③）：QualityParams 尾加 `fxChain` 位即两壳同享；EMA 只降不升（Low 降档 = FX 自动关——压测过载时链先卸）。
7. **Bake append 契约**：编辑器关灯帧 FX 开需拆双段（场景段 fxSceneRT + 豁免段 gameRT 块）——`Reserve` 帧容量一次声明前置（批② 开灯帧同款；lemon-game 双 batcher 无此约束）。
8. **冲击波 API 面**：ADR-019 D7 vtable 零新槽——引擎侧 `PostFx::SpawnShockwave`（渲染表现层，不入 StateHash/回放）；C# 门面**本批不做**（无消费者；触发式——批⑤ 或消费者出现时随 Tween/World 非 ECS 通道议，零哈希流影响口径）。

## 2. 文件/行级变更单

### A 引擎渲染件（两壳共用，防第三套）

- [x] **A1 槽位（Atlas.h）**：顶部保留段扩 4 槽常量本位——`kFxSceneTexSlot=248`/`kFxBloomATexSlot=247`/`kFxBloomBTexSlot=246`/`kFxBloomCTexSlot=245`；`kAtlasSlotCeiling` 249→245（硬执行顶部 7 槽段；#L20 记账 +4）。放 Atlas.h 本位避包含环（批③ kGlowTexSlot 先例）。
- [x] **A2 shaders ×4 新**（Shaders/，全部复用 composite.vert）：
  - `bloom_extract.frag`：`out = max(scene − threshold, 0)` 写 bloomA（1/4，线性采样即降采样）；
  - `bloom_down.frag`：4-tap box（uv ± srcTexel·0.5）；
  - `bloom_up.frag`：4-tap tent 上采样；
  - `fx_final.frag`：UV 扰动（waves[4] 环形高斯偏移求和，aspect 校正）+ bloom 加色 + 色调曲线（S 曲线 contrast + luma 暖偏）。
  全部声明 `uAtlases[256]`/`uSamplers[8]`（D10 护栏对象）。
- [x] **A3 CMakeLists.txt**：`lemon_embed_shader` ×4 + D10 foreach 扩 4 frag。
- [x] **A4 Quality.h**：QualityParams + `bool fxChain`（Low=false / Med=High=true）；头注"批④ Med+ 档"兑现（ADR-019 M5 尾注收口）。
- [x] **A5 PostFx.h/.cpp 新**（镜像 LightCompositor 结构）：
  - `PostFxParams`（bloomThreshold=0.72 / bloomIntensity=0.45 / toneContrast=1.06 / toneWarmth=0.10）；
  - `Init(device, samplerSlot, finalTargetFormat)`：4 管线（extract/down/up 恒 RGBA8Unorm + final 按目标格式）+ 自建角点几何 + 设备丢失回调（管线重建 + RT 句柄置空）；
  - `EnsureTargets(w, h)`：fxScene 全分辨率 + bloomA/B/C = 1/4/1/8/1/16（≥4px 钳）+ 建时绑槽（M4.7-P0）+ 尺寸变重建（WaitIdle 停稳，EnsureLightTargets 同款）；
  - `SpawnShockwave(uvCenter, maxRadius, strength, duration)`（kMaxWaves=4，满员顶替最老 + 一次性 WARN）；`Simulate(dt)` 半径推进/幅度衰减/寿终回收；`ActiveWaves()`；
  - `RenderChain(cl)`：extract→down→down→up→up 五离屏块；`RenderFinal(cl)`：已 Begin 显示目标块内录 fxFinal（RenderComposite 同款式）；
  - 纯函数 `ShockwaveOffset(dist, radius, amp)`（ringWidth=0.06 高斯环，CPU 单测/GPU 同式两处同步纪律）+ `ToneApply(c, contrast, warmth)`（CPU 镜像）；
  - `LastFrameInfo()`（waveCount/passed——smoke 与 bench 断言面）。
- [x] **A6 LightCompositor 双目标**：Init 增建 offscreen composite 管线（恒 RGBA8Unorm = fxSceneRT 拍板）+ 既有 direct 管线（传入格式）；`RenderComposite(cl, bool offscreen)` 选管线（批③ 调用点显式化）。编辑器两格式相同 = 建两份同构（代码路径统一，启动期一次）。

### B 编辑器壳（ViewportRenderer）

- [x] **B1 ViewportRenderer.h**：成员 `renderer::PostFx postFx_;` + 访问器 `PostFx()`（smoke-fx 断言面）。
- [x] **B2 ViewportRenderer.cpp RenderViewport**（:551-603 重构三分支，仅 idx=1）：
  - 开灯 + fxChain：场景段 sceneRT → 光照段 lightRT → 合成段 **fxSceneRT**（RenderComposite(offscreen)）→ RenderChain → gameRT 块（RenderFinal + 豁免段 append + RmlUi）；
  - 关灯 + fxChain：场景段（全量 Bake）渲 **fxSceneRT** → RenderChain → gameRT 块（RenderFinal + 豁免段 + RmlUi）——Reserve 帧容量一次声明（append 契约）；
  - Low 档（fxChain=false）：批②③ 原路径**逐字保留**（关链零成本机械口径）；SceneView（idx=0）零改动（编辑视口不吃 FX，SceneView 无光照同因）。
  - EnsureLightTargets 或帧头扩 `postFx_.EnsureTargets(rt.w, rt.h)`（两 FX 分支各自调）。

### C lemon-game 壳（GameEntry.cpp）

- [x] **C1 装配区（:910-952 后）**：`renderer::PostFx postFx; postFx.Init(*device, 0, device->SwapchainFormat());`（声明序先于 RecreateGuard；RT 句柄复位走 PostFx 自登记回调）。
- [x] **C2 帧循环（:1286-1320 重构三分支）**：
  - 开灯 + fxOn：批③ 前段照旧（sceneRT/lightRT）→ 合成段 **fxSceneRT**（offscreen composite）→ RenderChain → 交换链块（RenderFinal + 豁免段 + RmlUi）；
  - 关灯 + fxOn：场景段 fxSceneRT（sceneBatcher RGBA8Unorm 管线）→ RenderChain → 交换链块（RenderFinal + 豁免段 + RmlUi）；
  - else（Low 档）：批③ 两分支**逐字保留**。
  - `postFx.Simulate(rdt)` 挂质量钟旁（渲染帧时钟，表现层语义）。
- [x] **C3 `--smoke-fx`**：CLI + 头注 + 组合守卫（与 --smoke/--smoke-scene/--smoke-light 组合 WARN）+ 引擎侧夹具播种（暗背景 + 白亮块 + 棋盘条带——bloom/位移断言源）；帧机：f30 SetTier(Low) 基线捕获（交换链）→ f60 High（链开）→ f80 捕获 f81 断言（白块加亮 bloom + 全局 tone 差）→ f90 SpawnShockwave → f94 捕获 f95 断言（波环带内差分像素数）→ 波寿终 ActiveWaves==0 → f150 SetTier(Low) → f170 捕获 f171 **与基线逐位同**（关链零成本，编辑器同款口径）→ RESULT + fps≥60 门禁。

### D 编辑器 smoke + bench

- [x] **D1 `--smoke-fx`（EditorAppSmokeFx.cpp 新 TU）**：Launch.smokeFx + CLI + 帧守卫（≥240）+ Seed/Frame/Capture/Sample/Verdict 五件（smoke-light 同构）；断言源 gameRT（编辑态无 Play/UI = fxFinal 纯输出）。
- [x] **D2 `--bench-survivor-lit`（压测 A 干跑）**：Launch.benchSurvivorLit + CLI + 帧守卫（≥600）；bench-survivor 全链（播种 + EnterPlay + Immediate）+ 追播 64 灯 + 8 blob 进 Play 场景 + High 档钉定（FX 链在场）；BenchSample/BenchCaptureStop/BenchVerdict 挂点扩 `|| benchSurvivorLit`；裁决 `[bench-survivor-lit]` fps + alive≥10000 + lights≥64 + tier/fxPass 回显 → 门禁 fps≥60（@60fps 判据；终验归批⑥）。**不进回归/ci-daily**（干跑口径 = DevLog 数字）。
- [x] **D3 回归脚本**：+2 步（编辑器 --smoke-fx --validate + lemon-game --smoke-fx）→ full **24→26**；09 §8/§9 两处口径同步。

### E 单测（tests/engine/PostFxTests.cpp 新）

- [x] ShockwaveOffset：波前峰值/远场趋零/amp=0 恒零；ToneApply：中点带保持/高低端拉伸/钳域；
- [x] bloom RT 尺寸派生（1/4·1/8·1/16 + ≥4px 钳）；QualityParams fxChain 三档；
- [x] 冲击波生命周期（Simulate 推进 → 寿终回收 → ActiveWaves 0；满员顶替）。

### F 文档落账

- [x] 02 §4 FX Pass 行落地注（双滤波 bloom 链形态 + fxFinal 合并式扰动/色调 + Med+ 档 + 关链零成本 + 两壳接线）；
- [x] Quality.h 头注（A4 内）；09 §8/§9（D3 内）；M8.md ④ 行勾销；AGENTS.md:5 基线刷新；DevLog 条目新文件。

## 3. 设计裁决（默认执行，用户可否决）

1. **链形态 = fxFinal 合并式**：扰动/加色/色调合并单 pass（02 §4 顺序 Bloom→Shockwave→Tone 语义保持——bloom 提取基于无扰动图，扰动在最终采样施加）；UV 扰动不设独立 pass = 无活跃波时零额外成本，波计算只是 fxFinal 的 ALU 分支。
2. **bloom = 双滤波 1/4-1/16 三级**（extract@1/4 → down 1/8 → down 1/16 → up 1/8 → up 1/4）：全屏 pass 共 6 次（含 final），LDR 阈值语义（02 §8 拍板——近白像素，HDR 16F 列 v1.1）。
3. **编辑器显示终点照旧**：gameRT 仍是 fxFinal 目标 + ImGui 采样源（"gameRT 终点照旧"= 显示终点；合成终点前移 fxSceneRT——与 lemon-game"合成终点→FX 链→交换链"同构，两壳共用 PostFx 防第三套）。
4. **fxChain 联动 EMA 降档**：Low 档（含自动降档）= 链整体跳过且 RT 不建——过载时 FX 先卸（质量分级的本义）；关链帧与批③ 前路径逐位同（smoke 机器证）。
5. **冲击波引擎侧 API（零 vtable/零哈希流）**：`PostFx::SpawnShockwave` 收归一化屏幕坐标（uv + 相对短边半径）；C# 门面无消费者不预铺（D7）。
6. **bloom 采样不扰动、场景采样扰动**：低频 bloom 光晕对亚像素级位移不敏感（观测不可分），final 内 scene 走扰动 uv、bloom 走原 uv——省一次 aspect 归一（观感无损，公式面简单）。

## 4. 排程（预估 2 日）

日1：A 全部 + 单测 + 构建 → B 编辑器接线 + D1 smoke-fx 过；日2：C lemon-game + D2/D3 + 全量回归 + F 文档。超期预案：D2 干跑可单独后挪（回归不依赖）；A+B+C 为不可拆主干。

## 5. 完工记录（2026-10-11 机器面）

- 数字与过程五条见 [DevLog](../../DevLog/2026-10-11-m8-b4-postfx-two-shell.md)（bloom 断言三改定形分离差分 / 菜单 UI 遮蔽 / 波时序帧率适配 / smoke-light 关灯帧语义细化 / fetch 误吃修复）。
- 裁决兑现：①fxFinal 合并式（无波零成本实测——waveOk 差分仅在波窗口帧命中）；②双滤波三级；③编辑器显示终点照旧（ImGui 采样零改动）；④EMA 降档联动；⑤冲击波引擎侧 API 零 vtable（ClearWaves 增补 = 场景重置正当路径，smoke 兜底同用）；⑥bloom 不扰动、scene 扰动（fx_final.frag：bloom 采 vUV 原 uv、scene 采扰动 uv——低频光晕对亚像素位移不敏感，观感无损）。
- 移交批⑤：真人走查（FX 观感调参面 PostFxParams 四参数）；bench-lights 数字重锚（编辑态 FX 默认在场）；冲击波 C# 门面触发式。
