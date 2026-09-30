# M6a 批③a —— RmlUi 渲染地基：Engine/Ui + RenderInterface over RHI + GameView 静态文档冒烟

Status: done（主体 2026-09-28 勾销；T7 收尾同日过——③c 开工门槛清除）

> [ADR-014](../../ADR/ADR-014-Game-UI-RmlUi-Integration.md) 五子批之首。本批目标 = 引擎侧 RmlUi 呈现通路打通（渲染/上下文/字体/文档加载 + 冒烟验收），**不含** C# API（③c）、资产通道（③b）、模板迁移（③d）。
> 实现前调研结论（2026-09-28，Explore 全量走查 RHI/ViewportRenderer/spike-04/冒烟基建）已折入下文分解。

## 现状盘点（本批动工面）

| 事实 | 出处 | 对本批的含义 |
|---|---|---|
| gameRT 渲染块：`BeginOffscreenPass → SetViewportScissor → batcher.Record → EndPass`，块后 RT 转 SHADER_READ | `Editor/Interaction/ViewportRenderer.cpp:574-578` | **UI 唯一合法插入点 = `Record` 与 `EndPass` 之间**（离屏块 loadOp 恒 CLEAR，块后另开块清内容，`RHI.cpp:1491`） |
| ImGui 先例：编辑器交换链块内经 `cl.NativeCommandBuffer()` 追加录制（`EditorApp.cpp:4607-4610` → `ImGuiBackend.cpp:252-256`） | RHI.h:145-147 豁免口 | 引擎侧 UI 后端同款形态：块内追加录制，但消费者是 Engine/Renderer 兄弟 .cpp |
| 引擎管线布局无 UBO；顶点输入硬编码 quad；`CreatePipeline` 无自定义顶点布局入口 | `RHI.cpp:498-506, 1094-1101` | RmlUi 顶点（pos/色/uv 20B 交错）走**自有管线**，不经 `rhi::CreatePipeline` |
| 动态内存先例 = SpriteBatcher 实例环（hostMapped 常驻 + 3 段环） | `SpriteBatcher.cpp:66-89` | RmlUi 6.3 只给 CompileGeometry（无逐帧顶点流）→ 单池 + VmaVirtualBlock 子分配 + 延迟释放（spike 同款简化） |
| spike-04 后端两黑屏根因：动态 scissor 不设 + CPU_TO_GPU 无 flush（MoltenVK dGPU） | spike backends 改造⑧⑩⑫ | 本批纪律：**帧首钉默认 scissor + 每帧显式 `vmaFlushAllocation`**（一致内存上 no-op） |
| RmlUi 6.3 `GenerateTexture` 像素恒 RGBA **预乘**；`SetScissorRegion` 语义 = 窗口坐标（与 transform 无关） | CPM 缓存 `RenderInterface.h:56-58, 68-70` | 着色器 2 个 frag 变体即可（无 ALPHAMAP——ADR-008"3 变体"按 6.3 实测降为 2 + 1 vert，ADR-014 附录注记）；scissor 可恒用 `vkCmdSetScissor`，无需 stencil 路径（引擎块也无 stencil 可用） |
| 设备丢失 = 句柄全作废 + `AddRecreateCallback` 按序重放 | RHI.h:196-206，ViewportRenderer 先例 | RmlUiBackend 全部 Vulkan 态（管线/池/纹理）走 recreate 回调重建 |

## 任务分解

### T1 RHI 内部桥（`Engine/Renderer/RHI.h` + `RHI.cpp`）

- RHI.h 末段（Device 类内，`GetVulkanTextureViewInterop` 之后）新增：
  - `struct InternalBridge { void* device; void* physicalDevice; void* allocator; void* queue; uint32_t queueFamily; }`（全 void*，头文件零 Vulkan 类型，与 `VulkanInteropHandles` 同豁免逻辑）
  - `const InternalBridge& GetInternalBridge() const;`
  - `using ImmediateRecordFn = void (*)(void* commandBuffer, void* userData); void InternalImmediateSubmit(ImmediateRecordFn, void*);`（包装 `Device::Impl::ImmediateSubmit`，`RHI.cpp:671-689`）
