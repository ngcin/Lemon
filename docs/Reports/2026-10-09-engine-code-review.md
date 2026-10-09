# Lemon 引擎侧代码评审（引擎面专项，基线 ab0d35f）

> 2026-10-09 · 工作流评审产出：9 个专项评审员并行深审（8 纵向模块 + 1 横向架构），每条发现由独立
> 复核员**重读代码**验证（去重后 65 项：高危 3 / 中危 23 / 低危 39；复核成立 64 / 不成立 1）。
> 范围：`Engine/` 全部引擎自有代码（137 个 C++ 文件 + 29 个 C# 宿主文件 Lemon.SDK/Lemon.Entry）；
> `Editor/` 与第三方库按本次要求排除（上一轮 Engine+Editor 全量评审 =
> [2026-10-02-code-review-2fce9f0.md](./2026-10-02-code-review-2fce9f0.md)）。
> 基线 main@ab0d35f（工作树仅 demo 未跟踪资产）。一次性快照：产出后不更新；修复进度在
> Plans/DevLog 跟踪，引用记法「review 2026-10-09 #N」。

---

- **评审对象**：Lemon 引擎侧（`Lemon/Engine/` 137 个 C++ 文件 + `Lemon.SDK`/`Lemon.Entry` 29 个 C# 宿主文件；`Editor/` 与第三方库按用户要求排除）
- **评审日期**：2026-10-09
- **产出**：9 位专项评审员（8 纵向模块 + 1 横向架构）静态评审 → 每条发现由独立复核员只读复核 → 本报告综合
- **结果概览**：66 条原始发现，去重后 **65 项**：高危 3 / 中危 23 / 低危 39；**64 项复核成立**（部分含定性修正，已按修正后口径收录），**1 项复核不成立**（L35，保留收录）

## 总结论

引擎侧代码整体质量高：分层纪律（依赖向下、Vulkan 零泄漏经确定性检查证实）、确定性/回放契约、设备丢失恢复体系、历史 review 修复的落地情况均扎实，未发现系统性架构缺陷；热路径无明显每帧冗余同步，核心正确性风险集中在少数「用户输入直达的边界」上。

最要紧的三件事：

1. **3 处高危缺陷需优先修复**，共性是「用户输入直达的边界缺少存活/类型校验」：脚本对失效实体句柄 `SetComponent` 静默注入幽灵组件并污染 entt 池（H1，Release 下完全静默）；坏 `.anim` 字段类型经 nlohmann 异常穿透直达 `std::terminate`，boot 崩溃/编辑器丢未保存工作（H2）；C# 公开 UI API 对容器元素 `SetInnerRml` 后，缓存的 Rml 裸指针悬挂成 use-after-free（H3）。
2. **中危面集中在两组**：其一是坏档/坏数据触发恒生效断言 abort 的脆面（资产侧 4 条「崩溃缝」+ ImmediateSubmit 的 DEVICE_LOST abort），与引擎自立的「坏档红字跳过」契约相悖；其二是设备丢失恢复体系的边角（RmlUi 旧句柄跨块释放、离屏 RT 跨提交 WAR 竞态）与稳态分配/装载性能（TargetBoard 每帧重建、spawn 全量 JSON 重解析、逐实体跨线程往返、资产 GUID 查询 O(N)、每张纹理一次全设备同步）。
3. **架构债登记后增量收敛**：59 槽双侧手工镜像 vtable、World 通道聚合 hub、Assets↔Renderer 头级双向依赖——当前以纪律管理、无功能问题，但同步成本随功能线性增长。

无崩溃级发现的模块（音频、核心平台入口的低危项均不致崩溃）与已被同构代码修复覆盖的面，可按常规节奏处理。

## 评审方法与覆盖

**流程**：9 位专项评审员静态评审（8 个纵向模块 + 1 个横向架构）→ 每条发现由独立复核员**逐条只读复核**（亲读代码与 vendored 第三方源码；复核员之一另跑独立 C++ 片段实测了 `std::stoull` 的异常行为）→ 本报告综合。全部结论为静态分析；**未做动态验证**（未运行构建、测试套件、sanitizer 或基准）。

**覆盖与口径核对**（本次综合阶段重跑）：

- `find Lemon/Engine -name '*.cpp' | wc -l` → 56；`-name '*.h'` → 82（含 vendored `Engine/Audio/thirdparty/miniaudio.h`），合计 138；扣除 thirdparty 内 1 个 .h 后 = **137**，与给定覆盖数一致（thirdparty 的 `.c` 文件不在 C++ 口径内）。
- `find Lemon/Engine/Scripting/dotnet -name '*.cs' | wc -l` → 42；扣除 `obj/` 生成物 12 个与 `TestScript` 测试项 1 个 = **29**，与给定口径（Lemon.SDK / Lemon.Entry）一致。

**Vulkan 泄漏确定性检查重跑**：`grep -rInE '(Vk|Vma|VMA|vk|vma)[A-Za-z_]' Lemon/Engine --include='*.cpp' --include='*.h'` → **481 处**命中（给定材料为 432 处，差异属 pattern 宽窄，无实质影响）；Renderer 之外 **4 处**：`GameEntry.cpp:467/475` 两处均为 `//` 注释（与核心平台评审员的逐行归类一致，本次重跑再证），另 2 处在 vendored `miniaudio.h:92772/92839` 的 `MA_DR_MP3_VMAC` 宏（第三方，范围外）。**结论：渲染内核之外的一方代码无代码级 Vulkan/VMA 类型使用，分层硬规无违规。**

**综合阶段抽查**（3 条高危 + 断言宏逐行亲读，全部属实）：`ScriptHost.cpp:98-110`（NativeWrite 全程无 `Alive` 闸）、`PlayCaches.cpp:48-65`（四处裸 getter）、`UiSubsystem.cpp:216-224` 与 `:983-986`（裸指针缓存 + SetInnerRml 直调）、`Log.h:42-53`（LEMON_ASSERT release 恒生效 + `std::abort`）。

**排除项**：`Editor/` 全目录（按用户要求；编辑器侧文件仅作为影响面佐证被引用）、第三方库（ImGui/RmlUi/VMA/entt/nlohmann/miniaudio/FreeType，vendored 源码仅被复核员用来确认行为）、`Samples/`/`tools/`（仅作佐证，未独立评审）、`Prowl*`/`duality` 等参考目录。

## 跨模块主题

### 主题一：「同构双实现，修复只落一半」

同一逻辑存在第二份内联拷贝或平行实现，一边按 review 修复加固、另一边未同步——这是本次多条缺陷的共同根因：

- H2：`PlayCaches.cpp` 内联 clip 解析器漏掉 `AnimAsset::ParseClipJson` 已修（review 2026-10-02 #30）的 type_error 防护；
- M15：`AssetIndex.cpp` 全文件非抛解析纪律中唯一一处 `std::stoull`；
- L2：RHI 全部映射内存写路径缺 `RmlUiBackend.cpp:379` 已有的 `vmaFlushAllocation` 纪律（后者注释明记 dGPU 实测教训）；
- M22：`Window.cpp` 为失焦卡键写的清键修复落在 `IsKeyDown` 上，lemon-game 直读的 `InputCollector::keyDown` 未同步；
- M5：`FxChannel.cpp` 定长 `memcpy` vs 同功能 `RtUiChannel` 的 `snprintf`；
- M10：`Events.DispatchPackets` foreach 活表 vs `GameUI.DispatchEvents` 的快照；
- M9：`lemon_scene_event` 内联执行 vs `lemon_ui_events_dispatch` 的池化域线程投递；
- L11：`SpatialHash::Rebuild` 漏掉 `TargetBoard::Grid::Build` 已修（review #19）的 NaN 防御。

修法模式统一：把加固下沉到唯一实现（如 H2 直接改调 `ParseClipJson`），或建立「改 A 必查同构 B」清单。

### 主题二：「响亮失败」纪律在用户数据面上的反噬

LEMON_ASSERT release 恒生效（Log.h:42-53）+ 异常无捕获穿透，使设计上应「红字跳过」的坏档变成进程 abort/terminate：资产侧 4 条崩溃缝（H2、M13 sliceCount 不对账、M14 LBF1 尺寸回绕、M15 stoull 抛异常），加上 M3（ImmediateSubmit 对 DEVICE_LOST 直接 abort）——均与引擎自己写下的契约文本（「坏 clip 红字跳过不炸 Play」「坏账清零、宁缺勿错」）相悖。`.meta`/`.anim`/场景档是用户可手编 JSON、`.lemon/baked/` 是第三方项目内容，都是真实输入面；这些路径的失败应是可恢复的红字，而非进程终止。

### 主题三：稳态零分配纪律执行不齐

口径明文（Systems.h:89 等）且同文件多有成员缓冲复用先例，但热路径仍残留分配：M7（DeclareTeams 每帧 clear+重建丢弃全部堆容量）、L12（Director/Spawn 每 tick 局部 vector）、L3（DrawTextEx 每调用新建 vector）、L26（UI ops 逐参数物化 std::string）、L16（Fx.Text 每命中 ToString 分配）、M11（attach/destroy 逐命令 new Command + 同步跨线程往返）、M4（每张纹理建销 staging + vkQueueWaitIdle 全设备同步）、M17（spawn 每发全量 JSON 重解析）、M19（音频回调逐帧 SPSC Read）。多数修法就是把同文件现成先例（chunkIntents_/dispatchBuf_/PostBatchTick 池化）搬过去。

### 主题四：设备丢失恢复体系的边角漏洞

恢复体系本身成体系且验收过（HandleDeviceLost 双回调 + token 反注册契约 + SimulateDeviceLoss 钩子 + 帧循环三分支），剩余风险面在：入口 ImmediateSubmit 对 DEVICE_LOST 直接 abort 绕过恢复（M3）；重建后 RmlUi 旧几何句柄在新 vblock 上跨块释放、污染 TLSF 自由链表（M2）；全量重导把 M4 的 N 次全停重演一遍；`game-assets` 回调按引用捕获 main 栈对象且不反注册（L37 埋雷）；持久离屏 RT 跨提交 WAR 竞态（M1，帧间只有 host fence 无 GPU 端依赖）。

### 主题五：用户可达边界上的句柄/指针/账本生命周期不设防

脚本缓存句柄跨帧对死实体 emplace（H1，ARPG 磁吸/锁定/AI 缓存目标为标配场景，Debug 响亮 abort、Release 静默池污染）；UI 容器裸指针对 DOM 内突变不失效（H3）；流式 clip 起播重读头与注册期快照不比对（L23）；EntityRef 前缀解析吞格式垃圾（M21）；ResolveScene 放行 `..` 越根（L19）；warn-once 表只增不清（L17）。共性是：内部约定成立的前提（句柄存活/格式合法/账实一致）在外部输入下不成立，而边界层不做验证——修复模式是「边界处校验 + 失效即红字」。

### 主题六：边界面随功能线性膨胀（登记型架构债）

59 槽 C++/C# 双侧手工逐字节镜像的 vtable 已成 god-interface（M23）、World 聚合 9 类 11 个非 ECS 通道成 hub（L39）、Assets↔Renderer 头文件级双向依赖靠纪律维持（L34）。三者同根：每加一个 C# 能力就在 vtable 与 World 各长一份镜像面。当前以表尾追加 + LegacyFrozenBytes 冻结区 + 判空降级纪律管理，未出功能问题，但同步成本与漂移风险随槽位线性增长，建议按「新能力优先走既有 ops 队列」的增量策略收敛。

## 发现明细

按严重度排序（高→中→低）；同一严重度内按模块分组；同根因跨评审员发现已合并并标注。ID 规则：H=高危、M=中危、L=低危。

