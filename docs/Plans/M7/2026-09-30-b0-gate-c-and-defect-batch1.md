# M7 批⓪ —— Gate C 清障 + 缺陷评审第一批 + demo 入 git（2026-09-30）

Status: done（2026-09-30；Windows 真机编译验证与 CI runner 首跑两个尾巴见文末）

## 背景与输入

[架构评审](../../Reports/2026-09-30-architecture-and-defect-review.md)（48 条缺陷）+
[工程建议书](../../Reports/2026-09-30-engineering-recommendations.md)（R1/R3/R5）同日出具；
用户确认复核结论并三拍板路线图重排（[DevLog](../../DevLog/2026-09-30-roadmap-reorder-windows-first.md)）。
本批 = 新顺序的"第 0 批清障"：纯收益无风险的缺陷第一批（评审 §8）+ Gate C 两前置 +
R5（demo 入 git）。同日缺陷复核记录见 [DevLog](../../DevLog/2026-09-30-defect-review-verification.md)。

## 任务分解与完成情况

### A. 缺陷评审第一批修复（评审 §8 第一批 + D2 上提）

| 项 | 修复 | 文件 |
|---|---|---|
| D1 IME 预编辑静默失效 | 构造函数 `impl_(std::make_unique<Impl>())` + `HandlerPtr()` 断言 | `Engine/Ui/SdlTextInputHandler.cpp` |
| D2 resize 后旧 imageIndex present | RHI：`RecreateSwapchain` 置 `imageIndex=UINT32_MAX` 哨兵 + `EndFrameAndPresent` 头部断言（把静默 UB 变响亮失败）；六处调用点（EditorApp + 5 Samples）改为"重建后必 `continue`，下一帧重新 acquire" | `Engine/Renderer/RHI.cpp`、`Editor/App/EditorApp.cpp`、`Samples/{anim-smoke,bench-sprites,bench-mow,bench-particles,rhi-smoke}/main.cpp` |
| D3 SpriteBatcher 回调重复登记 | `Init()` 开头先 `RemoveRecreateCallback(recreateCbId_)`（幂等）；`ViewportRenderer::OnDeviceRecreated` 不再重 Init 两个 batcher（其自登记回调注册序在前、已重建——原路径每次设备丢失净漏 2 buffer + 2 shader + 4 pipeline） | `Engine/Renderer/SpriteBatcher.cpp`、`Editor/Interaction/ViewportRenderer.cpp` |
| D10 uAtlases[64] vs kMaxTextureSlots=256 | shader 数组改 256 + **CMake 配置期一致性护栏**（`Engine/CMakeLists.txt` 读两侧正则比对，错配 FATAL_ERROR；阴性验证过：改 128 → configure 报错，还原 → 通过） | `Engine/Renderer/Shaders/sprite.frag`、`Engine/CMakeLists.txt` |
| M6 剔除区 4× 虚胖 | `SetViewport` 传半宽/半高（原来传全宽全高） | `Editor/Interaction/ViewportRenderer.cpp` |
| M1 键桶满静默丢弃 | `droppedSprites/droppedParticles` 首次丢弃时 `LEMON_WARN`（只响一次，模式照 `warnedSanitize_` 先例） | `Engine/Renderer/Renderable.{h,cpp}`、`Particles.{h,cpp}` |
| M26 日志行交错 | `fprintf/fflush` 移入 `gLogMutex` 锁内 | `Engine/Core/Log.cpp` |

### B. Gate C ①：CI 最小落地（09 §9 push 门禁）

`.github/workflows/ci.yml`：macOS runner，push/PR 触发 `cmake --preset mac` 构建 +
`ctest` 三项（engine-tests / imgui-isolation / script-tests）。**每日回归 + 性能基线
门禁 + Windows runner 仍待**（09 §9 全量口径，批① 补齐）。**runner 首跑待 push 后观察
调通**（brew 依赖清单按 AGENTS.md 工具链）。

### C. Gate C ②：Windows 编译阻断项清零（07 §3.6）

五条全数处置 + `win`/`win-debug` preset 入 CMakePresets（VS2022 x64 多配置；细节与
逐项状态见 [07 §3.6](../../EngineDesign/07-Porting-Matrix.md) 处置状态列）。新增
`Engine/Core/Process.h`（进程 id 跨平台）与 `Engine/Core/FileOps.{h,cpp}`（原子替换
改名：Win `MoveFileExW(REPLACE_EXISTING)` / POSIX `fs::rename`）。

### D. R5：`demo/` 入版本控制

`.gitignore` 摘除整目录忽略 `demo/` 一行；既有规则（`.lemon/`、`[Bb]in/[Oo]bj/`、
`.DS_Store`）天然挡住生成物（saves/manifest/构建中间态），源码（Scenes/Prefabs/Game/
Assets/tables/UI）入库。`ats/`、`etest/` 维持本地定位（不承载验收判据，已在 08/07
文档口径中说明）。**验收判据归属收口**：M6a 验收① 的被测对象 `demo/svr-test` 从此在
版本控制内，CI 有了被测对象。

## 验收

- 构建：`cmake --preset mac` + build ✅（唯一 warning 为改动前既存 `unused parameter`）；
- 回归：`tools/editor-regression.sh full` **16/16 PASS**（ctest 3/3 + 编辑器冒烟 13 步）；
- D10 护栏阴性验证 ✅（错配 → configure FATAL，还原 → 通过）；
- D2 修复语义：六处调用点重建成功后跳帧，RHI 哨兵断言兜底新调用方。

## 尾巴（不阻断本批勾销）

1. **Windows 真机编译验证待首次**——MSVC 侧只能保证 macOS 不回归；07 §3.5 行为验证
   （含 ④ 的 codepage/中文路径）与 Gate C ② 最终勾销以该次为准；
2. **CI runner 首跑调通待 push**——brew 安装耗时/CPM 网络取包/ctest 无 GPU 形态按
   09 §9 预期为逻辑面门禁，异常则修 workflow；
3. IME 预编辑"可达"冒烟断言（评审 D1 修复方向第三条）未做——需要 SDL_TEXT_EDITING
   合成事件注入链，归缺陷后续批与 §7.2.4 语义句柄改造合并考虑。