- RHI.cpp 实现：`GetInternalBridge` 填 Impl 字段（懒构造一次性）；`InternalImmediateSubmit` 转 `m->ImmediateSubmit`（VkCommandBuffer 经 void* 透传给回调）。
- `NativeCommandBuffer()` 注释扩一个消费者：Renderer 内兄弟后端（RmlUiBackend，gameRT 块内追加录制）。

### T2 着色器（`Engine/Renderer/Shaders/`）

- `rmlui.vert`：push constant 72B（`mat4 mvp` + `vec2 translate`）≥ 位置 vec2 / 色 R8G8B8A8 / uv vec2 三属性；`gl_Position = mvp * vec4(pos + translate, 0, 1)`。
- `rmlui_color.frag`：`oColor = vColor`（预乘直出）。
- `rmlui_texture.frag`：`set0 binding0 combined sampler`，`oColor = texture(uTex, vUV) * vColor`。
- `Engine/CMakeLists.txt` 追加三个 `lemon_embed_shader(lemon-engine ...)`；符号声明入 `Renderer/EmbeddedShaders.h`（extern 数组）。

### T3 RmlUiBackend（`Engine/Renderer/RmlUiBackend.h/.cpp`，本批主体）

- 头文件零 Vulkan/RmlUi 类型；`class RmlUiBackend { Init(Device&, Format); Shutdown(); BeginFrame(CommandList&, w, h); EndFrame(); void* RenderInterfacePtr(); }` Pimpl。
- .cpp（Renderer 豁免区，VmaVirtualBlock 不需要 VmaAllocator 句柄、池 buffer 经桥 allocator 创建）：
  - **管线**：自有 layout（set0 = combined image sampler ×1 + push constant 72B vert|frag）；动态渲染（`VkPipelineRenderingCreateInfo` pNext，格式 = gameRT RGBA8Unorm）；顶点输入 1 绑定 20B 三属性（R32G32F @0 / R8G8B8A8 @8 / R32G32F @12）；动态态 viewport+scissor；预乘混合（ONE / ONE_MINUS_SRC_ALPHA，双通道 ADD）；两管线（有/无纹理 frag）。
  - **内存池**：单 VMA buffer（VERTEX|INDEX，CPU_TO_GPU + HOST_SEQUENTIAL_WRITE + MAPPED，8MB）+ `VmaVirtualBlock` 子分配；`CompileGeometry` 常驻子分配（顶点+索引两段，memcpy `Rml::Vertex` 零转换、索引 uint32）；`ReleaseGeometry` 入 3 帧延迟释放环（对齐在途帧 = `kFramesInFlight` 2 + 余量 1）；`EndFrame` 全量 `vmaFlushAllocation`（改造⑫；一致内存 no-op）。
  - **纹理**：`LoadTexture`（路径加载）③a 返回 0 + 一次性 LEMON_WARN（图片解码归 ③b 资产桥）；`GenerateTexture`：VMA GPU-only RGBA8 + staging + 桥 `InternalImmediateSubmit`（barrier UNDEFINED→TRANSFER_DST→SHADER_READ_ONLY）+ 独立描述符集（懒建缓存于纹理对象）；`ReleaseTexture` 3 帧延迟销毁。自有 descriptor pool（128 combined sets）+ linear/clamp 采样器。
  - **帧协议**：`BeginFrame` 存 `VkCommandBuffer` + 尺寸、算投影（像素→NDC，y 向下）、**先钉全幅 scissor+viewport（改造⑧）**；`RenderGeometry`：绑管线（按有无纹理）→ push 72B（mvp = 投影 × SetTransform 矩阵（非空时）/ 投影）→ 绑 VB/IB（offset）→ 有纹理绑 set0 → `vkCmdDrawIndexed`；`EnableScissorRegion/SetScissorRegion` → clamp 后 `vkCmdSetScissor`（6.3 语义 = 窗口坐标，无需 stencil）。
  - **SetTransform**：非空矩阵折进 mvp（CPU 侧乘法，零额外管线）；`EnableClipMask/RenderToClipMask/PushLayer` 等 v1 不实现（默认 no-op/0 + 一次性 WARN——RCSS filter/层合成超 ③a 红线）。
  - **设备丢失**：`AddRecreateCallback("rmlui-backend")` 重建全部 Vulkan 态；`Shutdown` 反注册 + WaitIdle 后销毁。