| 模块 | 高 | 中 | 低 | 合计 |
|---|---|---|---|---|
| 渲染内核（Vulkan） | 0 | 4 | 4 | 8 |
| ECS 核心 | 0 | 1 | 6 | 7 |
| 组件与系统 | 0 | 3 | 2 | 5 |
| 托管脚本宿主 | 1 | 4 | 5 | 10 |
| 资产系统 | 1 | 5 | 4 | 10 |
| 音频系统 | 0 | 2 | 2 | 4 |
| UI 与序列化 | 1 | 2 | 3 | 6 |
| 核心平台入口 | 0 | 1 | 7 | 8 |
| 架构分层与边界（横向） | 0 | 1 | 6 | 7 |
| **合计（去重后）** | **3** | **23** | **39** | **65** |

类别分布：缺陷 39 / 优化 21 / 设计 5。状态：64 项复核成立（含定性修正后收录），1 项（L35）复核不成立、保留收录。

### 高危（3）

#### H1 对死亡/失效实体句柄 emplace = 幽灵注入与 entt 池污染
- **位置**：`Engine/Scripting/ScriptHost.cpp:104`（NativeWrite）；同款缺口：ApplyStructural case 2/4（:1154-1175）、AttachBehaviour（:991-999）；C# 入口 `GameObject.cs:124`、`SceneOps.cs:55-80`
- **类别/严重度/状态**：缺陷 · high · ✅ 复核成立（本次综合抽查再证）
- **问题**：对已死亡/版本失效的实体句柄调 `GameObject.SetComponent`（或 AddComponent/AttachScript 结构命令）会把幽灵实体静默注入组件池：entt 对死亡实体的 `readFn` 必返回 null → 走 `emplaceFn`，而 `try_emplace` 不校验实体存活。脚本缓存目标句柄跨帧（AI/磁吸/锁定，ARPG 标配）即触发数据污染与延迟崩溃；实体号被回收复用时更可致同 index 双条目、池结构损坏；幽灵实体还会进入 `View`/系统遍历（`Scene::Each` 有 valid 过滤，View 直接遍历 packed 无过滤）。
- **证据**：ScriptHost.cpp:98-110 全程无 `g_scene->Alive(ent)`（对比 :84 `NativeIsAlive` 明明存在）；ComponentCatalog.cpp:349-352 → Scene.h:41-51 直转 `registry_.emplace`；vendored entt `sparse_set.hpp:344-378` 仅断言非 null/tombstone。复核补充：`registry.hpp:605` 的 `ENTT_ASSERT(valid)` 仅 Debug 生效（本机 `build/mac` 为 Release `-O3 -DNDEBUG`）——Debug 下响亮 abort，Release 下完全静默，「静默注入」在 Release 成立；SDK 契约把校验责任全推给脚本作者（GameObject.cs:20-23 注释）。
- **建议**：NativeWrite 的 emplace 分支、ApplyStructural case 2 与 AttachBehaviour 的 `Emplace<ScriptBox>` 前加 `g_scene->Alive(ent)` 闸（失效 = 红字返回 0）；`GameObject.SetComponent` 同步 Alive 自查或文档明示风险。

#### H2 BuildClipCache 弱解析器：坏 .anim 字段类型错 → 异常穿透 terminate
- **位置**：`Engine/Assets/PlayCaches.cpp:54`（:55/:63/:65 同）
- **类别/严重度/状态**：缺陷 · high · ✅ 复核成立（本次综合抽查再证）
- **问题**：自带弱解析器对 fps/loop/sheet/cell 用未做类型预检的 nlohmann 裸 getter（`get<float>`/`get<bool>`/`get<std::string>`/`get<uint32_t>`）；「合法 JSON 但字段类型错」的手写/外部工具档（如 `"loop": 1`、`"fps": "8"`）抛 type_error，调用链（GameEntry.cpp:720 / EditorContext.cpp:593）全裸无 try/catch，异常逃出 main = `std::terminate`——boot 直接崩、编辑器丢全部未保存工作，违反 `PlayCaches.h:6`「坏 clip 红字跳过不炸 Play」契约。
- **证据**：:49-50 只查 contains/is_array 不查类型，:54-65 四处裸 get；同函数 events 解析（:89/:95）反而做了 `is_number` 预检（同文件已知此坑）；同一 bug 已在 `AnimAsset::ParseClipJson` 修过（AnimAsset.cpp:30/40-44，注释注明 review 2026-10-02 #30 原文「"loop": 1 直接 get<bool>() 会抛 type_error … = std::terminate」），这份内联拷贝未同步。nlohmann type_error.302 行为经复核员亲读 vendored 源（json.hpp:1405-1413 等）确认。
- **建议**：删内联解析改调 `AnimAsset::ParseClipJson`（其 `ClipFrame{sheetGuid,cell}` 恰是所需；注意 loopMode→bool 映射与现版不读 loopMode 的差异），或对四字段补 `is_number/is_boolean/is_string` 预检。

#### H3 UI 容器缓存裸指针对 DOM 内突变不失效 = use-after-free
- **位置**：`Engine/Ui/UiSubsystem.cpp:986`（缓存结构 :216-224；失效调用点仅 :592/717/750/820，全在文档装载/重载/卸载路径）
- **类别/严重度/状态**：缺陷 · high · ✅ 复核成立（本次综合抽查再证）
- **问题**：`ContainerState` 缓存 container/tpl/proto/itemRoots 四组 `Rml::Element*` 裸指针，仅文档装载/重载/卸载时整体失效。C# 公开 API `SetInnerRml`/`SetText` 的 key 可命中任意元素 id（含容器自身或其祖先）；Rml 的 `SetInnerRML` 就地 RemoveChild 全部子元素并立即析构释放（RmlUi 6.3 `Element.cpp:1173-1175` → `DestroyAndDeallocate`）——后续 `SetItems` 的 `proto->Clone()`、ResolveKey 返回的 itemRoot 等均为 UAF。脚本作者对容器/包裹层 div 做 SetText 属合理用法，公开 API 一等入口直达 use-after-free。
- **证据**：UiSubsystem.cpp:216-224 裸指针 + 注释自述仅重载/卸载失效；:983-987 SetInnerRml op 经 ResolveKey（:372-377，无斜杠 key 即 GetElementById）命中后无任何容器失效处理；`GameUI.cs:158` 公开可达、key 语义即元素 id、与容器 id 同一命名空间。复核强化：key 命中容器自身 id 时 container 仍活、:402 检查照样放行，但 tpl/proto/itemRoots 已全灭，UAF 面比原发现更宽。
- **建议**：SetText/SetInnerRml 命中元素为任一追踪容器自身/祖先时同步 `InvalidateContainers`；或容器态改弱引用并使用前校验存活。

### 中危（23）

#### M1 持久离屏 RT 跨提交 WAR 竞态
- **位置**：`Engine/Renderer/RHI.cpp:1589`（EndPass 转 SHADER_READ :1643-1647；帧序 EditorApp.cpp:794-800；RT 跨帧复用 ViewportRenderer.cpp:415）
- **类别/严重度/状态**：缺陷 · medium · ✅ 复核成立
- **问题**：BeginOffscreenPass 对持久离屏 RT（编辑器 viewport/gameRT）用 UNDEFINED→COLOR_ATTACHMENT 获取屏障且 srcStage=TOP_OF_PIPE（first scope 空集）、loadOp=CLEAR 随即写回；而帧 N-1 尾部 ImGui 采样读同一 image 仍在 GPU 在途——两次 `vkQueueSubmit` 间只有 host 侧 fence、无任何 GPU 端依赖（submit 未设 waitSemaphore，全仓无 timeline semaphore）→ WAR 数据竞态，偶发视口残影/撕裂 + 规范级 UB。
- **证据**：RHI.cpp:1589-1594；`kFramesInFlight=2`（:39），BeginFrame 仅 `vkWaitForFences` 本槽 fence = 2 帧前（:1285-1289），EndFrameAndPresent 的 VkSubmitInfo 零初始化即无 wait（:1328-1333）；代码自身用 `kRingFrames=3` 环形 SSBO（SpriteBatcher.h:20）规避同类在途覆盖问题，却未对离屏 RT 做同等处理。运行时游戏路径不经过离屏 RT（BeginOffscreenPass 全仓唯一调用方是编辑器 ViewportRenderer），故 medium。
- **建议**：获取屏障 srcStage/Access 改为 FRAGMENT_SHADER+SHADER_READ（pipeline barrier 的执行依赖跨 submit 边界有效，可对先前提交的采样建立依赖），或离屏 RT 按 kFramesInFlight 份轮换。

#### M2 设备丢失重建后 RmlUi 几何句柄跨块释放 = 新块元数据污染
- **位置**：`Engine/Renderer/RmlUiBackend.cpp:573`（RecreateAfterLoss :469-497；触发链 UiSubsystem.cpp:649-652 → 1093-1096）
- **类别/严重度/状态**：缺陷 · medium · ✅ 复核成立（机制定性修正后收录）
- **问题**：设备丢失重建后 Rml 释放旧几何时，`ReleaseGeometry` 无条件把旧 `VmaVirtualAllocation` 句柄压入 pendingGeo，随后 DrainDeferred 在**新建的** vblock 上 `vmaVirtualFree` **外来**句柄——VMA 3.4.0 的 TLSF `Free` 第一行就地解引用句柄并把旧块节点并入新块自由链表、`--m_AllocCount` 错账，后续 `vmaVirtualAllocate` 遍历坏链 → 堆损坏/崩溃（UB）。
- **证据**：:573-577 无世代号/归属校验；重建回调按注册序同步执行（RHI.cpp:816-819），backend 先于 ui-subsystem，后者置 reloadDocsNextUpdate → 翌帧 ReloadAllDocuments → UnloadDoc → Rml 用旧句柄回调 ReleaseGeometry 入延迟环 → 新 vblock 上释放。复核修正定性：设备丢失路径**不**销毁旧 virtual block（`vmaDestroyVirtualBlock` 仅 Shutdown 路径且先清空 pendingGeo，安全），旧块被泄漏放弃而非销毁——实际机制是「跨块释放外来句柄」的结构性污染，而非必然 use-after-free 读；后果同向，severity 维持。
- **建议**：给 GeoAlloc 加世代号（RecreateAfterLoss 递增），ReleaseGeometry 见旧世代直接丢弃句柄只 delete。

#### M3 ImmediateSubmit 在 DEVICE_LOST 时 abort，绕过恢复体系
- **位置**：`Engine/Renderer/RHI.cpp:714`（:715 同；入口 UploadTexture :1027、InternalImmediateSubmit :1474 → RmlUi 纹理上传）
- **类别/严重度/状态**：缺陷 · medium · ✅ 复核成立
- **问题**：ImmediateSubmit（纹理上传共用路径）对 `vkQueueSubmit`/`vkQueueWaitIdle` 返回 VK_ERROR_DEVICE_LOST 经 VK_CHECK→LEMON_ASSERT 直接 abort 整进程，而帧循环同类错误在 :1333-1340 显式走 HandleDeviceLost 完整恢复（且有 SimulateDeviceLoss 验收钩子在用，设备丢失恢复是明确设计目标）——设备丢失瞬间若编辑器后台导入/热重导/缩略图懒加载正在上传即整进程 abort，丢用户全部未保存工作。
- **证据**：:714-715 无 DEVICE_LOST 分支；VK_CHECK（:52-59）非成功一律断言，LEMON_ASSERT 恒生效（Log.h:42-53，本次综合亲读再证）；编辑器上传活动在帧循环运行期真实存在（AssetGpuCache.cpp:83/89/132、ThumbCache.cpp:93、ViewportRenderer.cpp:282/335），全项目编辑器侧无一处 IsDeviceLost 前置防护。
- **建议**：ImmediateSubmit 对 VK_ERROR_DEVICE_LOST 特判：跳过断言（WaitIdle 容错）返回 false，由调用方或上层触发 HandleDeviceLost。