### T4 UiSubsystem（`Engine/Ui/UiSubsystem.h/.cpp`）

- 头文件零 RmlUi 类型（Pimpl）；`Init(Device&, Format) → bool`、`Shutdown`、`LoadDocumentFromMemory(name, rml) → bool`、`ShowDocument(name, bool)`、`Update()`、`Render(CommandList&, w, h)`（内部尺寸变化 → `Context::SetDimensions`）。
- .cpp：`Rml::SetSystemInterface/SetRenderInterface/Initialise` + `CreateContext("game")`；SystemInterface = `LogMessage` 转 LEMON_LOG/WARN/ERROR + `GetElapsedTime`（steady_clock）；字体 = 系统降级链（Hiragino Sans GB → STHeiti → Songti，spike 同款；**③b 换 Noto Sans CJK 随引擎带**）。
- 命名：`lemon::ui`；RmlUi 类型只在本 .cpp。

### T5 接线（Editor）

- `ViewportRenderer.h/.cpp`：
  - 头文件加 `class IGameUiLayer { virtual RenderGameUi(cl, w, h) }` + `SetGameUiLayer()` + `GameRenderTarget()`（冒烟回读用，对照 `:138` SceneRenderTarget）+ 成员 `gameUi_`。
  - `RenderViewport`（`:577` 后 `:578` 前）：`if (idx == 1 && gameUi_ && ctx.Playing()) gameUi_->RenderGameUi(cl, rt.w, rt.h);`
- `EditorApp.h/.cpp`：
  - 成员 `std::unique_ptr<lemon::ui::UiSubsystem> gameUi_` + 匿名 adapter（IGameUiLayer → UiSubsystem::Render）。
  - Init（`viewport_->Init` 后，≈`:2856`）：创建 + Init 成功才挂层（字体链全败 = 不挂 + 红字，编辑器照常跑——UI 缺席不阻断编辑器）。
  - 主循环：Play 中、`UpdateGameCameraFollow()`（≈`:3584`）后 `gameUi_->Update()`。
  - Shutdown（`viewport_.reset()` 前）：`gameUi_->Shutdown()`。
- `EditorEntry.cpp` + `EditorApp.h`（EditorLaunch）：`--smoke-uirml`（隐含 smoke+play；frames==0 时默认 180）。

### T6 冒烟（`--smoke-uirml`，EditorApp.cpp）

- 播种（Init 后）：`LoadDocumentFromMemory("smoke", 内嵌 rml)`——深色面板 + 品黄标题 + 正文三要素（对齐 spike 三判据①的像素级验证形态）；`ShowDocument(true)`。
- 末帧（`:4629` 场景 RT 回读同款）：`cl.DebugRecordTextureCapture(viewport_->GameRenderTarget())`；循环后 `DebugFetchTextureCapture` + `CountPixelsNear`（`:1318-1340` 既有助手）断言面板底色/标题色像素数过阈。
- VERDICT：`[lemon] smoke-uirml: doc=%d font=%s panel=%u title=%u => OK/FAIL`；失败 → exitCode 1。
- **回归脚本不动**（`tools/editor-regression.sh` 14 步维持——smoke-uirml 接入回归随 ③c 契约断言一起做，批文件登记）。