#### M4 每张纹理上传一次全设备同步
- **位置**：`Engine/Renderer/RHI.cpp:699`（UploadTexture :1005-1077；调用方 TextureStore.cpp:79-81、AssetGpuCache.cpp:130-132）
- **类别/严重度/状态**：优化 · medium · ✅ 复核成立
- **问题**：UploadTexture 每调用走一次 ImmediateSubmit（分配命令缓冲→提交→vkQueueWaitIdle→释放）；设备仅一个 VkQueue（RHI.cpp:128），每次 waitIdle 即整条 GPU 管线排空。dev 形态启动（无 .baked）、编辑器首次导入、设备丢失全量重导时装载期上百次串行全停（RHI.h:18-20 注释自证 svr-test 双怪 111 张帧图规模真实存在）。
- **证据**：:699-717；:1010-1077 每次建/销 staging；TextureStore/AssetGpuCache 逐资产循环逐张调用；RHI.h:175 唯一上传接口、无批量重载。
- **建议**：上传 API 增加批量重载（一次提交多张），或共享持久 staging 环 + timeline 信号量延迟回收。

#### M5 FxChannel 定长 memcpy 越界读源串 = UB（飘字高频路径）
- **位置**：`Engine/ECS/FxChannel.cpp:19`（`t.text` 为 char[16]，FxChannel.h:28）
- **类别/严重度/状态**：缺陷 · medium · ✅ 复核成立
- **问题**：PopupTextEx 对任意长度源串恒 memcpy 15 字节（不看 strlen），短于 15 字节的合法串（伤害数字 "5"/"12" 是主路径；C# marshal 串长度任意）越界读源对象尾部 = 标准级 UB，ASAN 首跑即报；非 ASAN 构建下通常静默（垃圾字节被 NUL 截断）。
- **证据**：:19 与对照写法 World.cpp:26（RtUi 用 `snprintf` 安全写法）同功能不同命；调用点 GameEntry.cpp:1022/1068/1084（"stale"）、ScriptHost.cpp:299/312（C# 透传主路径）、tests 多处短串（含 `char buf[8]` 栈缓冲从 8 字节对象读 15 字节）实锤。复核补充：mac-san 门禁构建配置时间（2026-09-20）早于该文件最近修改（2026-10-08），最新代码未经 ASAN 复跑。
- **建议**：改 `std::snprintf(t.text, sizeof t.text, "%s", text)`（RtUiChannel 同手法），或按 `strlen(text)` 上限拷贝。

#### M6 投射物命中无扫掠/CCD：高速弹穿透
- **位置**：`Engine/Systems/Systems.cpp:874`（推进 :742 单步；speed 域 ComponentCatalog.cpp:197）
- **类别/严重度/状态**：缺陷 · medium · ✅ 复核成立
- **问题**：命中是离散点采样（每 tick 仅查弹心当前圆域），固定步长 1/60（EditorApp.cpp:620）下步长超过 2×hitRadius（默认 12px → speed>1440px/s）即出现漏检带；编辑器允许 speed 至 8192 → 步长 136px、漏检带最宽约 112px，目标被高速弹穿透。TriggerSystem probe=4 同族（阈值 speed>4320，较次要）。休眠的 Raycast（SpatialHash.h:70-76 自认零调用方、有测试覆盖）未接入命中路径。
- **证据**：:874-876 OverlapCircle 只判当前弹心；MovementSystem :742 一 tick 一次 `tf->pos += v*dt` 无子步；系统注册序 Movement→SpatialHashRebuild→Hitbox；全库 grep 无 sweep/CCD/substep。模板资产实测 speed=320、hitRadius=12（步长 5.3px）出厂内容不触发——属参数空间内的潜伏正确性缺陷，编辑器明示允许且无 speed/hitRadius 联动约束。
- **建议**：对 `speed*dt > 2*hitRadius` 的弹做扫掠判定：记录上一 tick 位置，segment-圆查询（或激活 Raycast 走线语义）替代/补充点查询；触发器按相对位移连扫。

#### M7 TargetBoard::DeclareTeams 每帧 clear+重建，堆容量全丢
- **位置**：`Engine/Systems/Systems.cpp:28`（AISystem 每 tick 调 :520；复用意图注释 Systems.h:80）
- **类别/严重度/状态**：优化 · medium · ✅ 复核成立
- **问题**：每帧 `teams_.clear()` 析构 TeamList 元素（内嵌 list + Grid 的 keys/offs/items/occ/scratch_ 五 vector 容量全释放），下一帧 Rebuild 从容量 0 重新翻倍扩容——与「稳态零分配/复用缓冲」注释意图相悖；万怪场每队每帧约 4 个翻倍数组 × log2(队规模) 次 malloc/free（复核精确化：list<64 早退，grid 数组仅 ≥64 目标的大队分配）。
- **证据**：:28-31 全量重建；Systems.h:53-87 内嵌容器结构；Systems.h:80 注释「Build 暂存（复用免逐帧分配）」因每帧析构落空；AISystem 注册默认管线每 tick 无条件调用（无「未变则跳过」短路）。
- **建议**：DeclareTeams 差量同步：teamIds 与现有声明一致时只 `list.clear()` 不重建 TeamList 对象（容量即稳定复用），不一致才增删对应槽。

#### M8 TargetBoard::NearestAny 全表线性扫描，Flee 多时撞复杂度墙
- **位置**：`Engine/Systems/Systems.cpp:269`（Flee 段每实体每帧 :563；教训注 Systems.h:56-62）
- **类别/严重度/状态**：优化 · medium · ✅ 复核成立
- **问题**：NearestAny 对 all_ 线性扫描、无网格加速（range 只作 bestD2 初值不影响扫描量），Flee 段又自身串行 view 循环（对比 Chase/Shooter 已 ParallelFor）——万级 Flee 场（敌群溃逃，目标品类常态）回到自家教训注释记录的复杂度墙（「13500 查询者 × 6500 候选 ≈ 25ms」）。复核补充：Systems.h:139 头注声称 AI 系统并行，Flee 段实际串行，头注与实现不一致。
- **证据**：:269-281；Rebuild 只对 teams_ 建 Grid（:49/73/106-114），all_ 仅收集追加；team 版 Nearest ≥64 走 grid 环搜（:251/266，环搜实现 :203-238）。
- **建议**：all_ 复用同款 Grid 桶（Grid::Build 接受任意 vector，已就绪），或 Flee 改走 SpatialHash OverlapCircle 圆域查询（注意管线序：SpatialHashRebuild 在 AI 之后会引入一帧位置陈旧，第一方案更干净）。

#### M9 lemon_scene_event 主线程内联执行，违反自家域线程纪律
- **位置**：`Engine/Scripting/dotnet/Lemon.Entry/Exports.cs:143`（对照 :286-293 lemon_ui_events_dispatch）
- **类别/严重度/状态**：缺陷 · medium · ✅ 复核成立（泄漏归因修正后收录）
- **问题**：sceneLoaded/sceneUnloaded/activeSceneChanged 及 FireCompleted 的用户 ALC 委托与 await 续段在 UCO 调用线程（模拟主线程）内联执行，而 UnloadScript/ReloadScript 的 `alc.Unload()` 恰在同一线程——破坏 #14 修复时立的「卸载线程从未触碰用户 ALC」纪律（ADR-010 D1）；且 SceneManager.cs:56/94-97 自称「域线程同步续跑」与实现矛盾。两条导出口径不对称确凿（ui_events 已走池化投递且注明同类教训）。
- **证据**：Exports.cs:143-150 无 DomainManager 投递；调用链 SceneSwitcher.cpp:342/381/384 → ScriptHost.cpp:1243-1252 同步直调，运行于引擎/编辑器主循环线程；热重载 Unload 就地执行于同线程（EditorAppScriptReload.cpp:177 → DomainManager.cs:214）。复核修正归因：「此后每次换装确定性泄漏（MB 级/域）」不成立——域线程执行模型的 pin 自 M4.5 起即确定性常态（tests/script/main.cpp:288-294 固化断言），scene_event 未引入或加重当前泄漏，量级约百 KB/域非 MB；真实危害 = 违反 D1 纪律 + B 线「泄漏计数自然归零」恢复前提存疑（ADR-010:150-155）+ 注释/ADR 与实现矛盾。
- **建议**：lemon_scene_event 改走 DomainManager.PostUiEvents 同款池化域线程投递（PostBatch 族），与 lemon_ui_events_dispatch 对齐；同步修正注释/ADR 措辞。

#### M10 Events.DispatchPackets 迭代活订阅表 + 异常被静默吞
- **位置**：`Engine/Scripting/dotnet/Lemon.SDK/Events.cs:76`（吞点 DomainManager.cs:280、300-307）
- **类别/严重度/状态**：缺陷 · medium · ✅ 复核成立
- **问题**：直接 foreach 迭代订阅表，handler 内 Subscribe/Unsubscribe（公开 API，LemonBehaviour.Subscribe 实例助手鼓励使用）修改同类型 List 后下一次 MoveNext 抛 InvalidOperationException——该异常不在 per-handler try 内（try 只包 handler 调用体），逃逸到 EvtBody 后被 RunPooled 静默吞掉（无任何日志）且当批剩余事件全部丢弃。
- **证据**：Events.cs:73-81；.NET List 版本检查是文档化行为（与线程无关，域线程单线程也逃不掉）；对照 GameUI.cs:368-372 UI.DispatchEvents 先 ToArray 快照（其注释自称同纪律却多一层），不对称证明是遗漏；PostUiEvents 有 Error 红字而 PostBatchEvents/PostBatchTick 不读 Error；C++ 侧 ScriptHost.cpp:1221-1241 整批转发后无重试，异常中断即本批剩余 packet 永久不派发。
- **建议**：迭代前快照（或 for 索引 + 容忍尾部增删，兼顾 Events.cs:4「无每帧分配」GC 纪律）；PostBatchEvents/PostBatchTick 补 Error 检查红字。

#### M11 lemon_scripts_attach/destroy/detach 逐实体 PostBatch 往返
- **位置**：`Engine/Scripting/dotnet/Lemon.Entry/Exports.cs:209`（DomainManager.cs:247-255）
- **类别/严重度/状态**：优化 · medium · ✅ 复核成立
- **问题**：三导出每条命令一次 PostBatch：`new Command`（含 ManualResetEventSlim）+ 闭包 + BlockingCollection 入队 + 一次同步跨线程往返（cmd.Done.Wait()）。批量销毁（爆炸/清屏，目标品类常态）N 个带脚本实体 = N 次分配 + N 次上下文切换往返；GC 纪律只池化了 tick/events/UI 三通道。
- **证据**：C++ 侧确认逐实体调用：NotifyPendingDestroys 对每个待销毁实体逐一 scriptsDestroyFn_（ScriptHost.cpp:1203-1206，注释明示覆盖战斗击杀/投射物到期/越界回收），调用发生在 C++ 管线线程（s_onDomainThread 仅域线程置位）必走全程投递分支。量级为机制推断（N 次分配 + 每次 ≥2 次上下文切换），未实测。
- **建议**：改命令参数批量打包后单次域线程投递（同 PostBatchTick 池化模式），不破坏 #52 迭代器不变量。

#### M12 Behaviours.Detach 每销毁实体全类型槽×全实例线性扫描
- **位置**：`Engine/Scripting/dotnet/Lemon.SDK/Behaviours.cs:191`
- **类别/严重度/状态**：优化 · medium · ✅ 复核成立
- **问题**：Detach 外层遍历全部类型槽、内层遍历每槽全部实例比较 Entity.Id，无实体→实例索引；批量销毁 N 实体 = O(N×总实例数)，与 M11 的逐实体跨线程往返叠加构成清屏场景双重放大（引擎侧 ScriptBox 每实体有定长槽表，托管侧无对应索引）。
- **证据**：:191-204；Behaviours 唯一字典 s_hotBags 是热重载状态包与销毁查找无关；Exports.cs 只有单实体版 lemon_scripts_destroy 无批量路径。行为本身正确（恰好一次由 ScriptBox.notified 位保证），纯性能问题。
- **建议**：Detach 增加实体→(slot,index) 索引，或 C++ 侧先收集本帧全部销毁实体一次性批量投递（与 M11 合并修）。

#### M13 manifest sliceCount 与 .meta 网格不对账 → release abort
- **位置**：`Engine/Assets/AssetIndex.cpp:274`（消费链 TextureStore.cpp:44-48、Atlas.cpp:108；同缺口影响 AtlasStore.cpp:49）
- **类别/严重度/状态**：缺陷 · medium · ✅ 复核成立
- **问题**：manifest 的 sliceCount 与 .meta 的 gridCols×gridRows 从不对账：陈旧 manifest（.bak 恢复路径 / manifest.pkg.json 快照 + 后续改大的 .meta，掉电半写可达）配新 .meta 帧数时，RegisterGridSlices 会以 spriteId 0 调 SetSpriteAt → `LEMON_ASSERT(spriteId != 0, "spriteId 0 reserved")`（Atlas.cpp:108）release 恒生效 abort，硬崩装载；违背自家「坏账清零、宁缺勿错」口径（同族防线 :286/309 已补，此为漏网）。
- **证据**：:274-279 只读几何不动数量；:310-314 三道防线只管号域不对账数量；SliceSpriteId（AssetIndex.h:63-65）cell≥sliceCount 返回 0；日常编辑器 Rescan 恒对齐不触发；像素校验（TextureStore.cpp:37-43）挡不住 frames 变大同时 cell 变小的组合。
- **建议**：LoadFromManifest 装载期对账：sprite 条目 sliceCount != gridCols*gridRows 即按 .meta 重派（或清零转全幅）。

#### M14 LoadBakedFont 头字段无域检 + 尺寸乘法回绕
- **位置**：`Engine/Assets/FontBake.cpp:298`
- **类别/严重度/状态**：缺陷 · medium · ✅ 复核成立
- **问题**：pageW/pageH 零 sanity 校验且 `(size_t)pageW*pageH*4` 可 2^64 回绕：pageW=pageH=0x80000000 时积 ≡ 0，仅含头+字形表的小文件即通过字节对账，随后 BitmapFont 以 2^31 量级 CreateTexture（BitmapFont.cpp:159-160）→ VK_CHECK abort/驱动 UB。精心构造/损坏的 LBF1（项目 `.lemon/baked/fonts/` 即第三方输入面，GameEntry.cpp:574-580 装载）可触发。
- **证据**：:296-302 仅 magic/version + 尺寸对账；对照 LAT1 读取器有 kAtlasMaxImageDim 域检 + 回绕防线注释（AtlasBake.cpp:352-358，注明「LBA1 review 2026-10-01 先例」）——引擎自身规约即防此型，LBF1 缺失；RHI CreateTexture 无尺寸域检，UploadTexture 的断言乘积同样回绕拦不住。
- **建议**：增加 pageW/pageH 非零且 ≤ kPageHMax（或 GPU 尺寸域）显式域检，仿 LAT1 读取侧口径。

#### M15 ReadFontImporter 的 std::stoull 抛异常 → boot/packager 终止
- **位置**：`Engine/Assets/AssetIndex.cpp:156`
- **类别/严重度/状态**：缺陷 · medium · ✅ 复核成立
- **问题**：outline 颜色串用 `std::stoull` 解析：非数字/超域字符串抛 invalid_argument/out_of_range，从 AssetIndex::Open 全链无捕获 → boot（GameEntry.cpp:553）与 packager（tools/packager/main.cpp:426）进程终止。`.meta` 为用户可手编 JSON，`"outline":[2,"zz"]` 即触发；违反本文件「坏段=默认」宽容契约——同文件其余解析（ReadGridImporter/ReadAudioImporter/ReadMetaGuid）全走非抛 nlohmann 判型，唯独此处引入抛异常路径。
- **证据**：:155-156；复核员实跑独立 C++ 片段验证：`"zz"` 抛 invalid_argument、17 位 F 串抛 out_of_range、`"12zz"` 部分前缀不抛；两调用文件精确 grep try/catch 零命中；触发仅限项目含 .ttf/.otf 时。
- **建议**：改 HexToGuid 同款手写 hex 解析（AssetTypes.cpp:57-72 已有非抛先例），或先逐字符判 hex 再转换。

#### M16 AssetIndex 全部 GUID/spriteId 查询 O(N) 线性扫
- **位置**：`Engine/Assets/AssetIndex.h:108`（实现在 AssetIndex.cpp:454-492）
- **类别/严重度/状态**：优化 · medium · ✅ 复核成立
- **问题**：只建 byPath_ 哈希；FindByGuid/FindByWholeSpriteId 被每实体（ResolveSpriteRefs——装载与每次换场）、每帧引用（BuildClipCache）、每图集条目（AtlasStore，O(N²)）调用，数千资产 × 万级实体 = 千万次 u64 比较级的一次性装载卡顿（数十 ms）。
- **证据**：:454-458/:466/:478/:485 四处全扫；消费方 GameEntry.cpp:375-383 适配器直通线性版；SpriteRefs.cpp:9-29 每带 SpriteRenderer 实体调一次。复核补充：FindBySpriteId/FindByLowId 当前无运行时热消费方（仅测试），guid 一张表即覆盖全部已核实热点。
- **建议**：Open 时建 guid→下标 unordered_map（优先）；spriteId 表需注意切片区间语义可后置。

#### M17 spawn 热路径每发全量 JSON 重解析
- **位置**：`Engine/Assets/PrefabCache.cpp:68`（→ SceneArchive.cpp:357-358）
- **类别/严重度/状态**：优化 · medium · ✅ 复核成立
- **问题**：InstantiateJson→LoadEntityTree 每次调用 `Json::parse` 全量重解析 prefab 文本；Build 期已持有文本快照却不缓存解析产物。目标品类高频刷怪/弹幕（ShooterSystem/WaveDirector/SpawnSystem 都走此径）下每发 spawn 一轮堆分配密集的解析。
- **证据**：:59-68；PrefabCache.h:68-71 Entry 仅存 {guid, json 文本}；:67 注释自述「高频 spawn 工厂与交互路径共用（M5 清障②）」——IO/日志已清、唯独解析留在热路径。复核提示：预解析 nlohmann::json 入 Entry 与 SceneArchive.h:69「json 类型不出头文件」红线冲突，需 pimpl 封装或走二进制组件快照方案。
- **建议**：Build 期预解析（pimpl 封装）或二进制组件快照存入 Entry，spawn 只走实例化半边。

#### M18 StreamFeed 析构的 fclose 可在主线程持音频锁时执行
- **位置**：`Engine/Audio/AudioEngine.cpp:861`（同构 :329、:715-718；~StreamFeed :89-91）
- **类别/严重度/状态**：缺陷 · medium · ✅ 复核成立
- **问题**：Tick 锁内 `v = {}` 释放 stream shared_ptr；一次性流式播完时填充线程早已 erase job（环内余量约 1.37s 保证时序），Tick 的释放即末引用 → 阻塞 `fclose` 持 impl_->mtx 执行，而 DataCallback 等同一把 mtx——设备回调被磁盘 IO 间接阻塞。这是 review #4 修掉 fread 链时遗漏的对偶面；本地盘常态微秒级，网络盘/IO 高压可逼近 ~10ms 回调期限，且是每次一次性流式播放结束的必经路径。
- **证据**：:855-861、:89-91、:519-524；复核枚举 FeedRef 全部持有者确认末引用时序；PlayLocked 槽复用（:329）与 Play 拒绝路径（:715-718 锁内析构已 open 的 feed）同构。
- **建议**：锁内将 FeedRef swap 到局部变量、锁外释放；或约定末引用析构固定发生在填充线程（voice 侧只置 dead 不持末引用）。

#### M19 流式声部回调内逐帧 2-4 字节 SPSC Read
- **位置**：`Engine/Audio/AudioEngine.cpp:408-422`（SpscRing.h:41-54）
- **类别/严重度/状态**：优化 · medium · ✅ 复核成立
- **问题**：MixVoices 帧循环内每帧一次 ring.Read（acquire load + memcpy + release store），48kHz 每流式声部每秒 4.8 万次原子发布；而环本支持字节批量、生产者侧 PumpFeed 已按帧块批量 Write——消费者逐帧确属不对称。绝对开销量级小（单声部合计 <0.1% 单核），属可接受的工程优化项。
- **证据**：:409-411 单声道 2 字节/立体声 4 字节；SpscRing.h:42/52 原子操作；:45 按需收敛支持批量；流式声部恒 rate==1.0（:334）保证块级 Read 与现行为完全等价。
- **建议**：每声部每回调块一次 Read 取 min(frames, avail) 入栈缓冲（实读不足静音补齐），原子流量从每帧降到每块。

#### M20 UI Init 失败后 Shutdown 调未初始化的 Rml::Shutdown → 崩溃
- **位置**：`Engine/Ui/UiSubsystem.cpp:614-616`（Shutdown :656-664；析构防御 :599-607）
- **类别/严重度/状态**：缺陷 · medium · ✅ 复核成立
- **问题**：backend Init 失败早退后 impl_ 保留，后续显式或析构防御 Shutdown 调从未 Initialise 的 `Rml::Shutdown`——debug 断言中止、release 断言编译掉后 `core_data->contexts.clear()` 即空指针 UB。触发路径真实且与设计契约冲突：GameEntry「UI 缺席降级运行」（Init 失败仅 WARN 继续跑，函数尾/析构必调 Shutdown）；编辑器 else 分支 `gameUi_.reset()` → 析构防御 Shutdown 同炸。Rml::Initialise 自身失败同款。
- **证据**：:610-619、:657-662 无 Rml 已初始化守卫；RmlUi 6.3 Core.cpp:64/157/162-164（RMLUI_DEBUG 受 NDEBUG 控制编译为空）；正常路径仅 RT 格式不支持时 Init 失败，罕见但一旦触发即从「降级运行」变崩溃。
- **建议**：Init 失败分支就地 `impl_.reset()`，或记 rmlInitialised 标志让 Shutdown 按序守卫。

#### M21 EntityRef 前缀解析不查 endptr，坏档静默指向错误实体
- **位置**：`Engine/Serialization/SceneArchive.cpp:49-52`
- **类别/严重度/状态**：缺陷 · medium · ✅ 复核成立
- **问题**：`"eabc"`→索引 0、`"e5x"`/`"e 5"`→5：strtoull 前缀解析不要求消费到串尾，坏档静默指向错误实体（编号越界有防、格式垃圾无防），违背同处注释「格式非法返回 false + 红字」契约；「用户可编辑文本档」是支持场景（:195 注明）。
- **证据**：:49-51 endptr 传 nullptr，仅靠前缀 'e' 与越界检查；受影响字段 = Hierarchy 父子链与 Chase/Shooter/Collectible.target（ComponentCatalog.cpp:46-47/170/178/220）；全引擎唯一 strtoull 调用点。复核按 C 标准（C11 7.22.1.4）语义推导三例输入行为，未实跑。
- **建议**：strtoull 配 endptr 要求消费到串尾，拒绝前导空白与负号。