### T7 文本输入微 spike（spike/04 扩展，可独立后置半日）✅ 2026-09-28（三判据全过，一次过）

- 交付：`data/test.rml` 加 `<input type="text" id="name" maxlength="12">`（起名用例 + `:focus` 高亮）；main.cpp 重构 `SpikeSystem : SystemInterface_SDL`——原裸 `Rml::SystemInterface` 的 `ActivateKeyboard/DeactivateKeyboard` 是 no-op，继承 SDL 版后接通"`<input>` 聚焦 → RmlUi 自动调 `ActivateKeyboard(光标绝对坐标, 行高)` → `SDL_SetTextInputArea`/`SDL_StartTextInput`"候选窗通路（调用点 `WidgetTextInput.cpp:1615` 核心自带）。后端两处 spike 改造：**⑬ `Backend::GetWindow()`**（SystemInterface_SDL 构造需窗口）+ **⑭ `Backend::GetTextInputHandler()`**（事件循环早就在 `SDL_EVENT_TEXT_EDITING` 分支喂 `HandleEdit`，但 IME handler 从未注册给上下文——经 `Rml::CreateContext` 第 4 参接上）。
- 注入器三件（main.cpp）：`PushTextInput`（TEXT_INPUT）/ `PushTextEditing`（预编辑——真实 IME 事件序 `EDITING(串)→EDITING(空)→INPUT(提交)`）/ `PushKey`（KEY_DOWN/UP 成对）；断言 = `GetValue()` **UTF-8 逐字节比对**（乱码必字节不等——判据①的机器化）。
- 三判据实测（`build/mac/spike/04-rmlui/lemon-spike-rmlui`，VERDICT `text=OK(ime=OK route=OK) => PASS`，exit 0）：
  - **① 中文提交零乱码**：IME 事件序提交"柠檬" + 纯 TEXT_INPUT"骑士" → 值逐字节 == `柠檬骑士`（4 字，LengthUTF8 核对）；
  - **② 候选窗贴光标**：聚焦即 ActivateKeyboard，光标坐标随打字推进（1052→1084→1116，行高 19），锚点 (1100,278.8) 落输入框 (1040,269) 344×39 内；失焦 DeactivateKeyboard 成对；
  - **③ 事件不串**：裸 KEY_DOWN('N') 不插字（字只能走 TEXT_INPUT 通道）/ RETURN 的 `'\n'`（平台层 key_down 分支转 `ProcessTextInput('\n')`）被单行 input 吞 / ←→ 只移光标 / 退格恰删一字 / 点按钮失焦出 change。
- **结论（ADR-014 D4 定案）**：RmlUi 自带 `<input>` 质量过关，③c 提交制文本输入直接吃核心控件，**M8 自定义元素兜底不触发**。
- 对 ③c 的两条白送发现：**change 在每个提交边界派发**（每次文本落定 + 回车，非只 blur）——M3 的 change/submit 映射有现成语义底座（注意过滤"逐键 change"，D4 语义是提交制）；**光标每次移动都重发 ActivateKeyboard**——候选窗跟随光标是核心自带行为，引擎无需自建机制。真人 IME 视觉确认（候选窗实贴光标）✅ 2026-09-30（随 ③c 真人验收覆盖，[DevLog](../../DevLog/2026-09-30-acceptance-b3c-input-events.md)），机制面已全部机器化。

## 验收判据（全过才勾销）