#### M22 lemon-game 失焦卡键：清键修复落在不读的路径上
- **位置**：`Lemon/Engine/Entry/GameEntry.cpp:170-198`（对照 Window.cpp:63-68；输入消费 :868-877）
- **类别/严重度/状态**：缺陷 · medium · ✅ 复核成立
- **问题**：InputCollector 无 FOCUS_LOST/MINIMIZED 清键处理（事件经 observer 送达但 switch 无分支被丢弃），而 gameplay 输入直读这份 keyDown 表——Window.cpp 专为该 bug 写的失焦清键修复落在 m->keys（IsKeyDown），lemon-game 全程不调用该接口（grep 证实零调用）。失焦瞬间按住移动键 → KEY_UP 丢失 → 角色持续单向移动（项目注释自证的实测行为）。
- **证据**：:170-198 无对应分支；:868-877 直读 keyDown 后 :879 ApplyInput；Window.cpp:63-68 修复注释原文即「不清 = 卡键（Play 态角色持续单向移动）」。复核修正：IsKeyDown 另有 6 处 Samples 调用（受保护）；编辑器 Play 态移动实际走 ImGui::IsKeyDown——修复确实未落在任何读移动键的路径上。
- **建议**：InputCollector 增加 FOCUS_LOST/MINIMIZED 分支清空 keyDown（与 Window.cpp 同款），或 lemon-game 输入改走 Window::IsKeyDown。

#### M23 C++/C# 边界 vtable 膨胀为 59 槽 god-interface
- **位置**：`Engine/Scripting/ScriptHost.h:53-197`（镜像 NativeApi.cs:16-80；通道面 :353-366）
- **类别/严重度/状态**：设计 · medium · ✅ 复核成立
- **问题**：边界已从设计文档自述的「批量 API + 事件队列」两通道（04-CSharp-Scripting.md:5）扩张为 6 类通道（批量帧、事件队列、SceneOps、UI ops/UiEvent、换场事件直推、59 槽 vtable），vtable 横跨 13+ 域无分域；每加一个 C# 特性就要两侧手工同步一份逐字节镜像签名（8 个月 36→59 槽全表尾追加），同步成本随槽位线性增长。
- **证据**：三处独立计数一致 = 59 槽（ScriptHost.h:56-197 声明 / ScriptHost.cpp:603-662 初始化 / NativeApi.cs:16-80 镜像）；:164 sceneLoadRequest 自注「首个结构性 vtable 通道」经 vtable 直调 Switcher().Request 不进 SceneOps。复核精确化：SceneOpC 为 16B 定长本就放不下场景名/mode 载荷，准确说是「新结构性命令类别未收敛进命令缓冲族」，时序语义未破坏（同样下一帧 Essential 执行）。
- **建议**：结构性/查询族通道收敛回命令缓冲与批量查询面，或按域拆分 vtable 子表；新增能力优先走既有 ops 队列而不是表尾追加新指针。

### 低危（39）

#### L1 SortingLayer 内负 order 符号反转
- **位置**：`Engine/Renderer/Renderable.cpp:180`（int16_t 源 Renderable.h:74）
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：order=-1 经 `(uint16_t)` 强转变 65535 → 排到 order=0 之后绘制（显示在最上层），与 Unity「负 order 在下层」语义相反（Unity 心智模型是本项目明文基准）；仅影响使用负值的场景，默认 0 不受影响。
- **证据**：:180 打包入 sortKey 第 24-39 位；:249-252 升序 stable_sort + 画家序消费（越晚画越在上层）；编辑器 Inspector 的 Int16 控件是无限幅 DragInt（InspectorPanel.cpp:620-625，对比 UInt16 有 0-65535 限幅）；场景反序列化与 C# API（LayoutTables.cs:107）均原样读入无钳制；仓库示例只用正值。
- **建议**：打包前做 `(uint32_t)(order + 32768)` 偏移（-32768→0、32767→65535），或文档明确 order 仅支持非负并钳制。

#### L2 RHI 映射内存写路径从不 vmaFlushAllocation
- **位置**：`Engine/Renderer/RHI.cpp:1022`（hostMapped 家族 :940-946；对照 RmlUiBackend.cpp:379）
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：纹理 staging memcpy 后无 flush 即提交；hostMapped 常驻指针的全部调用方（SpriteBatcher 每帧写等）同样无 flush。复核读 vendored VMA 3.4.0 证实：AUTO_PREFER_HOST + SEQUENTIAL_WRITE 的 HOST_COHERENT 既非 required 也非 preferred，非一致 HOST_VISIBLE 类型可被选中——若选中则 GPU 读陈旧数据。RHI.h:66 注释自称 HOST_VISIBLE|COHERENT 与实现不符，佐证系疏漏。
- **证据**：grep Engine/ 仅 RmlUiBackend.cpp:379/:718 有 flush（注释明记 dGPU 非一致不 flush 则 GPU 不可见，spike-04 实测黑屏根因）；现配置（AUTO_PREFER_HOST 使 DEVICE_LOCAL 进 notPreferred）触发面窄、现网罕见（主流平台 HOST_VISIBLE 全 coherent）。
- **建议**：MapBuffer 写路径统一补 vmaFlushAllocation（coherent 上 no-op 零成本），修正 RHI.h:66 注释。

#### L3 DrawTextEx/TextWidthEx 每调用堆分配
- **位置**：`Engine/Renderer/BitmapFont.cpp:201`（:269-270 同）
- **类别/严重度/状态**：优化 · low · ✅ 复核成立
- **问题**：每次调用新建 `std::vector<uint32_t>` + DecodeUtf8 必然堆分配（vector 无 SSO）；飘字密集场景逐帧逐字符串分配（GameFx.cpp:91 每条可见飘字每帧调用；FxChannel 16 字节定长文本多落 SSO，主要分配是 cps vector）。
- **证据**：:201-202；FontBake.cpp:23-25 clear+reserve；无烘焙页早退不分配，但生产路径走 LoadBaked（BitmapFont.h:81 注明）。
- **建议**：复用成员/线程局部 scratch 缓冲（mutable vector），或流式逐码点解码免中间数组。

#### L4 粒子桶内绘制序依赖池序而 swap-and-pop 破坏池序
- **位置**：`Engine/Renderer/Particles.cpp:103`（回收 :86）
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：注释「桶内池序即稳定序」与实现不符：swap-and-pop 回收把池尾存活粒子搬到死亡槽位，同桶 Alpha 混合粒子帧间叠加序不稳定；对照 Renderable.cpp:247-252 为同问题显式 stable_sort（注释 #45：非稳定序致 alpha 混合闪跳）。复核强化：粒子 sortKey 低 40 位装池下标但全仓无任何消费者（sortKey 仅 Renderable.cpp:251 被比较），粒子路径连兜底排序都没有。
- **证据**：:86-87、:117（按池下标生成 packets）、:175 注释自证桶序即绘制序（SpriteBatcher bakeSpan 顺序录制无重排）；Renderable 路径成立是因 free-list 回收不搬移存活实体，该前提在粒子侧被破坏。
- **建议**：桶内改稳定依据（如发射序号），或修正注释并把 Alpha 混合粒子纳入稳定排序。

#### L5 ComputeStateHash 每实体每字段重复 strlen+哈希字段名
- **位置**：`Engine/ECS/StateHash.cpp:55`
- **类别/严重度/状态**：优化 · low · ✅ 复核成立
- **问题**：字段名是组件级常量却在每实体循环内 strlen+逐字节哈希：kProjectile 名字节 103B/实体 vs 数据 44B/实体，万实体回放校验下哈希字节数超数据 2 倍余且 strlen 无法跨调用缓存。
- **证据**：:52-55；经 forEachFn 绑定 ForEachComponent（ComponentCatalog.cpp:387-394）每实体回调；FieldMeta.name 指向字面量（ComponentRegistry.h:35-40）。场景为 bench 录制回放与 smoke 断言，非正常游玩热路径。
- **建议**：组件级对字段名表 (name,type,offset) 预哈希一次（保留 schema 漂移敏感性），实体循环只哈希数据字节；注意会改变哈希流布局致既有回放文件不兼容（内部格式变更）。

#### L6 forEachRangeFn 对 begin 越界未防御（休眠缺陷）
- **位置**：`Engine/Components/ComponentCatalog.cpp:377-379`
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：只钳 end 不钳 begin，`std::advance(it, begin)` 在 begin > view.size() 时越过 end() = UB。当前 forEachRangeFn 零调用点（并行切段钩子已备未启用，ScriptHost.cpp:926 注释「10 万级再启用」），是不可触发的休眠缺陷；docs/Reports/2026-09-23-engine-review.md:151 已记录同一问题至今未修。
- **建议**：`if (begin >= view.size()) return;` 与 end 钳制对齐，或加 LEMON_ASSERT。

#### L7 Scene::Destroy「线程安全」契约名不符实
- **位置**：`Engine/ECS/Scene.cpp:34-37`（契约注释 Scene.h:35-36）
- **类别/严重度/状态**：设计 · low · ✅ 复核成立
- **问题**：锁只保护 destroyQueue_，锁内 `registry_.emplace<DestroyQueueTag>` 是 registry 结构写，与并行迭代的含 tag 查询（ScriptHost.cpp:1203、SceneArchive.cpp:319/337/342、Scene.h:105 无锁读）仍是数据竞争；且注释点名的 worker 直调方（ProjectileLifetime）已改走主线程收集提交（Systems.cpp:1216-1227），.h 注释与实况相悖，Scene.cpp:27-31 实现注释还自认 worker 直调会破坏状态哈希提交序。全库核查当前无任何 worker 段直调。
- **建议**：头文件契约改为「仅主线程调用；并行销毁走收集意图→主线程归并（03 §4 契约 3）」，或把 tag 打标移入主线程提交段。

#### L8 DDOL 幸存判定逐实体上行祖先链
- **位置**：`Engine/ECS/SceneMembership.cpp:45-56`
- **类别/严重度/状态**：优化 · low · ✅ 复核成立
- **问题**：QueueDestroyAllExceptDdolLineage 对每个非 DDOL 根实体逐个上行祖先链判幸存（每层 2 次 TryGet、深度护栏 8）≈ O(N×9-20) 次稀疏集池查找，五万实体换场清场数十万~百万次查找的常数级尖峰（毫秒级为合理量级估算，未实测）。
- **证据**：:25-35 链检查、:45-56 全场遍历；QueueDestroySceneGroup（:58-68）与 CollectDontDestroyOnLoadLineage（:124-130）同款模式；CollectTree（:73-84）已有同款 DFS 原语可直接复用。
- **建议**：先收集 DDOL 根沿 firstChild 链 DFS 标记幸存集合（O(N+边)），清场查集合代替上行链；同族两函数一并考虑。

#### L9 IntegrateStaged 坏条目直调 CommitDestroys 未注明红线豁免
- **位置**：`Engine/ECS/SceneSwitcher.cpp:265`（红线 SceneMembership.h:64-67 + SceneSwitcher.h:7-8 双处自declare）
- **类别/严重度/状态**：设计 · low · ✅ 复核成立
- **问题**：绕过自家「直接调 CommitDestroys = 静默漏 OnDestroy（F1）」红线；当前坏槽是 `live.Create()` 出厂的零组件空实体、无 ScriptBox 可通知，直调与先 notify 后 commit 行为等价、无实害——但例外未注明豁免理由。三处直调（SceneArchive.cpp:464/608、SceneSwitcher.cpp:265）仅 staging 侧 608 带豁免注释；既有直调模式先于红线存在，红线确立后未回头补注。若日后有人在可入队销毁的回调上下文模仿此直调即成实害。
- **建议**：调用点补注「坏槽=零组件空实体，无 OnDestroy 可漏」豁免理由，加注释/断言锚住坏槽不得携带组件。

#### L10 场景档案记录只增不减
- **位置**：`Engine/ECS/SceneSwitcher.cpp:348` / `Engine/ECS/World.cpp:81-98`
- **类别/严重度/状态**：优化 · low · ✅ 复核成立
- **问题**：每次换场（含同名重复装载、BuildInto 失败路径）都追加 SceneRecord，旧记录置 isLoaded=false 永不回收，FindSceneRecord 线性扫随换场次数增长——死亡重开循环与自动化测试下无界缓慢增长（每条 ~100B 量级，1 万次重开 ≈ 1MB、每次换场 3-4 次线性查找为微秒级）。
- **证据**：World.cpp:86 push_back 单调发号，全仓无 erase/clear。复核补充：「只增不删、句柄不复用」是文档化契约（World.h:138-144，回放确定性「同序装载即同号」，tests/engine/SceneTests.cpp:509、tests/script/main.cpp:791/851 断言钉死）——按 path 复用句柄会触碰该契约，哈希索引半条建议安全可行。
- **建议**：给 sceneRecords_ 加句柄→下标哈希索引；复用句柄方案需先评估发号语义与回放兼容。

#### L11 SpatialHash::Rebuild 对 NaN/inf 无防御（float→int UB）
- **位置**：`Engine/Physics2D/SpatialHash.cpp:24`
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：Transform.pos 非有限时 `(int32_t)std::floor` 转换按标准是 UB；主流平台饱和转换后坏实体落垃圾 cell、静默永不命中。同类坏坐标已在 TargetBoard::Grid::Build 修复（review #19，Systems.cpp:144-148 注释明记「原实现对 NaN 的 float→int 是 UB，脚本写 Transform、手改场景档可达」）但此处漏修；Movement 的 clamp 对 NaN 两比较均 false 原样返回洗不掉；查询侧（:88-91/120-123/170-171）同款转换亦未检查。
- **证据**：:24-25 无 isfinite；C# Transform2D.Pos 公开可写字段（Components.cs:29）。
- **建议**：Rebuild 收集循环加 isfinite 剪除（与 #19 同款），坏坐标实体跳过收录；查询侧一并考虑。

#### L12 Director/Spawn 每 tick 局部 vector 堆分配
- **位置**：`Engine/Systems/Systems.cpp:337-338`（SpawnSystem :457-458 同构）
- **类别/严重度/状态**：优化 · low · ✅ 复核成立（组件与系统 + 架构分层两位评审员独立发现，合并收录）
- **问题**：每 FixedTick 一次 malloc/free（局部 `std::vector<DeferredSpawn>` + reserve(16)）；timeScale=0 冻结期与「工厂已注册但场景无导演/刷怪器」的空转期照样分配；违反「稳态零分配」口径（Systems.h:89 注释），同文件 chunkIntents_/dispatchBuf_/TargetBoard 缓冲均已成员化复用，此两处漏网。
- **证据**：两系统类声明（Systems.h:109-137）无 deferred 成员；调度经 SystemPipeline::RunStage 每模拟 tick 必调（SystemPipeline.cpp:73-76）；复用先例 Systems.h:237-240/307、Systems.cpp:1214-1215。
- **建议**：挪为系统成员 clear() 复用（DeferredSpawn 随之上移头文件），与 chunkIntents_ 同法；无确定性/回放风险。

#### L13 NotifyPendingDestroys 的迭代安全前提陈述与代码不符
- **位置**：`Engine/Scripting/ScriptHost.cpp:1197`
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：注释「当前所有导出（spawn/attach/destroy）都只投递命令不就地改池」为假：OnDestroy 回调内可经 vtable 就地 Instantiate.Spawn（Create+四次 Emplace，:146-158，其自身注释 :141-144 明写「就地建实体」）与 DontDestroyOnLoad（就地 Emplace<SceneMembership>，SceneMembership.cpp:40）；当前碰巧安全仅因这些池与 View<DestroyQueueTag, ScriptBox> 迭代的两池不相交且 EnTT 池独立——前提一旦被新 vtable 导出打破即迭代器失效 UB。复核强化：DestroyQueueTag 已在脚本侧注册（ComponentCatalog.cpp:464），OnDestroy 内 SetComponent<DestroyQueueTag> 可就地向被迭代池追加，仅因 entt 3.15.0 迭代器实现细节（range-for end 一次性捕获、纯追加不悬垂）才无现行 UB——「安全靠碰巧」。
- **建议**：改为先收集待通知实体到局部数组、循环结束后统一回调（注释 :1198-1199 自带该逃生条款），并修正不变量陈述。

#### L14 BindHostfxr 失败分支泄漏 dlopen 句柄
- **位置**：`Engine/Scripting/CoreCLRHost.cpp:113`
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：dlopen 成功但必需导出（hostfxr_initialize_for_runtime_config / hostfxr_get_runtime_delegate）缺失时直接 return false 不 dlclose；LoadHostfxr 继续尝试候选链下一个 root 时 api.lib 被覆盖——前一句柄泄漏（多 root 链最多 4 root × 2 形态 = 8 次）。触发条件苛刻（损坏/不完整 hostfxr），泄漏量最多几个句柄、进程退出回收。
- **证据**：:104/:113-116；全文件无任何 dlclose/FreeLibrary；:190-194 的 #53 修复仅覆盖 Fxr 对象重试泄漏；成功路径不 dlclose 属合理设计（CoreCLR 进程级不可卸载）。
- **建议**：BindHostfxr 失败分支释放 api.lib 后再 return false。

#### L15 Fx.Crit 使用 System.Random 违反自定纪律
- **位置**：`Engine/Scripting/dotnet/Lemon.SDK/Fx.cs:106`
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：`Random.Shared.NextSingle()` 违反 Pcg32.cs:2「脚本侧禁 System.Random（全引擎唯一随机源 = PCG32 子流）」与 ADR-010 D3「System.Random 全域禁用（测试 grep 防线）」；Fx 为呈现层不入 StateHash 无实际回放影响，但破口已被复制：demo/svr-test PlayerBehaviour.cs:60、DuelBehaviour.cs:31 已直接 new System.Random()。复核发现 ADR 所称 grep 防线测试本体不存在——解释了违规为何未被拦下。
- **建议**：改用引擎确定性 Rng 子流（BatchSystemFrame.RngSeed）或 SDK 侧 Pcg32；至少注释标注「仅呈现层许可」并补上 grep 防线测试。

#### L16 飘字数字格式化每次命中分配 string
- **位置**：`Engine/Scripting/dotnet/Lemon.SDK/Fx.cs:93`
- **类别/严重度/状态**：优化 · low · ✅ 复核成立
- **问题**：`number.ToString("0")` 每次分配一个小 string（~30-40B），高频战斗飘字构成持续 Gen0 压力，与 04 §5 GC 纪律「热路径每帧托管分配 >0 红字」口径相悖；CopyUtf8 的 ASCII 路径本身零分配，每次命中净分配就是这一个 string。
- **证据**：NativeApi.cs:303-314 ASCII 快路径；native 表无 float 直写变体、SDK 无 TryFormat 用法；该 float 重载正是伤害飘字主路径（svr-test/ui-test/vs-survivor 均用）。复核补充：`float.TryFormat` 写 stackalloc 即零分配、无需动 native 表；svr-test 同函数还有别的分配（Save.SetString 等），单修此处不足以归零。
- **建议**：SDK 侧 stackalloc + TryFormat 手写格式化（≤10 位 + NUL），或 native 表加 float 直写变体。

#### L17 Anim/Table warn-once 去重表只增不清
- **位置**：`Engine/Scripting/dotnet/Lemon.SDK/Anim.cs:67`（s_paramMissed :179；Table.cs:47）
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立（部分修正后收录）
- **问题**：Anim.s_missed/s_paramMissed 键含实体 id（entt 单调递增），解析失败模式持续时（如每只生成敌人都调词表外参数）无界累积并根住字符串；PlayReset 与换域 Reset 均不清这三张表（清的是 Scripting/Events/UI/Behaviours/SceneOps/Time），且 Lemon.SDK 钉在 Entry 的 ALC（全进程唯一）跨热重载持久存活。复核修正：Table.s_warned 键不含实体 id、上限受 Play 时刻快照约束，是有界集合（跨局陈旧键残留，极次要）。
- **建议**：PlayReset/域 Reset 时清空 Anim 两表，或键改 (类型名, 名字) 全局去重。

#### L18 FontBake 装箱无横向界检查
- **位置**：`Engine/Assets/FontBake.cpp:200`
- **类别/严重度/状态**：缺陷 · low（复核自 medium 降级）· ✅ 复核成立（定性修正后收录）
- **问题**：字形位图宽 cw > kPageW(512) 时换行后仍在 penX=1 放置，页合成 memcpy 每行越 512px 行界写 → 页内相邻行互毁 + UV 越界（字形渲染本身错）。复核降级理由：纵向截断 + pageH 倍增循环保证越界尾巴始终落在 buf 内最后一行——**无堆越界写**，仅缓冲区内页互毁的视觉破坏；经 importer 路径需 ~4em 宽字形（fontPx 钳 128、outline ≤8）才触发，直接调 API（tests/engine/RendererTests.cpp:483）无钳制。
- **证据**：:200-205 纵向有 kPageHMax 截断、横向无 cw>kPageW 拒绝；:283-284 行 memcpy 以 512 宽页承接 >512 宽行。
- **建议**：装箱循环开头对 cw > kPageW - 2*kSpacing 走 ++dropped 截断（与纵向同款），顺带钳 cw/ch ≤ u16 域。

#### L19 ResolveScene 未拒绝 ".." 越根
- **位置**：`Engine/Assets/ProjectFile.cpp:105`
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：只拒绝对路径，`"../x.scene"` 拼根后 stat 照常命中即原样返回——头注释承诺「绝对路径/越根 = 拒绝」只兑现一半，C# LoadScene 可越项目根读场景文件（调用链 GameEntry.cpp:265/273 与 EditorAppScripts.cpp:54/59 原样读出，无消毒）；另精确命中用 is_regular_file，macOS 大小写不敏感卷放行错大小写路径存入 out.path、Linux 构建下断裂（OS 语义推演，未实测）。
- **证据**：:104-108 仅 is_absolute 检查；同工程 AssetsTests.cpp:662-679 明确把 ".." 越根当必须拒绝（rename/import/向导三处），UiSubsystem.cpp:145-159 已有 lexically_normal 先例——无「有意允许」反证。
- **建议**：词法归一（weakly_canonical/lexically_normal）后确认仍在 projectRoot 之下再判存在；stem 分支统一大小写策略。

#### L20 PackAtlasPages 只装箱最后一张未关页
- **位置**：`Engine/Assets/AtlasBake.cpp:136`
- **类别/严重度/状态**：优化 · low · ✅ 复核成立
- **问题**：oversized 专属页（closed）一旦成为尾页，此前未满开页的剩余空间永久弃用（h 降序也覆盖不了「新高 shelf 放不下但矮图放得下」）；页数虚耗直接吃 bindless 槽容量（kMaxTextureSlots=256，AtlasStore 超限整体拒载）。现有单测即活证：3 页断言而 2 页足够——浪费被测试固化，落地需同步改断言。复核修正：「显存虚耗」是二阶的（页按用到 extent 裁剪），直接代价是页数/槽位。
- **证据**：:136-137 放置判定只看 pages.back() 且要求未关；:131 oversized push_back closed 专属页；整个循环无回溯更早开页路径。
- **建议**：维护未关页列表重试装箱（完整解）；oversized 页不参与游标推进。

#### L21 LoadBakedAtlasFile 失败时 out 残留半成品
- **位置**：`Engine/Assets/AtlasBake.cpp:347`
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：违反头文件契约「失败 false（out 保证为空）」（AtlasBake.h:69）：部分拒载路径（页尺寸越域 :354/payload 域不符 :359/载荷尺寸 :360/条目非法 :374-379）在 out 已 resize/填充后才返回 false；当前唯一生产消费方 AtlasStore.cpp:62-64 失败即弃局部变量未爆，契约靠巧合成立，且测试无失败后 out 为空的用例兜底。
- **建议**：所有 Reject 前 `out = BakedAtlasBuild{}`，或解析到局部临时、成功后再换出。