1. ✅ `cmake --preset mac && cmake --build --preset mac` 全绿（新增 Engine/Ui + RmlUi 链入 lemon-engine；RMLUI_SHELL OFF 瘦身——spike 用本地后端拷贝不受影响）。
2. ✅ `./build/mac/Editor/lemon-editor --smoke-uirml --frames 180 --validate`：VERDICT OK（panel=102652(>3000) title=791(>20) body=374(>20)，font=Hiragino Sans GB）+ **验证层零错误** + exit 0。
3. ✅ 既有回归 `tools/editor-regression.sh` 14/14 不回归（**复跑全绿**；首跑 13/14——`--save-scene` 早退路径触发 UiSubsystem 析构断言崩溃，改防御性收尾修复，见 [DevLog](../../DevLog/2026-09-28-m6a-b3a-rmlui-renderer.md)）。
4. ✅ `--smoke-uirml --screenshot` 目检（**真人验收 ✅ 2026-09-28**：首轮抓到纵向翻转 → 热修 `025c221` → 二次目检通过"看着正常了"；上半幅集中断言机器化防复发，详见 [DevLog 追记](../../DevLog/2026-09-28-m6a-b3a-rmlui-renderer.md)）。
5. ✅ T7 文本输入微 spike（2026-09-28 收尾）：`lemon-spike-rmlui` 一次过，VERDICT `doc=OK font=OK click=3 text=OK(ime=OK route=OK) => PASS` + exit 0；全量构建零波及（spike 目标隔离，其余目标 no work to do）；真人 IME 视觉确认 ✅ 2026-09-30（随 ③c 真人验收覆盖）。

### 实现期发现（偏离批文件预设计的落账）

- **着色器 3 变体 → 2 变体**：RmlUi 6.3 `GenerateTexture` 像素契约恒 RGBA 预乘（字体图集同路），无 ALPHAMAP 需求——ADR-008 D2"3 shader 变体"按 6.3 实测收敛为 vert+color+texture 三件。
- **RCSS font-family 不支持逗号回退列表**：整串被当作单一族名（实测日志实锤）——冒烟文档按 `LoadedFontFamily()` 单值注入；③b Noto 唯一正字后此耦合自然消失。
- **Pimpl 纪律补一条**：`= default` 内联构造/析构会在使用方 TU 实例化 `~unique_ptr<Impl>`（构造异常路径析构已构成员）——Pimpl 类五件套全部声明在头、定义（哪怕 `= default`）在 cpp。
- **进 Play 门**：`playTest` 的进/出往返与 `smoke` 门绑定（`EditorApp.cpp` 3224 块）；smoke-uirml 独立裁决链补独立进 Play 分支（同判据 PlayBlockedByScripts）。
- **描述符池**须 `FREE_DESCRIPTOR_SET_BIT`（纹理逐集释放；验证层 VUID-00312 实抓）。

## 风险与既知边界

- **池容量**：8MB 固定，耗尽 = CompileGeometry 返回 0（RmlUi 跳过该几何）+ LEMON_ERROR 计数；增长策略（WaitIdle 重建迁移）留 ③c 视实测需要。
- **图片纹理**：`LoadTexture` 返回 0（文档内 `<img>` 不显示，CSS 底色/边框/文字全正常）——③b 资产桥接上。
- **filter/层合成/clip-mask**：v1 红线外（RCSS 常规属性不受影响）。
- **手柄/键盘输入进 RmlUi**：③c M7 路由；③a 冒烟为纯像素断言（无交互注入）。
- 编辑器 GameView 尺寸/DPI：RmlUi 上下文按 gameRT 物理像素走（Context 坐标 = RT 像素），显示缩放由 ImGui letterbox 承担——与 spike 改造⑤的窗口密度问题不同路径，无需 density 换算。

## 关联

- [ADR-014](../../ADR/ADR-014-Game-UI-RmlUi-Integration.md)（D1 ③a 定义 / D2 契约 / D3 落位）
- [ADR-008](../../ADR/ADR-008-Runtime-UI-Strategy.md)（D2 接入形态原文 + spike 三判据 + 回退条件）
- spike 参考：`spike/04-rmlui/backends/`（改造①-⑫注释）+ [DevLog 2026-09-22](../../DevLog/2026-09-22-m5-clear-4-rmlui-spike.md)
- 后续：③b 字体资产（done 同日）→ ③c C# API（T7 门槛已清 2026-09-28）→ ③d 模板迁移 → ③e 图鉴