#### L22 ResetClips 锁内整体析构全部 PCM 大块
- **位置**：`Engine/Audio/AudioEngine.cpp:671-676`（另 Tick 锁内 LogMsg :863-866）
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：MountAll 每次进 Play 首步即调（AudioMount.cpp:34），全部 PCM 堆释放持 impl_->mtx 进行，设备回调（持同锁混音）被边界性卡顿（free/munmap 大块典型亚毫秒~毫秒级）；违反本文件 review #4 自立纪律「设备回调不得被 mtx 持有者的慢操作间接卡」。Tick 欠载告警 LogMsg 亦在锁内（一次性、量级小）。
- **证据**：:671-676 lock_guard 内 clips.clear()（每 Clip 含 PCM vector）；DataCallback :519-524 持同锁混音；>1MiB 未 preload 资产走流式不入 RAM（kStreamThresholdBytes=1MiB），整载大头是 SFX 累计与显式 preload。
- **建议**：锁内 swap/exchange 出 clips、锁外析构；Tick 日志先取快照再于锁外打。

#### L23 流式 clip 起播重读头与注册期字段无一致性比对
- **位置**：`Engine/Audio/AudioEngine.cpp:697-702`（消费界 :365-370/:410）
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：注册后重烤（编辑器后台 WarmAudioBakes/Rescan，原地覆盖同路径）致静默劣化：变短 → 差值区间逐帧计欠载静音放完（污染 underrun 验收观测并触发误导性告警）；变长 → 提前截断无日志；变声道 → 按旧宽度切帧字节错位无日志。均不崩溃、有界（channels 双侧校验 ∈{1,2}，无内存越界）。
- **证据**：Play() 锁外 OpenBakedStream 重读 .baked 头（:692）后 :697-702 直接灌入 feed，全程无与注册期 Clip 快照（:653-656）的比对；生产界用 feed 新头（:168/:180-183）、消费界用注册期字段（:369-370/:410）；EnsureLoaded 对已注册 guid 不重注册（AudioMount.cpp:65），「注册后重烤」时序成立。
- **建议**：Play() 锁内比对 frameCount/channels（loop 域），不一致拒播并红字提示资产已重烤需重注册。

#### L24 ParseSceneDoc 无条件整串拷贝 jsonText
- **位置**：`Engine/Serialization/SceneArchive.cpp:413`
- **类别/严重度/状态**：优化 · low · ✅ 复核成立
- **问题**：对所有档（含已是当前版本 v2 的绝大多数）无条件 `std::string text = jsonText` 全量深拷贝；text 仅在 `ver < kSchemaVersion` 迁移循环内使用——v2 档 `2 < 2` 为假，这次拷贝纯属浪费；5 个调用点（Load/BuildInto/预检/读档名/prefab 链）每次解析都付，万实体档 MB 级。
- **建议**：确认 ver < kSchemaVersion 后再拷贝（Migrate 需可变引用，迁移时确需副本，语义不变）。

#### L25 EntityCount 在 ReleaseDocChunk 后调用即抛异常
- **位置**：`Engine/Serialization/SceneArchive.cpp:566-568`
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：ReleaseDocChunk 尾部 `impl_->doc = Json()` 后，`EntityCount()` 的 `doc.at("entities")` 对 null json 抛 type_error.304，穿透 pimpl 与 C++ API 边界（vendored nlohmann 3.11.3 亲读确认必抛，工程无 -fno-exceptions，SceneSwitcher 全文无 try/catch）；当前调用方只在释放前查询，属未被踩中的公共 API 陷阱——SceneArchive.h:82 契约未标注释放后失效。
- **建议**：Parse 时缓存实体数，或判 doc.is_null() 回落 ledger.size()（CreateSlots 已把 ledger resize 到 entities.size()，语义与「槽账口径」一致）。

#### L26 UI ops 热路径逐参数物化 std::string
- **位置**：`Engine/Ui/UiSubsystem.cpp:922-923`（SetItems 同款 499/505/511）
- **类别/严重度/状态**：优化 · low · ✅ 复核成立
- **问题**：每条 op（上限 256/帧）把 arena 内 NUL 串物化 std::string 再转 Rml::String（doc/key 路径双层拷贝，斜杠分支另有两次 substr+拼接）；SetItems 每行再分配 kv/stack 两 vector、行 key 拷三次、525 以可 move 的左值再拷进 pair——实际比原发现更差。相比每行的 proto->Clone() DOM 克隆大头低一至两个数量级。
- **建议**：arena 层提供 string_view/Rml::StringView 直读，key/attr 免中间 std::string；SetItems 行缓冲上移、525 改 move。

#### L27 Rng 注释承诺不存在的断言
- **位置**：`Lemon/Engine/Core/Random.h:18`
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：头注释承诺「Seed() 前调用 Next() 会被断言拦截」，但全文件（85 行）无任何断言（grep 零匹配）——未种子使用静默产出 state_=0/inc_=1 的固定流。复核精确化：run-to-run 仍确定，真正危害是该流与世界 seed 无关（换 seed 行为不变）且所有忘播种实例共享同一条流（跨系统相关随机），「破坏逐位复现」措辞略重但方向正确。
- **建议**：Next() 首调用前加 seeded_ 断言，或修正注释移除不存在的承诺。

#### L28 SDL_CreateWindow 失败路径泄漏 SDL 初始化
- **位置**：`Lemon/Engine/Platform/Window.cpp:38-41`（析构 :46-50）
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立（部分修正后收录）
- **问题**：失败路径 return nullptr 不调 SDL_Quit（析构守卫走不进）；~Window 的 SDL_Quit 把「单窗口析构」与「全局 SDL 关停」耦合。复核修正：「同进程二次 Create 拆后者地基」有反证——Create 每次入口先 SDL_Init 能自愈，真正受影响仅「多窗口并存、先销毁其一」场景；全部 7 个调用点均单窗口、失败即退进程，现实影响趋近于零，属理论设计瑕疵。
- **建议**：失败分支补 SDL_Quit，或 SDL_Init/Quit 提为独立 RAII 与窗口生命周期解耦。

#### L29 FastSin/FastSinCos 大角度截断 UB
- **位置**：`Lemon/Engine/Core/Math.h:188`（FastSinCos :198、FastCos :193 同受影响）
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立（阈值修正后收录）
- **问题**：rad 未归一，`float x = rad*(4096/τ); int i = (int)x;` 在 |rad| 超域时截断 UB；x86 得 INT_MIN → 掩码后表内不越界但插值产出垃圾 sin/cos（静默错误函数值/渲染异常）。复核修正阈值：|rad| ≳ **3.29e6** rad（原发现 5.3e6 偏高约 1.6 倍——UB 比原述更早触发）。可达路径：Transform2D.Rot 脚本可写且全链无归一，官方模板 VsTemplateGen.cpp:1449 无界累加（默认 2.2 rad/s 约 17 天达阈）、「增量」品类挂机长跑在目标内。
- **建议**：入口 fmod(τ) 归一（注意该路径标称 15 万次/帧级热路径的 fmod 开销权衡）或输入断言限幅。

#### L30 WriteFileAtomic 固定 .tmp 后缀无并发防护
- **位置**：`Lemon/Engine/Core/FileOps.cpp:46`
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：同路径并发/跨进程双写者交叉写同一 `path + ".tmp"` 再各自 rename，可能把半档提升为正式档（POSIX 下 B 持同一 inode 继续写已换名正式档、B 的 rename 失败但脏数据已落），恰恰破坏该函数「磁盘满/中断不产生半档」契约（FileOps.h:30-33，场景/Prefab/存档/manifest 四条保存链统一走此口）。跨进程场景真实（双开编辑器写 manifest、编辑器与运行时共用 projectRoot 写存档），助手层与调用方层均无串行化（grep 无 mutex/lock）。
- **建议**：tmp 掺 pid/计数（`.tmp.<pid>`），代价一行。

#### L31 CLI 解析静默吞未知/残缺参数
- **位置**：`Lemon/Engine/Entry/GameEntry.cpp:429-436`（:439 paced 判定）
- **类别/严重度/状态**：优化 · low · ✅ 复核成立
- **问题**：六个 else-if 无 default/告警，未知参数与残缺参数静默落空；`--frames` 非数字经 atoi→0 使 paced 反转为 true——本意自动化的调用变成无窗口超时的交互模式无限挂住（负值机制略异：atoi("-5")=-5，挂住由 `frames > 0` 守卫永不触发所致，症状相同）。
- **建议**：未识别参数与非数字 --frames 打 ERROR 并非零退出（附用法行）。

#### L32 ParallelFor 每块 packaged_task 分配与同步开销
- **位置**：`Lemon/Engine/Core/JobSystem.cpp:127-133`（结构 JobSystem.h:41-49）
- **类别/严重度/状态**：优化 · low · ✅ 复核成立
- **问题**：每块经 Schedule 产生类型擦除任务体+promise 共享态堆分配、handles 向量每次调用一次分配、Enqueue 每任务两把锁 + notify、收尾 future 同步——细粒度高频并行下可能吃掉并行收益；现网块粒度（grain 256-2048）下每块实体级工作量远大于百纳秒级派发开销，登记为已知开销即可（单线程档/单块快路径无此开销）。
- **建议**：如成热点换无 promise 的计数完成式任务（保留值语义），或复用 per-World handles 缓冲。

#### L33 LogMsg 无级别门控
- **位置**：`Lemon/Engine/Core/Log.cpp:27-40`
- **类别/严重度/状态**：优化 · low · ✅ 复核成立
- **问题**：Info 级发布版也全量走 vsnprintf + 全局互斥锁内 fprintf（无缓冲 stderr syscall，含 Error 时 fflush 与 sink 回调）——JobSystem worker 并发日志按最慢消费者串行化；锁内 fprintf 是有意设计（防行交错），但全仓库无任何级别开关。复核澄清：主格式化 vsnprintf 在锁外，锁内是 fprintf 格式化+写出+计数+sink。
- **建议**：加运行时级别门控（LEMON_LOG_LEVEL 环境变量——引擎已有 LEMON_AUDIO 等同类开关先例），发布档默认 Info 静音。

#### L34 Assets↔Renderer 头文件级双向依赖
- **位置**：`Engine/Assets/TextureStore.h:13-14`（AtlasStore.h:14-15 同）↔ `Engine/Renderer/BitmapFont.h:15`
- **类别/严重度/状态**：设计 · low · ✅ 复核成立
- **问题**：Assets 拉 Renderer 的 GPU 句柄/注册表（RHI/Atlas，向上依赖，与分层铁律精神相悖），Renderer 拉 Assets 的数据格式头（FontBake）——目录分层靠纪律维持（Engine 单一静态库，链接器不拦截）。另有三组 .cpp 级软环：ECS↔Audio（World.h:16 vs AudioMount.cpp:11）、ECS↔Physics2D（Scene.cpp:5 vs SpatialHash.h→Entity.h）、Scripting↔Systems；SpatialHash.h:24-26 自认系「review #43 登记项」。文档未明文规定 Assets↔Renderer 方向，非明文违反。两方向性质不对称：FontBake.h 零 Vulkan 依赖纯数据格式，Assets 拉的却是 GPU 句柄。
- **建议**：GPU 上传型资产仓（TextureStore/AtlasStore）迁 Renderer 或接口反转；数据格式头（FontBake/AtlasBake）下沉中性层，使 Assets→Renderer 单向化。

#### L35 公共头 Process.h 直含 windows.h 无守卫
- **位置**：`Engine/Core/Process.h:8-12`
- **类别/严重度/状态**：缺陷 · low · ❌ **复核不成立**（保留收录）
- **问题（原发现）**：`#if defined(_WIN32)` 下直接 `#include <windows.h>`，头内无 WIN32_LEAN_AND_MEAN/NOMINMAX 守卫，min/max 宏污染下游 Editor/tools 编译单元（Windows 目标）。
- **复核不成立理由**：根 CMakeLists.txt:16-22 在所有 add_subdirectory 之前对 MSVC 全局 `add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)`，覆盖全部自有 TU 与 CPM 子项目，注释原文即写明此坑（「批⑦ review 实锤②：min/max 宏会咬 std::min/std::max」）；项目 Windows 构建全部走 MSVC（CMakePresets.json win preset=VS2022；CI windows runner），无 MinGW 路径；该问题已在 2026-10-05 批⑦ review 修复（DevLog 2026-10-05-b7-review-hardening.md:32、07-Porting-Matrix.md:194 登记），修后 CI win job 首绿。头内自卫仅是纵深防御偏好，非现行构建体系下的缺陷。
- **保留建议**：若追求头文件自洽可在包含前定义守卫，但非必须。

#### L36 ParallelFor 任务异常被静默吞掉
- **位置**：`Lemon/Engine/Core/JobSystem.h:53-55`（JobSystem.cpp:136）
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：Complete 只 `wait()` 不 `get()`，packaged_task 存入 future 的异常无人观测（grep 全引擎无一处对 JobHandle 调 .get()）——并行块（如 TargetBoard::Rebuild 建桶任务内的 bad_alloc）得到部分完成的静默结果。附带不一致：单线程诊断档 Schedule 就地执行异常直接抛给调用方，同一故障 `--threads 1` 可见、多线程静默。
- **建议**：Complete 改 wait 后 get() 重抛（或至少 LEMON_ERROR 记录），主线程统一拦截。

#### L37 game-assets 重建回调按引用捕获栈对象且不反注册
- **位置**：`Lemon/Engine/Entry/GameEntry.cpp:790-796`
- **类别/严重度/状态**：缺陷 · low · ✅ 复核成立
- **问题**：`AddRecreateCallback("game-assets", [&]{...})` 按引用捕获 main() 栈对象（atlas/textures/bakedAtlas/ui 等）但 token 被丢弃、全程无 RemoveRecreateCallback，违反 RHI.h:199-206 自述契约（「不摘除 = 设备丢失重建 UAF」）；当前栈序析构全部先于 device 死亡且收尾无触发点，benign 埋雷。复核精确化：recreate 回调仅设备丢失路径触发（resize 的 RecreateSwapchain 不调回调），实际爆点比 GameEntry.cpp:788 注释所述更窄。
- **建议**：保存 token 并在 main 收尾显式 Remove，或将资产页重建封装为拥有 token 的 RAII 对象随捕获对象同寿。

#### L38 AudioSystem::Tick 对 bindings_ 线性扫描
- **位置**：`Engine/Systems/Systems.cpp:1383-1387`
- **类别/严重度/状态**：优化 · low · ✅ 复核成立
- **问题**：每 AudioSource 实体内层线性查 bindings_，O(sources×bindings) 平方开销；未绑不起播的声源同样全扫。每次比较 8 字节 Entity 连续内存驻 L1，实际耗时微秒级以下（N=100 约 5 千-1 万次比较/tick，原表述在百级低端偏高约 2 倍，量级方向正确）。
- **建议**：bindings_ 换 Scene::EnttIndex 位索引数组（SceneExtractor 的 SlotMap + version 校验先例）。

#### L39 World 成为通道聚合 hub
- **位置**：`Engine/ECS/World.h:16`（成员 :305-314、访问器 :180-238）
- **类别/严重度/状态**：设计 · low · ✅ 复核成立（部分修正后收录）
- **问题**：直接包含 16 个模块头、按值持有 9 类 11 个非 ECS 通道实例（RtUi/Cards/Clips/Controllers/Fx/Saves×3/Tables/Tweens/Audio），每个新 C# 能力都长在 World 上——与 M23（vtable）同根的镜像面。复核修正：AudioChannel.h 是唯一跨目录通道头（其余通道全在 ECS/ 内）；头文件包含图**文件级无环**（Audio 目录所有头只依赖 Core/，反向仅 AudioMount.cpp 包含 World.h），「目录环」仅在编译单元层成立。
- **建议**：通道族抽独立聚合（World::Channels()），AudioChannel 挪入 ECS 目录与兄弟通道同置，阻断 ECS↔Audio 目录级耦合。

## 各模块健康度

- **渲染内核（Vulkan）**：整体质量高——fence-only acquire + 按图像持有 present 信号量方案自洽、设备丢失恢复成体系（pre-destroy/recreate 双回调 + token 反注册）、幂等 Init 与 bindless 槽位记账等细节均有据可查；本次实质风险集中在持久离屏 RT 的跨提交同步盲区与设备丢失路径上的两处边角（上传 abort、RmlUi 几何句柄悬垂），热路径无明显每帧堆分配或冗余同步。
- **ECS 核心**：质量整体很高——两阶段销毁、deferred spawn、chunk 归并并行、POD 布局钉、TakeAll 事件快照等历史审查修复均已落地且自洽；1 处中等 UB（FxChannel 定长 memcpy 越界读）与 6 处低危契约/性能问题，无崩溃或数据丢失级缺陷。
- **组件与系统**：组件层布局冻结与确定性纪律扎实、系统管线顺序显式自洽；命中连续性（无扫掠）与目标板每帧堆分配是两块真实短板。
- **托管脚本宿主**：边界契约纪律性很强——59 槽 vtable 与全部镜像 struct 逐字段核对一致、帧缓冲 reserve + D5 双层护栏、UCO 异常隔离与池化命令纪律基本成立；主要风险集中在失效实体句柄的 emplace 防御缺口（可致池污染，H1）与三处线程/迭代器纪律遗漏（scene 事件主线程内联、事件订阅表迭代、逐实体跨线程往返）。
- **资产系统**：防御功底整体扎实（号域 sane 上限、u32 回绕防线、原子写、双账对账、坏档隔离），但三处「同一逻辑双实现」各留了一条单侧加固的崩溃缝（PlayCaches 弱解析器、manifest 账与 .meta 几何不对账、FontBake 横向无界 + 装载侧尺寸无校验）；性能面索引查询缺 guid/spriteId 哈希、spawn 热路径每发全量 JSON 重解析。范围内无异步加载状态机与世代句柄（全同步 + 只增不减 spriteId），依赖面为扁平索引无环。
- **音频系统**：纪律良好的音频内核——线程模型单一（主线程短临界区 + 回调锁内混音）、SPSC 内存序正确、池管理与烘焙边界历史修复密集到位；剩余问题集中在锁内阻塞残留面（fclose 链）与流式消费逐帧粒度，均不致崩溃。
- **UI 与序列化**：纪律严整（两阶段销毁、响亮失败、坏档防御与历史 review 修复均落位）；主要风险在 UI 侧元素裸指针缓存对 DOM 突变不设防（H3）与 Rml 生命周期失败路径未配对（M20），序列化侧仅剩边缘校验缺口。
- **核心平台入口**：整体纪律扎实——退出销毁序（batcher→rm→world→ui→audio→atlas→ScriptHost→Device→Window 的栈序逐项核对全部安全）、原子写、日志锁化、设备丢失同步重建都刻意处理过，无高危缺陷；主要缺口是 lemon-game 输入采集层与窗口层已有的失焦清键修复不同步（卡键）。Vulkan 符号线索核验：GameEntry.cpp:467/475 命中均为注释（亲跑 grep + 逐行读确认），代码仅使用 rhi:: 抽象类型——Renderer 之外无代码级 Vulkan/VMA 类型，硬规无违规（本次综合重跑 grep 再证）。
- **架构分层与边界（横向，综合自其发现）**：分层铁律（依赖向下、Vulkan 零泄漏）整体守住——确定性检查证实 Renderer 之外无代码级 Vulkan 符号；剩余债务集中在 C++/C# 边界面膨胀（59 槽 vtable）、World 通道聚合 hub、Assets↔Renderer 头级双向依赖三类结构性观察，均为登记型而非功能缺陷。

## 未覆盖与存疑项

**未覆盖**：
- `Editor/` 全目录（按用户要求排除；编辑器文件仅作为影响面佐证被引用，未独立评审）；第三方库（ImGui/RmlUi/VMA/entt/nlohmann/miniaudio/FreeType——vendored 源码仅被复核员用于确认行为，未评审其自身质量）；`Samples/` 与 `tools/` 仅作佐证引用；`Prowl*`/`duality` 等参考目录未涉及。
- **未做动态验证**：全程静态评审 + 只读复核，未运行构建、测试套件、sanitizer 或基准。文内量级结论凡涉「毫秒级尖峰」「<0.1% 单核」等为机制估算；「ASAN 必报」（M5）、「macOS 大小写不敏感行为」（L19）、「strtoull 三例输入」（M21，复核员另以独立片段实测过 stoull）等为标准语义推定或片段实测，均非引擎构建下的实跑结果。

**复核修正后收录的定性**（报告内已按修正口径呈现，此处汇总）：
- M2 RmlUi 几何句柄：机制为「跨块释放外来句柄」的结构性元数据污染（旧块被泄漏放弃而非销毁），非必然 use-after-free；后果同向。
- M9 lemon_scene_event：「此后每次换装确定性泄漏（MB 级）」归因不成立——域线程 pin 自 M4.5 起即确定性常态，该发现未引入或加重泄漏，量级约百 KB/域；真实危害是 D1 纪律破坏 + B 线恢复前提存疑 + 注释/ADR 与实现矛盾。
- L18 FontBake 装箱：严重度降为 low——无堆越界写（pageH 倍增兜底），仅页内互毁 + UV 越界的视觉破坏。
- L17 warn-once：Table.s_warned 有界（键不含实体 id），仅 Anim 两表无界成立。
- L28 Window：「二次 Create 拆地基」有反证（重 Init 自愈），现实影响趋近零。
- L29 FastSin：UB 阈值修正为 ≈3.29e6 rad（原 5.3e6 偏高，UB 实际更早触发）。
- M22 失焦卡键：IsKeyDown 的受益面修正（Samples 6 处受保护、编辑器 Play 态走 ImGui），「修复未落在任何读移动键的路径上」的结论反而更强。
- L10 场景记录：「只增不删」是回放确定性契约（测试钉死），复用句柄方案需先评估发号语义。

**存疑待作者定夺**：
- L7 Scene::Destroy 的头注释契约（宣称 worker 可直调）与实况（已全部主线程化）相悖——保留哪个口径是设计决定。
- L9 SceneSwitcher 坏槽直调 CommitDestroys 的豁免理由未锚定，未来维护者若在可入队上下文模仿即成实害。
- M1 离屏 RT 竞态、L2 flush 缺失的实机复现依赖特定硬件/驱动形态（MoltenVK 现网均 coherent），修复属规范性正确而非已观测故障。
- M17 的「预解析 json 入 Entry」方案与 SceneArchive.h:69「json 类型不出头文件」红线冲突，落地需 pimpl 或改走二进制快照。

**确定性检查**：Vulkan 符号 grep（本次综合以 `grep -rInE '(Vk|Vma|VMA|vk|vma)[A-Za-z_]'` 重跑：481 处命中，pattern 宽于材料原 432 口径）；Renderer 之外 4 处 = GameEntry.cpp:467/475（均为 `//` 注释，与核心平台评审员逐行归类一致）+ miniaudio.h 两处宏（vendored 第三方，范围外）。**硬规无违规结论成立。**