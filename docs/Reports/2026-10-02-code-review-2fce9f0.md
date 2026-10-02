# Engine/Editor 全量代码评审（第二轮，基线 2fce9f0）

> 2026-10-02 · 工作流评审产出：12 个模块并行深审 + 1 个系统级架构总审，高/中严重度发现逐条由
> 独立复核员**重读代码**验证（confirmed 41 / partial 3 / refuted 0），低严重度 59 条按复核预算未复核。
> 范围：`Engine/` 与 `Editor/` 全部手写代码（213 个文件逐文件通读；vendored 第三方仅核对登记），
> 基线 main@2fce9f0（工作树仅 demo 未跟踪资产；上一轮 = [2026-10-02-code-review-620657c.md](./2026-10-02-code-review-620657c.md)，其 37 条修复批即本基线最新提交）。
> 一次性快照：产出后不更新；修复进度在 Plans/DevLog 跟踪，引用记法「review 2026-10-02 #N」。

---

## 0. 结论 TL;DR

工程基本面健康：构建与全部 3 项 ctest 通过，Vulkan 头零泄漏与 Engine→Editor 反向依赖两条硬纪律机器验证零违例，分层、防御性编程与演进治理明显高于典型同期原型。103 条发现中 44 条经独立复核确认（0 条被反驳），存量风险集中在设备丢失恢复链、并行确定性与一批常规交互即可触发的正确性缺陷。最要紧的三件事：

1. **补全设备丢失恢复链**——GetInternalBridge 缓存跨重建不失效（#1，high）与 ImGuiBackend 未注册重建回调（#24，medium）两处叠加，带 RmlUi/ImGui 后端的编辑器进程遇真实设备丢失恢复必然使用已销毁句柄；
2. **按 03 §4 落实并行销毁的主线程稳定归并**，守住状态哈希与金回放这一引擎核心承诺（#2，high，结构性成立、未实证复现）；
3. **修掉用户可直接触发的问题**——UI 事件多拷贝丢失（复选框单击即触发，#3）、Clip/AnimSet JSON 转义缺失致坏档覆写=数据丢失（#5；ControllerToJson 支暂无生产调用方）、Play 中脚本/Prefab 操作跨世界串写并把撞号实体永久写进 .prefab（#4，主体成立）、SetItems 中文 key 字节长度失步（#22）。

另有 M7a 计划唯一缺口：ECS→渲染提取环节未列入搬运清单（#6），修计划文档即可。

## 1. 确定性检查（脚本执行，非模型判断）

- `cmake --build build/mac`（增量，基线 HEAD）exitCode=**0**；
- `ctest --test-dir build/mac`（engine-tests / imgui-isolation / script-tests）exitCode=**0**；
- 纪律 grep：Vulkan 头包含越界（Engine+Editor 全域，Renderer .cpp 除外）**零命中**；`Engine/` 对 `Editor/` 的反向 include **零命中**。
- 代码层面新发现的同类越界一条：EnTT 头泄漏进引擎公共头（#43，01 §5 三兄弟纪律，无机器守卫）。

## 2. 评审单元与统计

| 单元 | 通读文件 | 发现 |
|---|---|---|
| 渲染内核（Vulkan RHI） | 17 | 10 |
| ECS 内核、组件与序列化 | 36 | 9 |
| C# 脚本桥（C++ 侧） | 8 | 14 |
| C# SDK 与托管宿主 | 29 | 13 |
| 玩法运行时系统与核心基座 | 19 | 16 |
| 音频系统 | 9 | 9 |
| UI 系统（RmlUi 集成） | 5 | 11 |
| 编辑器应用壳 | 15 | 13 |
| 编辑器冒烟与自动化框架 | 6 | 14 |
| 编辑器资产管线 | 14 | 15 |
| 编辑器面板 | 9 | 13 |
| 编辑器工具、交互与模板 | 16 | 14 |
| 系统级架构总审（横向抽样，未计入文件口径） | — | 11 |

合计 103 条 = high 6（全确认）/ medium 38（全确认，含 3 条复核判「部分成立」，修正细节在各条证据内）/ low 59（未复核）。文件数为各单元自报。

## 3. 问题清单

严重度：high = 会崩溃/数据丢失/错误结果/违背核心设计；状态：已确认 = 独立复核成立（confirmed 或 partial），未复核 = 低严重度未进复核预算。证据中「复核：」之后为独立复核员的验证意见（含对原判的修正）。

### 3.1 高（#1–#6，全部已确认）

**#1 · high · 渲染内核（Vulkan RHI） · 已确认**
`Engine/Renderer/RHI.cpp:1382` — GetInternalBridge 的 bridgeCache 惰性填充后永不失效，设备丢失重建后返回已销毁的旧 VkDevice/VmaAllocator 句柄，RmlUi 后端恢复路径在其上创建资源 = UB/崩溃。
证据：RHI.cpp:1383 `if (!m->bridgeFilled)` 一次性缓存；HandleDeviceLost（RHI.cpp:761-784）销毁并重建 device/allocator 后逐回调重建，全仓 grep 确认无任何 bridgeFilled=false 重置点；唯一消费者 RmlUiBackend.cpp:171（CreateAll，丢失恢复 RecreateAfterLoss:497 同走）取 bridge 后 vkCreateDescriptorSetLayout/vkCreateGraphicsPipelines/vmaCreateBuffer。SimulateDeviceLoss 仅被 rhi-smoke/bench-mow（均无 RmlUiBackend）调用，宿主编辑器路径从未被设备丢失验收覆盖｜复核：RHI.cpp:1382-1392 确为一次性缓存，全仓无重置点；HandleDeviceLost（761-784）销毁旧 device/allocator 并经 PickPhysicalAndLogical 重建后回调 rmlui 的 RecreateAfterLoss→CreateAll（RmlUiBackend.cpp:171/497），此时 GetInternalBridge 返回的仍是已销毁旧句柄，随后的 vkCreateDescriptorSetLayout(:192)/vkCreateGraphicsPipelines(:300)/vmaCreateBuffer(:315) 即在死句柄上创建资源，恢复路径必然 UB。HandleDeviceLost 由真实 VK_ERROR_DEVICE_LOST 触发（RHI.cpp:1208/1270/1288），而 SimulateDeviceLoss 仅 rhi-smoke/bench-mow 调用且二者无 RmlUiBackend，编辑器路径无覆盖，证据与覆盖性说法均属实。

**#2 · high · ECS 内核、组件与序列化 · 已确认**
`Engine/ECS/Scene.cpp:30` — 并行系统在 ParallelFor worker 内直接调 Scene::Destroy，销毁队列的入队序 = 互斥锁获取序（跨线程不确定），CommitDestroys 按该序执行 registry_.destroy，组件池终态布局与实体槽回收序随之随线程交错漂移，破坏状态哈希与金回放确定性。
证据：Scene.cpp:30-34 锁内 destroyQueue_.push_back（次序无任何稳定化）；Scene.cpp:55-60 按队列序 destroy、无排序归并；Systems.cpp:1162-1169 ProjectileLifetimeSystem 在 world.Jobs().ParallelFor lambda 内调 scene.Destroy；vendored entt 3.15（swap_and_pop :19874-19886、generate 从 packed[free_list] 取最近销毁槽 :43961-43966）证实删除序决定池布局与后续 Create 的实体 id；StateHash.cpp:73-78 按池序哈希且 :51 哈希 e.id。03 §4 并行契约第 3 条明令实体销毁须『收集意图 → 主线程按池序稳定归并提交』，2026-09 审计只修了数据竞争（Scene.cpp:27-29 注释），归并序一节未落实。未做实证复现｜复核：证据全部属实：Systems.cpp:1162-1170（在 Engine/Systems/）确在 ParallelFor lambda 内调 scene.Destroy，JobSystem.cpp:121-135 证实池>256 时跨 worker 真实并行；entt swap_and_pop 删除序决定 packed 布局、generate 从 free_list 取最近销毁槽定后续 Create 的 id；StateHash.cpp:51/73-78 经 ComponentCatalog.cpp:386-394 ForEachComponent 按池序哈希且 FNV 序敏感。违反 03-ECS-Runtime.md:124 并行契约第 3 条；2026-09-19-m2-full-audit.md:12 证实审计只加了锁未做归并序稳定化（评审把审计日期误写为 09-26，小偏差不影响结论）。同帧多 worker 各销毁≥1 实体时提交序随线程交错漂移，问题结构性成立；本次未做实证复现。

**#3 · high · UI 系统（RmlUi 集成） · 已确认**
`Engine/Ui/UiSubsystem.cpp:956` — DrainEvents 多事件派发时把队首事件拷贝 n 份、其余 n-1 条被删除丢失（dst[k] 恒赋 front()）。
证据：UiSubsystem.cpp:956 循环体只有 `dst[k] = i.events.front();`；消费方 ScriptHost.cpp:932-934 以 cap=64 调用并把 n 条全部派发给 C#。RmlUi 6.3 上游 InputTypeCheckbox.cpp:28 证实 checkbox 单击同时派发 Click+Change（核读 build/mac-debug/_deps/rmlui-src）。git log -L 显示该函数自 13d4871（批③c）引入后未改动，smoke/脚本断言均为单事件口径故漏网｜复核：UiSubsystem.cpp:952-958 属实：循环体仅 `dst[k] = i.events.front();` 后 erase 前 n 条，n≥2 时队首被拷贝 n 份、其余 n-1 条静默丢失。消费链核实：UiBridge.h:75 kUiEventsPerDrain=64 → ScriptHost.cpp:932-934 n 条全量派发 C#（EditorApp.cpp:197 绑定真实 DrainEvents）；且 UiSubsystem.cpp:267-268 同时订阅 Click/Change、:343 每条独立入队，checkbox 单击即产生 2 条不同事件——非理论场景。git log -L 952,959 仅 13d4871 一笔；tests/script/main.cpp:1640-1644 假钩子恒 return 1、:1758 断言单事件 c1r0，多事件路径确无覆盖。

**#4 · high · 编辑器应用壳 · 已确认（部分成立）**
`Editor/EditorContext.cpp:322` — 脚本/Prefab 操作族（AttachScript:322、SetSlotScript:348、RemoveScriptSlot:369、MakePrefabFrom:433、ApplyPrefabInstance:784、RevertPrefabInstance:800（含 :823 SceneDestroyEntityTree(*scene_,e)）、BreakPrefabInstance:839）恒操作 edit 世界 scene_，Play 中经 Inspector/Hierarchy 调用时拿 play 句柄戳 edit 世界：多数情况 id 撞车（EnterPlay 从快照重建、两 registry 同源分配 id，Scene.h:113 ToEntt=id-1 无世界隔离）导致假失败但暗改编辑侧，其中 Apply 路径把撞号实体的子树经 WriteFileAtomic 永久写进 .prefab 资产文件（Stop 重建无法回滚）。
证据：对照同文件 CreateEntity:259 / DuplicateEntity:851 / DestroyEntityTree:865 已改 ActiveScene() 且注释明言『原恒走 scene_ = 拿 play 句柄戳 edit 世界，no-op 或 id 撞车错删』——同一已修 bug 模式漏掉这七个函数；调用点 InspectorPanel.cpp:858/801/816/734/740/751 与 HierarchyPanel.cpp:426 在 Play 中可达（旁证：这些调用点对 PushStructuralUndo 单独做 !ctx.Playing() 守卫，说明 Play 是设计内编辑路径），违反 05-Editor.md §4 M4 决议『Play 中允许编辑、改动只落 Play World，Stop 即丢』及 EditorContext.h:144『Play 中编辑不动编辑侧脏标记』（AttachScript:342 置 dirty）｜复核：主体成立：AttachScript/SetSlotScript/RemoveScriptSlot/ApplyPrefabInstance（:792-793 SaveEntityTree(*scene_)+WriteFileAtomic 写 .prefab）/RevertPrefabInstance（:823）/BreakPrefabInstance 恒走 scene_ 且置 dirty；Scene.h:113 无世界隔离、EnterPlay:984-990 快照同源重建致 id 撞车、InspectorPanel:697-708 Play 中仅横幅不 return 使各调用点可达、05 决议与 EditorContext.h:144 均属实。唯 MakePrefabFrom（:433 恒走 scene_ 属实）的 UI 入口 HierarchyPanel.cpp:424 以 !ctx.Playing() 置灰，Play 中不可达，且其写盘用 ofstream（:446-451）非 WriteFileAtomic——该函数的 Play 触发路径不成立。

**#5 · high · 编辑器资产管线 · 已确认**
`Editor/Assets/ClipEdit.cpp:100` — 三个手写 JSON 序列化器（ClipToJson/AnimSetToJson/ControllerToJson）对名字字段无 JSON 字符串转义，且 AnimSetToJson/ControllerToJson 用定长 char[] snprintf——名字含 `"` 或 `\`（如状态名 atk\idle）写出非法 JSON，长名（段名 >约 63 字符、from/to 合计 >约 163 字符）被静默截断，下次解析即失败，原档被覆写 = 数据丢失。
证据：ClipEdit.cpp:100-101 clip 名原样入串；ClipEdit.cpp:182-187 段名经 char sg[128] snprintf；ControllerEdit.cpp:168-169/190-194 控制器名与状态名原样拼接、199-201 from/to 经 char head[192]。对照同模块 Csv.cpp:160-168 用 nlohmann dump（带转义）；AnimationPanel.cpp:1320/1871 的 InputText 无长度限制，TrySave（AnimationPanel.cpp:196-203）生成后不 roundtrip 校验直接 WriteFileAtomic 覆写原档｜复核：逐行属实；AnimationPanel.cpp:196-203 TrySave（及 1117-1119 TrySaveSet）生成后无 roundtrip 校验直接覆写，1871 名称 InputText 零校验、CommitSegRename:1504-1508 只拦 /\.. 不拦引号不限长，用户输入即可触发坏档覆写=数据丢失，链路闭合成立。两点小出入：截断阈值实为约 63/163 字符（评审约 65/150 系近似）；ControllerToJson 当前无生产调用方（仅 tests/engine_tests.cpp 引用，controller 档暂无编辑器写盘入口），该支缺陷属实但暂不可达，不推翻整体。

**#6 · high · 系统级架构总审 · 已确认**
`Editor/Interaction/ViewportRenderer.cpp:404` — ECS→渲染提取已有两套实现（sample 简化版 + 编辑器产品级），引擎管线的 Extract 阶段空转；M7a 搬运清单完全未列提取环节，GameEntry 按计划对标 anim-smoke 极可能写出第三套。
证据：01-Architecture-Overview §3.2 定义 Extract 为引擎管线阶段；SystemStage::Extract 枚举存在（SystemPipeline.h:17）但全仓唯一 Stage()=Extract 的实现是 Samples/anim-smoke/main.cpp:139-168 的简化版（注释自认「简单全量刷新」「实体池静态」）；产品级实现在 ViewportRenderer.cpp:404-470 且签名锚 EditorContext&；对 docs/Plans/M7a/M7a.md 全文 grep「提取/Extract/Renderable」零命中，§1 事实#5 称 GameEntry 循环对标 anim-smoke｜复核：所有证据点均属实：全仓唯一 Stage()=Extract 实现在 anim-smoke:139-170（匿名 namespace 不可链接复用），产品级 ExtractScene(EditorContext&) 在 ViewportRenderer.cpp:404；World::Step（World.cpp:96-97）只跑 Essential+FixedTick，RunStage(Extract) 唯一调用点是 anim-smoke:341，管线 Extract 在产品路径空转。M7a.md 批④装配序列（:112）主循环仅列 InputState/fixed-step/audio/camera follow/sprite pass+UI pass 无提取环节，且 :110 明确 lemon-game 不链 editor-core——现有两套实现一套不可复用一套不可搬运，问题成立。

### 3.2 中（#7–#44，全部已确认）

#### 渲染内核

**#7 · medium · 已确认**
`Engine/Renderer/RHI.cpp:1042` — DestroyTexture 销毁纹理后不清退 bindless 纹理数组槽的描述符，与 DestroyBuffer 的同款防御不对称，槽位悬空完全依赖调用方纪律。
证据：DestroyBuffer 在 RHI.cpp:929-932 摘除 boundStorageBufferIds 同 id 槽并注释『句柄 id 复用……描述符悬空指向已销毁缓冲(实测画面错乱)』；DestroyTexture（1042-1050）无对应物——销毁后 bindlessSet binding0 该槽仍写有已销毁的 VkImageView，后续帧采样该槽即 use-of-destroyed-view。TextureRes 不记槽位，RHI 层无法自查，热重载删资产是常规操作｜复核：对照 DestroyBuffer:929-932 防御确属不对称，且 TextureRes(RHI.cpp:101-107) 不记槽位、BindTextureToSlot(1168-1182) 不记 id→slot，RHI 层确实无法自查。细节小瑕：ThumbCache/ViewportRenderer 的纹理走 ImGui_ImplVulkan 独立描述符集不占 bindlessSet 槽，『重绑同槽』仅 AssetGpuCache.cpp:85-90 成立；评审还遗漏 AssetGpuCache.cpp:144/179 两处销毁占槽纹理不重绑的路径（靠无引用/后续 RebuildAll 兜底），反而更印证『完全依赖调用方纪律』——当前无已触发 bug，medium 定性一致。

**#8 · medium · 已确认**
`Engine/Renderer/RHI.cpp:990` — 纹理上传为全同步路径（每张一次 staging 分配 + ImmediateSubmit + vkQueueWaitIdle），偏离 02 §5『异步暂存上传、主线程只登记』的设计承诺，批量资产导入时串行卡顿主线程。
证据：02-Rendering-Vulkan.md:151 明文『纹理｜异步暂存上传 + mipmap 生成走计算/传输队列，主线程只登记』；RHI.cpp:691-692 ImmediateSubmit 以 vkQueueSubmit+vkQueueWaitIdle 收尾，UploadTexture（968-1040）每张纹理单独 staging 后走一次完整同步提交；ThumbCache.cpp:71 注释自认『同步，几 ms 量级』｜复核：证据全部属实；RHI 全文无 transfer queue/上传线程，AssetGpuCache.cpp:89/132 批量导入逐张调用，N 张图 = N 次串行全队列等待，且 grep DevLog/ADR/02 分册未找到任何偏差标注——问题在当前代码下真实成立。

**#9 · medium · 已确认**
`Engine/Renderer/Particles.cpp:119` — 粒子视口剔除是中心点测试 + 硬编码 64px margin，不含粒子自身半径，大尺寸粒子（光晕/雾类特效）在屏幕边缘整颗突然消失；且与精灵路径的分桶搬运代码重复、剔除策略已分叉。
证据：Particles.cpp:112 `Expanded(64.0f)` 后 119-121 仅用 p.pos 点判定；对照 Renderable.cpp:206-215 精灵路径用 radX/radY（含尺寸）+ 旋转保守半对角线。EmitterConfig.sizeMax（Particles.h:38）无上限校验，大光晕粒子（>128px）可稳定复现边缘突失。分桶/搬运逻辑与 Renderable.cpp:222-241 近乎逐行重复｜复核：证据全部属实：粒子渲染尺寸即像素宽高（163-165 行 Lerp），sizeMax 无上限校验（Emit 35-78 行只钳 blend/filter），sizeMax>128 时半径>64>margin，边缘整颗突失确定可复现；两处分桶搬运近乎逐行重复（注释措辞都一致），bench-mow:250-252 调用方甚至自行 +64 补偿硬编码 margin。

#### ECS 内核、组件与序列化

**#10 · medium · 已确认**
`Engine/Serialization/SceneArchive.cpp:161` — Save 不过滤待销毁实体，且 DestroyQueueTag 作为登记组件被整体序列化——窗口期内保存的场景档含『死实体 + DestroyQueueTag:{}』，读档复活。
证据：SceneArchive.cpp:161-173 WriteEntity 遍历全部登记组件无过滤，DestroyQueueTag 在 ComponentCatalog.cpp:438 登记（readFn=TryGet）→ 打标实体写出空对象、ReadEntity（:202）按名 emplace 重新打标。该组合已在编辑器侧真实出过事故：EditorContext.cpp:867-871 注释实录『快照含待删实体 → Redo 会复活被删实体（smoke-ui 真人链路抓到）』，修复只落在调用点补 CommitDestroys，引擎侧 Save 仍无防线｜复核：证据逐行核实属实。两处修正：①后果比评审更重——CommitDestroys（Scene.cpp:48-61）只消费 destroyQueue_ 不扫 tag 池，Load 复活实体永不入队，不会被『下一次提交』销毁而是每帧 tick 的**永生僵尸**；②EnterPlay 窗口在真人交互下不可达（主循环 sim 段先于 BuildUI 且编辑管线含 DestroyCommitSystem），真正必中的同帧路径是 InspectorPanel.cpp:739-742 Revert 成功即调 PushStructuralUndo、其 after 快照（EditorContext.cpp:1127）先于清队执行，Redo 必复活——正是 867-871 实录事故在 Revert 路径的漏修。核心问题（引擎侧 Save 无防线）真实成立。

**#11 · medium · 已确认**
`Engine/ECS/Hierarchy.h:32` — SceneDestroyEntityTree 的头文件契约与实现相反：注释称『直接提交销毁（不经 destroyQueue_）』，实现实为两阶段入队（延迟到 CommitDestroys 才真正销毁），依赖即时性契约的调用方会拿到『看似已删、实际仍在』的实体。
证据：Hierarchy.h:31-33 vs Hierarchy.cpp:132-135（s.Destroy(d) 入队，cpp 内注释自认『两阶段入队』）。头尾注释互相矛盾，是契约改动后头文件未同步的漂移；与 #10 的编辑器事故同根｜复核：属实——Scene.cpp:23-35 的 Destroy 仅打 DestroyQueueTag 并入队，真正销毁延迟到 CommitDestroys；入队后 Alive()（仅查 registry_.valid）仍为 true，依赖即时性的调用方（如 EditorContext.cpp:823 Prefab Revert 删除后立即重建）确实会拿到看似已删实际仍在的实体。

#### C# 脚本桥（C++ 侧）

**#12 · medium · 已确认**
`Engine/Scripting/ScriptBox.h:52` — ScriptSlot.className 定长 24 字节静默截断：超过 23 字符的类名存不进持久键，且截断名与真实短名可互相碰撞。
证据：AppendSlot 内 `if (n > 23) n = 23;` 无告警；编辑器侧 SetSlotScript 同样 snprintf 静默截断（EditorContext.cpp:361），ResolveScriptTypeId 按全名精确匹配（:317）必然失配 → EnterPlay 只报『脚本类型未注册（跳过）』（:393），用户看不出是名字过长。Unity 风格命名常态超 23（'PlayerMovementController' 恰 24 字符即中招）｜复核：证据全部属实：持久化写出的就是截断名（SceneArchive.cpp:181），EnterPlay/热重载只报『脚本类型未注册』且打印的是截断名；碰撞形式需精确（超长类截断名恰好等于某真实 ≤23 字符类名时把后者误判已挂载/静默清洗丢弃），medium 定级合理。

**#13 · medium · 已确认（休眠隐患）**
`Engine/Scripting/ScriptHost.cpp:816` — ApplyStructural 的占位 id 解析是 O(n²)：每条结构命令对 resolved 线性扫描。
证据：Resolve lambda 对 resolved vector 逐对扫描（818-821），每条 op1-5 都调用（:838/855/873/879）；C# 侧每实例化一个实体通常跟 2-3 条引用占位的命令，单帧 5k 实体 ≈ 3.7 千万次比较 ≈ 数十毫秒帧卡；换 flat_hash_map 即线性化｜复核：代码形态属实（批量下确为平方级）。但占位 id 唯一生产者 SceneOps.Create()（SceneOps.cs:43-48）全仓库零调用；实际生成路径 Instantiate.Spawn/Prefab 走 NativeSpawnSprite（ScriptHost.cpp:95-109）就地建实体返回真实 id，Resolve 高位判断后直接早退零扫描，resolved 恒空；占位路径仅 tests 以个位数命令触达。故『同帧千级波次帧尖峰』在当前代码不可达，属休眠隐患而非现行缺陷。

#### C# SDK 与托管宿主

**#14 · medium · 已确认**
`Engine/Scripting/dotnet/Lemon.Entry/Exports.cs:258` — lemon_ui_events_dispatch 在引擎管线线程内联执行用户 ALC 代码（UI.Events 订阅 handler），绕过 ADR-010 D1 的域线程统一执行模型；游戏事件走 PostBatchEvents 投递域线程，两条用户代码通道线程契约不对称。
证据：Exports.cs:148-149（lemon_events_dispatch → DomainManager.PostBatchEvents，注释「域线程」）vs :258-263（lemon_ui_events_dispatch 直接调 Lemon.UI.DispatchEvents）；ScriptHost.cpp:930-934 在 DispatchEvents 内联调 uiEventsDispatchFn_；DomainManager.cs:84-88 自证矩阵：「卸载线程必须从未触碰过 ALC——UCO 主线程只发命令时 OK」｜复核：证据行号全部属实：GameUI.cs:350-354 就地执行用户 handler（TestScript.cs:61 订阅 OnUiEvent），无任何域线程包装（grep 确认唯一调用点）。调用链 Systems.cpp:1411→ScriptHost.cpp:934 确在 C++ 管线（本宿主为编辑器主循环 EditorApp.cpp:612 TickPlay→World::Step）内联执行，正是 D1 点名「主线程直调实测 pin ALC」要消灭的形态，也击穿 DomainManager.cs:86 的前提；两条通道线程契约不对称成立。仅措辞微瑕：「管线线程」当前实为主循环线程。

**#15 · medium · 已确认**
`Engine/Scripting/dotnet/Lemon.Entry/DomainManager.cs:240` — PostBatch 静默吞掉脚本装配异常：Behaviours.Attach 中 slot.Factory()（用户 LemonBehaviour 构造器）抛异常时无任何捕获与日志，而 C++ 侧已先写入 ScriptBox 槽——槽在、C# 实例无、脚本静默失效且零诊断，违反 04 §7 异常隔离。
证据：Behaviours.cs:146 `var b = slot.Factory()` 无 try/catch → DomainManager.cs:234-241 PostBatch 捕获 cmd.Error 后丢弃且无 Console.Error；对照 ScriptHost.cpp:715-719（AttachBehaviour 先 AppendSlot 写槽再调 scriptsAttachFn_）。TestScript 的两个 Behaviour 均在构造器里做实际工作，异常面真实存在｜复核：证据全部核实：同行内 Awake/OnEnable 走 SafeCall 红字+禁用，唯独构造器裸奔；PostBatch 240 行只 Wait 后丢弃，既不像 Post:81 重抛也无任何 Console.Error——注释『Batch 内部已做异常隔离』对 attach 路径不成立，与 04 §7（SafeCall 实现，Behaviours.cs:243/258-263）口径相悖。

**#16 · medium · 已确认**
`Engine/Scripting/dotnet/Lemon.Entry/Exports.cs:213` — lemon_behaviours_list 对非 ASCII 类名按 (byte)char 直出（Latin-1 化），产出非法 UTF-8——类型名经编辑器进 .scene className 持久键（04 M4.4）；与 M5 批④定性的同类缺陷，而 NativeApi.CopyUtf8 现成未用。
证据：Exports.cs:212-214 for 循环 dst[p++] = (byte)n[i]；NativeApi.cs:239-261 CopyUtf8 为现成正确实现且其注释定性了同类缺陷；lemon_behaviours_list 导出类型名表是编辑器 className 装配的正源（04:76）｜复核：属实（>0xFF 截断、0x80-0xFF Latin-1 直出均非合法 UTF-8）；类型名取自反射 t.Name 可非 ASCII，C++ 侧原样拷字节（ScriptHost.cpp:775-780），经 EditorContext.cpp:313-320 按 className 精确匹配写入持久键并落 .scene（SceneArchive.cpp:181）。对非 ASCII 类名确定性成立。

**#17 · medium · 已确认（潜伏坑，零消费者）**
`Engine/Scripting/dotnet/Lemon.SDK/Behaviours.cs:75` — ExecutionOrder 特性的 Order 数值不参与排序：RebuildOrder 只按「Order==0 / 非0」分两桶、桶内按注册序，负 Order 也不会早于默认 0 桶——与 04 §2.1 借鉴的 Prowl2D/Unity 同名特性数值排序语义不符。
证据：Behaviours.cs:72-78 双 pass 实现；:239 注释「桶按 (ExecutionOrder, 注册序) 固定排序」与实现不符；grep 全仓确认无任何使用者｜复核：`:75 (Slots[i].Order == 0) == (pass == 0)` 证实 Order 仅作 0/非0 布尔分桶，与注释及 Prowl2D 数值排序语义（SceneDispatcher.cs:261/277 用 CompareTo 全序）不符；全仓零使用者，真实潜伏坑。

**#18 · medium · 已确认**
`Engine/Scripting/dotnet/Lemon.Entry/Exports.cs:110` — lemon_dm_tick 是唯一执行用户代码却无 try/catch 的 UCO 导出：TestScript.Tick 异常经 Post 重抛逃逸导出 = coreclr abort，直接违反同文件 M4.6 明文纪律（dm_load/dm_unload/dm_reload/ui_ops_pull 均已做）；且 DomainManager.LoadScript 硬编码测试类名 asm.GetType("TestScript") 留在通用宿主内。
证据：Exports.cs:110 无 try/catch + DomainManager.cs:81 throw cmd.Error；同文件 :52-54 红字纪律；grep 确认 lemon_dm_tick 唯一消费方在 tests/script/main.cpp:1827，DomainManager.cs:108 硬编码类型名｜复核：逐条属实（评审已如实披露消费面，medium 合理）。

#### 玩法运行时系统与核心基座

**#19 · medium · 已确认**
`Engine/Systems/Systems.cpp:141-148` — TargetBoard 网格桶的占位位图对极端/NaN 坐标无防御：bbox 尺寸整型溢出或天量分配，可把坏数据放大为进程 abort。
证据：`(int)std::floor(pos/kCell)` 对 NaN 是 UB，`occ.assign((size_t)w*h/64, 0)` 在两簇实体相距 1e9×1e9 时分配超百 TB → bad_alloc 未捕获即 terminate。触发条件仅需同队列表 ≥64 实体（kMinList）加一个越界/NaN 坐标（脚本可写 Transform、手改场景档均可达）。对照 SpatialHash::Rebuild 纯排序无位图对任意键安全——与自家『坏档红字跳过不炸 Play』哲学相悖｜复核：逐行相符：对相距 1e9 的两簇坐标需分配约 122 TB；可达路径成立（SceneArchive.cpp:39-43 Vec2 读档无 isfinite 校验、GameObject.cs:124 SetComponent 可脚本写 pos、MovementSystem 钳制仅限有 Velocity 且 HasBounds 的实体），tick 链 SystemPipeline.cpp:73-83 无任何 try/catch。对照 SpatialHash 确为纯排序无位图；SceneArchive.cpp:188 自述『坏档不得抛穿』哲学，数值坏数据却直穿 abort。

**#20 · medium · 已确认**
`Engine/Core/Log.h:42-54` — LEMON_ASSERT/LEMON_CHECK 与架构基准 01 §7 的错误分级脱节：两者实现恒等且永不编译掉，release 版任何断言命中即 abort。
证据：01-Architecture-Overview.md §7 明文『LEMON_ASSERT（开发期断言，release 编译掉）/ LEMON_CHECK（运行时校验，失败走致命路径）』；Log.h:43-51 无任何 NDEBUG 分支恒 abort，:54 又把 CHECK 直接定义为 ASSERT。连带后果：Window::Create（Window.cpp:24-26、36-38）中 LEMON_ASSERT(false) 之后的 return nullptr 错误路径是死代码｜复核：全仓库 grep 确认无其他定义处，脱节属实；连带死代码成立。文档与实现必须收敛其一。

#### 音频系统

**#21 · medium · 已确认**
`Engine/Audio/BakedClip.cpp:102` — 烤制路径无时长/载荷上限且全程无异常护栏：超长源解码无界占内存，≥89 分钟立体声产物超装载侧 1GiB sane 上限后永久不可载且报错误导；后台烤制线程内 bad_alloc 未捕获会 std::terminate 崩整个编辑器。
证据：BakeAudioFile 将整源解码进单个 vector 无任何 cap（102-127），而装载侧有 kMaxBakedPayloadBytes=1GiB 拒载（:51/196-199）——2h 立体声源烤出 ~1.38GB 产物，ParseLbaHead 以「字段不一致」红字恒拒（实际是超限），且 BakeStale 见产物新鲜不重烤、每次 EnterPlay 重复失败；EditorAppScripts.cpp:351-366 的 audioBakeThread_ lambda 无 try/catch（工作线程未捕获异常 = std::terminate），主线程同步烤制链同样裸奔｜复核：证据逐条属实（含红字固定文案不含超限信息、BakeStale 纯 mtime 判定不重烤、全文件唯一 try/catch 在 61-64 行 JSON 解析）。

#### UI 系统

**#22 · medium · 已确认**
`Engine/Scripting/dotnet/Lemon.SDK/GameUI.cs:270` — SetItems 行块的 keyLen/字段名长编码为 UTF-16 字符数，引擎按字节数解码，非 ASCII key/字段名导致行块失步。
证据：GameUI.cs:270/274 `PutU8((byte)Math.Min(it.Key.Length, 255))` 写 char 数，PutBytes 写 UTF-8 字节数；引擎侧 UiSubsystem.cpp:481-494 按 keyLen 字节读串后再读 u16 fieldCount。中文单语引擎（ADR-014 D7）且 UiItem.Key 无 ASCII 约束，key 为中文即触发（3 字中文 → 声明 3、实写 9 字节）→ need() 越界 → SetItems 中止半成品行｜复核：因果链完全属实；契约 UiBridge.h:40 明文字节语义。补充：现仓内调用点（demo/ui-test、Templates/vs-survivor、TestScript）key 恰为 ASCII、中文仅在值（值长走 u16 回填真实字节），故现有 smoke 未暴露——但这是确定性编码缺陷而非理论风险。

**#23 · medium · 已确认**
`Engine/Ui/UiSubsystem.cpp:185` — Init 以 macOS 系统字体链为成败门，而登记为『主字体』的引擎自带 Noto 只在 Init 成功后由编辑器注册——非 macOS 平台整个游戏 UI 层禁用，主/兜底次序与登记意图倒置。
证据：UiSubsystem.cpp:619-629 链全败即 return false；编辑器在 Init 成功分支内才 LoadFontFace(Noto)（EditorApp.cpp:163→181），失败分支 gameUi_.reset()（:209）。THIRD_PARTY.md:20 登记 Noto 为『RmlUi 主/fallback 字体，系统字体链降级兜底』——代码次序正好相反，Windows/Linux（07 移植矩阵目标平台）上仓库自带的正字永不生效｜复核：属实：kFontChain 三条均为 /System/Library/Fonts/ macOS 路径，错误信息自认「本会话 UI 层不可用」；非 macOS 平台系统链必败→UI 层整体禁用、自带 Noto 永不装载，与登记意图及 UiSubsystem.cpp:179-180「正式版不依赖系统字体」倒置。唯一未运行验证点：Rml::LoadFontFace 对缺失文件返回 false 属 API 行为推断。

#### 编辑器应用壳

**#24 · medium · 已确认（与 #1 叠加成必崩链）**
`Editor/App/ImGuiBackend.cpp:159` — ImGuiBackend 未注册设备重建回调，设备丢失重建后 ImGui_ImplVulkan 内部资源（descriptor pool/字体纹理/管线）全部绑定旧 VkDevice 句柄，后续帧使用悬空句柄。
证据：RHI.cpp:761-784 HandleDeviceLost 销毁并重建 VkDevice 后仅执行 recreateCallbacks；全仓 5 个 GPU 资源持有者均注册（EditorAppScripts.cpp:114、ViewportRenderer.cpp:352、UiSubsystem.cpp:634、SpriteBatcher.cpp:31、RmlUiBackend.cpp:680），唯 ImGuiBackend 不在链上；EditorApp.cpp:761 deviceLost 路径仅 SkipFrame 不重初始化 ImGui 后端｜复核：全仓 grep 证实 AddRecreateCallback 恰好 5 处，ImGuiBackend 全文无注册；vendored imgui 1.92.9b 的 CreateDeviceObjects（imgui_impl_vulkan.cpp:1159）在该句柄上创建 descriptor pool/管线/字体纹理/V-I 缓冲，设备重建后全为悬空句柄；且设备丢失有真实触发点（RHI.cpp:1209/1271/1289 及 SimulateDeviceLoss 验收钩子）。

**#25 · medium · 已确认**
`Editor/App/ImGuiBackend.cpp:106` — 高 DPI 下 ImGui style 尺寸双重缩放：ApplyTheme(displayScale) 内部已把全部尺寸乘 k，ApplyScale 又追加 ScaleAllSizes(scale) 再乘一次，密度 2.0（Retina，开发期主平台）时所有间距/圆角=设计值×4。
证据：Theme.cpp:9-22 全部 ×k；ImGuiBackend.cpp:103-108 ApplyScale 先 ApplyTheme(k) 再 ScaleAllSizes(scale)（注释『1.92：尺寸缩放』系升级时叠加）｜复核：属实：BeginFrame（:204-206）在 Retina density=2.0 时必触发，单次调用内 ×2×2=设计值×4（如 WindowPadding 8→32），非理论可能。

**#26 · medium · 已确认**
`Editor/EditorContext.cpp:1127` — 结构轨 Undo 对每次结构操作做两次全场景 JSON 序列化（调用方 before 快照 + PushStructuralUndo 内部 after 快照），交互延迟随实体数线性恶化，且偏离设计的命令式结构轨。
证据：PushStructuralUndo:1127 无条件 `SceneArchive::Save(ActiveScene())`；全仓约 20 处调用点均为「before 快照→操作→Push」模式；05-Editor.md §4 Undo 行设计为『结构操作 = action 命令（带 undo/redo lambda）』，全场景快照仅『超大结构操作退化为』的路径，现成了唯一路径（与 #37 同一问题的两面）｜复核：证据全部属实：SceneArchive::Save 成本随实体数线性增长，性能问题真实成立；设计偏离属实（05-Editor.md:123、ADR-009:41、M4.md:249 更要求显式逆操作数据），代码无任何命令式分流，且无后续 ADR 修订此设计。

**#27 · medium · 已确认**
`Editor/App/EditorApp.cpp:1080` — Play 中游戏相机跟随每帧对整个实体池线性扫描查 Camera/Player/ScriptBox 三个目标，找到后不早退（Scene::Each 无 break）、跨帧无缓存，目标场景（bench-survivor 1 万怪）下每帧 1 万迭代。
证据：UpdateGameCameraFollow:1080-1088 每帧全池扫描；Play 分支每帧调用（:639），无目标缓存机制（grep 零命中）；对照 05 §9 编辑器自身开销纪律与 08 §3 的 ≥45fps 判据场景｜复核：证据属实（Each 为遍历整个 entity 池、fn 返回 void 无早退机制；DevLog 记录 alive=10003、同模式编辑器层每帧开销曾致 28fps）。一次要不精确：「×3 组件查询」是上界——Transform2D+Meta 对每实体无条件执行且三个目标齐后仍不停止遍历，核心 O(N)/帧、无早退、无缓存完全成立。

#### 编辑器冒烟与自动化框架

**#28 · medium · 已确认**
`Editor/App/EditorAppSmokeTpl.cpp:763-765` — 裁决函数 SmokeTplVerdict 内调用 OpenProjectPipeline 切换到第二个临时项目，重量级副作用泄漏到其后的同进程裁决与收尾。
证据：OpenProjectPipeline 停双 watcher、复位图集注册表、UnloadAllDocuments、dotnet 编译第二工程并重建脚本宿主。--smoke-template 隐含 --smoke（EditorEntry.cpp:86），故 EditorApp.cpp:972 裁决后 FinalVerdict、overlay 像素断言、SmokeUirmlVerdict、收尾全部在『项目已换走』的会话上执行——overlay 仍绿依赖两个未声明前提；EditorEntry flag 非互斥，手动组合 --smoke-template --smoke-uirml 时 SmokeUirmlVerdict 因文档已全卸必 FAIL。verdict 应为只读聚合｜复核：证据逐条属实（含 overlay 读设备级 capture 暂存 + 帧循环已停两个隐含前提；手动组合时 hasDoc=false 必 FAIL）。小瑕疵：管线函数实际延至 156 行（dotnet 编译在 128-133），不影响实质。

**#29 · medium · 已确认**
`Editor/App/EditorAppSmokeTpl.cpp:393-413` — 四段近逐字重复的代码：催命贴脸、压血停火各两份，bench survivor/scene 打印块两份，改口径需多点同步。
证据：催命贴脸 393-413 与 596-616 逐字同型；压血停火 418-427 与 492-502 lambda 体逐字相同；EditorAppBench.cpp:212-250 与 277-314 打印块约 39 行仅前缀不同（261 行注释自认同构）。bench 数字是 09 §6.10 台账与性能回退判定的数据源，两份打印漂移会让 survivor/scene 口径分叉｜复核：三组重复均属实（diff 实测各 39 行仅 14 行不同且全为前缀及变量名）。

#### 编辑器资产管线

**#30 · medium · 已确认**
`Editor/Assets/ClipEdit.cpp:39` — ParseClipJson 对 legacy "loop" 字段无 is_boolean 预检直接 get<bool>()——"loop": 1（常见手写/外部工具形态）抛 nlohmann type_error.302，调用链（AnimationPanel 双击装载等四处）无 try/catch → std::terminate 编辑器崩溃，违反自身「坏档不炸编辑器」契约。
证据：同函数 fps 在 29-30 行有 is_number 预检，loop 无；调用点 AnimationPanel.cpp:90/964/1182/1557 均无 try/catch（grep 验证）；ClipEdit.h:36-37 明文契约｜复核：逐字属实且为全函数唯一无类型预检的裸 get；工程未定义 JSON_NOEXCEPTION——"loop":1 坏档将 std::terminate。现有测试（engine_tests.cpp:4437-4450）只用布尔 loop，覆盖不到此洞。

**#31 · medium · 已确认**
`Editor/Assets/AssetDatabase.cpp:641` — Rescan 对项目内全部资产做无条件的全量内容哈希 + 全部 .meta 完整 JSON 重解析，无 mtime/size 门控——Assets/ 下任一文件外部改动即触发全项目同步 IO 重读卡 UI 线程；上一轮评审 #24 给引用语料做了 mtime+size 增量缓存，资产哈希路径未同步受益。
证据：每文件无条件 HashFile（188-202 全文件 16KB 分块读）；SyncMeta 307-321 每文件重读并 Json::parse；触发链 EditorApp.cpp:487 ConsumeDirty 即同步 Rescan。同文件 546-560 引用语料已做增量复用，证明哈希路径是同型问题的漏修半边。附带 O(n²)：消失文件检测线性 std::find、GUID 冲突体检双重循环｜复核：证据全部属实（watcher 500ms 轮询监视 Assets/；511-514 注释自认『UI 线程同步卡顿』）。

**#32 · medium · 已确认**
`Editor/Assets/ProjectWizard.cpp:65` — IsValidProjectName 未拒绝 shell/JSON 元字符（"、$、;、反引号等），项目名进入两处危险拼接：BuildGameProject 的 popen 命令串（POSIX sh 双引号内 $(...)/反引号仍展开 = 命令注入）与 project.lemon 的手写 JSON。
证据：拒绝集只有 {/, \, :, 控制字符}；:275-276/288 拼双引号 popen 串；:113-115/135-137 d.name 原样内插 JSON。当前平台 darwin 下注入路径可达｜复核：证据属实且注入可达：本机实测 /bin/sh 双引号内 $(...) 与反引号均展开＝命令注入成立。小偏差：project.lemon 实际已有 nlohmann::json::parse 校验（EditorAppScripts.cpp:62，含 " 名字打开项目即红字报损坏，非无声埋雷），不影响主体结论。

**#33 · medium · 已确认**
`Editor/Assets/AssetDatabase.cpp:337` — SyncMeta 写新建 .meta 用裸 ofstream trunc 直写：非原子（崩溃留半截 JSON → 解析 discarded → 323 行重发 guid，场景 spriteGuid/clip sheet 引用全部静默断链）且写失败完全无告警——与 WriteFileAtomic 纪律不一致（SetGridSlice 写同一文件已走原子写）。
证据：:337-338 无原子替换无 good() 检查；对照同文件 SetGridSlice:845 已用 WriteFileAtomic（其引入动机注释 947-952 正是防半截 JSON）｜复核：属实；后果链真实：半截 .meta 解析 discarded → 静默重发新 guid，且 metaExists 即 return 使坏档永不修复，旧 guid 引用永久断链。

#### 编辑器面板

**#34 · medium · 已确认**
`Editor/Panels/ViewportPanels.cpp:836-842` — RtUi 三选一卡片浮窗的坐标空间错误：center 是 "gv" 子窗口局部坐标，却传给了要求屏幕坐标的 SetNextWindowPos，且 ImGuiCond_Always 每帧钉死——卡片面板落在编辑器窗口左上方向偏移处而非 GameView 画面居中。
证据：:797/836 用局部坐标算 center；:837 SetNextWindowPos(center, Always)；:802 gvMin=GetCursorScreenPos() 只用于画布上报。全仓其余浮窗均用绝对坐标。设计依据：05-Editor.md §2「GameView 居中面板」｜复核：属实且在当前代码下必然复现（局部坐标值 + ImGui 视口钳位 → 落在屏幕左上方向而非 GameView 居中）。

**#35 · medium · 已确认**
`Editor/Panels/HierarchyPanel.cpp:271-281` — 层级搜索只向下看一层：SubtreeMatches 只遍历直接子节点名、不递归，深度≥2 的匹配搜不到（其父链根节点被过滤掉，匹配实体永远不可见）。
证据：:275-279 循环内只对直接子节点调 PassFilter；:211-216 顶层过滤只用 PassFilter||SubtreeMatches。A→B→C→D 结构搜 D 名：SubtreeMatches(A) 只验 B → A 不绘制 → D 不可达｜复核：属实（PassFilter 仅查自身 Meta::tag，子节点递归无入口；firstChild/next 为直接子链语义）。

**#36 · medium · 已确认**
`Editor/Panels/AnimationPanel.cpp:444-461` — 胶片带拖拽重排在 src<dst 时差一：注释声明「src<dst 插入位回退一格」，但 insert 用未回退的 i，而 selFrame_=i-1 却按回退后的位置算——拖到相邻下一帧即表现为两帧交换，且选中高亮落在错误帧上。
证据：:444 注释 vs :455-456 insert(begin()+i) 未回退 vs :457 按回退后位置算——两者互相矛盾。推演 [F0,F1,F2,F3] 拖 F0 到 F1：erase 后 insert(1) → [F1,F0,F2,F3]｜复核：属实（src>i 分支自洽，矛盾确在 src<dst 分支）。

**#37 · medium · 已确认**
`Editor/Panels/ 内 20 处 SnapshotSceneJson 调用 + Editor/EditorContext.cpp:1125-1127` — 每个小型结构操作都走全场景 JSON 快照双序列化，偏离 ADR-009/05 §4 规定的「结构操作 = action 命令、全景快照仅超大操作退化路径」；万级实体场景下每次点击两次全场景序列化、undo 栈 100 条全景字符串（与 #26 同根，此为面板侧调用面）。
证据：调用点遍布 Hierarchy/Inspector/Viewport/AssetBrowser；PushStructuralUndo 内再 Save 一次｜复核：证据属实：Panels 内实测 20 处（比评审说的 15 处还多；HierarchyPanel.cpp:429 实为 425/430 两处，其余行号全部精确命中），UndoStack.h:22 kLimit=100。设计偏离成立且无后续 ADR 修订。

**#38 · medium · 已确认**
`Editor/Panels/AssetBrowserPanel.cpp:508-511` — .tab 表格编辑的错误提示只显示一帧就清除：「提交被拒」「写盘失败」等红字渲染当帧即 tableError_.clear()，16ms 后消失，用户实际无从得知提交为何失败（错误路径等于不可见）。
证据：:508-511 渲染后立即 clear；:553/:564 在 tryCommit 写入。对照 BuiltInPanels.h:194 注释「上次提交/解析的红字提示（成功即清）」——语义应为持久至下次操作｜复核：属实：错误写入点位于渲染点之后触发且失败路径不失效缓存，红字仅显示一帧即被清除，与持久语义及 AnimationPanel saveMsg_ 持久模式相悖。

#### 编辑器工具、交互与模板

**#39 · medium · 已确认**
`Editor/Tooling/ThumbCache.cpp:150-156` — Get() 借用 AssetGpuCache 缩略图的条目仍留在待解码队列：Tick() 后续对同一张图重复解码+上传覆盖借用纹理，且 borrowed 标记导致逐出/清场时跳过销毁——RHI 纹理与 ImGui 描述符集随目录浏览无上界泄漏。
证据：Get() 先 push 进 pending 再借用提前 return（未摘除）；Tick() 对该项执行 Load() 覆盖 out.rhi/out.tex 但不清 borrowed；Destroy() `if (!it.borrowed)` 跳过 UnregisterViewportTexture 与 DestroyTexture。跨目录长会话浏览时泄漏量不受 kCap=128 约束，且重复解码违背 157 行「零解码零上传」自述｜复核：全部证据与问题成立（FilePicker.cpp:195 每帧调 Tick；逐出与 Clear() 均泄漏新建的 VkImage+描述符集）。

**#40 · medium · 已确认**
`Editor/Interaction/ViewportRenderer.cpp:509-532` — SceneView 实体名标签只对「画出的标签数」封顶（kMaxLabels=256），遍历本身无预算：视角缩小/实体多在视口外时仍全量走完 View，每实体重复一次 ComputeWorldTransform——与 ExtractScene 同量级的二次遍历。
证据：:511 break 仅在累计已画 256 个后生效；:516 对每个过滤通过实体调 ComputeWorldTransform；同一帧 ExtractScene（:431）已算过一次世界变换，两处无共享。注释自称「预算封顶」与实际成本结构不符；十万精灵 bench 下编辑器每帧多付一遍提取级遍历｜复核：属实（视口外实体不累加 labels，视角缩小时循环全量走完；十万精灵 bench 场景下该成本真实发生）。

#### 系统级架构总审

**#41 · medium · 已确认**
`Editor/EditorContext.cpp:533` — 资产→运行时的完整装配链（DB/解码上传/四类 Play 缓存/存档 IO）住在编辑器层，引擎无 Engine/Assets、无图像解码能力；M7a 虽已计划下沉，但登记了 M7a→M8 期间编辑器写侧 DB 与引擎只读 AssetIndex 双扫描器并存的窗口——schema 任何改动需双处同步，是未来两个里程碑内「一处改动波及全局」的头号脆弱点。
证据：EditorContext.cpp:532/575/915-978 全为编辑器私有；Editor/CMakeLists.txt:34 stb 仅注入编辑器层，Engine 树 grep stb_image 零命中；M7a.md §1 事实#2 自认「资产 DB 全家住 lemon-editor-core」、批② 风险行登记「双扫描器……合并归 M8」｜复核：逐字核实（双扫描器并存窗口是计划明文登记的风险而非臆测；细微出入：stb 同时注入 lemon-editor 可执行目标，但同属编辑器层，不影响结论）。

**#42 · medium · 已确认（两处证据细节被复核修正）**
`Engine/Scripting/ScriptHost.h:39-130` — C#↔C++ 边界已从设计承诺的「批量 API + 事件队列两条通道」长成通道族：46 槽 NativeApiVtable + 结构命令 + UI ops/事件 + 生命周期表并行存在，每条各自守纪律，但 01 分册契约未随迁、无统一抽象，成为每次功能里程碑必然增重的单一耦合面。
证据：awk 实测 NativeApiVtable 46 个函数指针（与 M7a.md §1 事实#11 记账一致），注释可见六轮尾加；01 文档 §1 铁律 3 仍写「仅两条通道」｜复核：主体属实（46 槽实测一致；§1 铁律 3(:61) 确写「边界只有两条通道」）。但两处证据不实：①§8（:205-215）并未写「仅两条通道」，其表格已列 5 行通道，即 §8 已部分随迁；②「ScriptHost.h 改动居引擎侧首位」不成立——同窗口 UiSubsystem.cpp 改 12 次、UiSubsystem.h 11 次，均高于 ScriptHost.h 的 8 次。

**#43 · medium · 已确认**
`Engine/ECS/Scene.h:18` — EnTT 头泄漏进引擎公共头，直接违反 01 §5 三兄弟纪律条款（Vulkan/SDL/EnTT 并列禁止）；且与 Vulkan/ImGui 两规则不同，此规则既已被突破又无任何机器守卫——属本次评审新发现的同类越界。
证据：01:190 明文「Engine/ 头文件不得 #include 任何 Vulkan/SDL/EnTT 头（Pimpl + 前置声明 + 自有句柄）」；实测 Scene.h:18 `#include <entt/entt.hpp>`，:81 View(entt::exclude_t)、:127 Registry() 公开 entt 类型，头注释以「业务代码不接触 entt::*」自我豁免；Engine/CMakeLists.txt:52 EnTT 走 PUBLIC 链接；对照 Vulkan 的 D10 护栏（CMakeLists:67-83）与 ImGui 的 tests/imgui_isolation.cmake 均为机器断言｜复核：属实且当前成立（唯 EnTT 走 PUBLIC 链接，全仓 cmake/tests 无任何 EnTT 泄漏守卫）。

**#44 · medium · 已确认**
`Editor/App/EditorApp.h:1` — 编辑器壳承担了本应属运行时宿主的全部帧编排（固定步长累加器、插值 alpha、输入语义位映射、相机跟随、音频 Tick），引擎层没有任何循环/宿主抽象；且自动化链全部绕过真人走的墙钟累加器路径，真实 pacing 代码零机器覆盖。
证据：EditorApp.h 实测 472 行；git log 近 60 提交 EditorApp.cpp 被改 34 次居全仓首位；EditorApp.cpp:591-595 playPaced 条件把 smoke/bench/final/--frames 全部排除出累加器路径；WASD/attack/confirm/pause 位映射硬编码于 :557-577；Engine/Core 仅 FileOps/JobSystem/Log 三实现 TU，GameEntry 全仓零出现｜复核：关键证据逐项核实（计数微差：EditorLaunch 实为 23 字段、挂点方法实数 34，不动摇主体结论；M7a.md:104 已将相机 follow/GameEntry 缺位列为待搬运项）。

### 3.3 低（#45–#103，未复核——按复核预算设计，处理前建议先按证据自查）

#### 渲染内核

**#45 · low · 未复核** `Engine/Renderer/Renderable.cpp:180` — sortKey 只装 seq 低 24 位且桶内用非稳定 std::sort，创建计数超过 16.7M 后回绕带内同 (layer,hash,order) 实体的绘制顺序未定义且帧间可变（alpha 混合下闪跳）。证据：`((uint64_t)e.seq & 0xFFFFFFull)` 打包进 sortKey 末 24 位，nextSeq_ 单调递增不重置；:247 桶内补排用 std::sort。目标品类含增量/挂机长跑，弹幕高频 Create 累积可达回绕量级。

**#46 · low · 未复核** `Engine/Renderer/RmlUiBackend.cpp:674` — Init 存在失败路径（RT 格式不支持）但返回 void，失败时静默 impl_.reset()，调用方只能拿到 RenderInterfacePtr()=nullptr 直塞 Rml::SetRenderInterface；且无重复调用幂等防御（与 SpriteBatcher::Init 的 D3 幂等修复不对称）。证据：:673-678 仅 LEMON_ERROR + reset 后 return；消费端 UiSubsystem.cpp:605 无空值检查；:669-683 直接 make_unique 覆盖，旧 Impl 泄漏且旧设备丢失回调残留。

**#47 · low · 未复核** `Engine/Renderer/BitmapFont.cpp:93` — Init 注释承诺『返回 false = 槽位已占用』，实现恒返回 true，槽位冲突实际走 AtlasRegistry 内 LEMON_ASSERT 直接中止。证据：BitmapFont.h:25 注释 vs cpp:65-93 无任何 return false 路径，槽位重复由 Atlas.cpp:37 的 LEMON_ASSERT 拦截（Release 下无防护）。

#### ECS 内核、组件与序列化

**#48 · low · 未复核** `Engine/Serialization/SceneArchive.cpp:391` — Load 的坏档静默路径：对 entities[] 每条先 scene.Create() 再交 ReadEntity，非 object 条目直接 return——坏档条目留下无组件空实体且无告警；EntityRef 格式非法时 ReadField 返回 false 被忽略，引用静默变 null。证据：:391-397 先建后验；:192 非 object 即 return 无 warn；:49-53 vs :204-211 字段循环不检查返回值（类型错有 warn、引用格式错反而无）——与同文件『坏档必须可见』口径不一致。

**#49 · low · 未复核** `Engine/Serialization/SceneArchive.cpp:187` — ReadEntity 注释宣称『清掉已有可重建组件后按档重建』，实现没有任何清理直接 emplaceFn——entt 对已存在组件的 emplace 是断言路径，未来对非全新实体复用即断言/UB（潜伏契约，现行调用点均传新建实体）。证据：:187 注释 vs :202 实现；Scene::Emplace 直接转发 registry_.emplace。

**#50 · low · 未复核** `Engine/ECS/Scene.h:80` — View<>/Pool<>/FromEntt 注释限定为『引擎系统内部使用』，但编辑器代码（含 EditorApp.cpp 生产分支与 Smoke* 系列）直接调用，entt 类型经 auto 推导渗入编辑域。证据：Scene.h:79-83/:91/:112-114 注释 vs EditorApp.cpp:695-701、EditorAppSmoke.cpp:528/1785 等；03 §1 封装原则明言目的是『保留将来替换/裁剪 EnTT 的自由』。

#### C# 脚本桥（C++ 侧）

**#51 · low · 未复核** `Engine/Scripting/ScriptHost.h:161` — SceneOpC.compId 为 uint8_t，但 op4/op5 复用它携带 Behaviours typeId——注册脚本类型超过 255 时静默回绕，挂错行为类。证据：ScriptHost.cpp:874/880 以 `(int)op.compId` 直接当 typeId 用；BehaviourTypeNames 无数量上限。

**#52 · low · 未复核** `Engine/Scripting/ScriptHost.cpp:900` — NotifyPendingDestroys 在迭代 entt View 的循环体内运行任意托管 OnDestroy 回调，迭代安全性仅靠『当前无导出会同步改动这两个池』侥幸成立，且无注释固定该不变量。证据：循环体内调 scriptsDestroyFn_；未来 vtable 加一个同步 native Destroy/Attach 导出即迭代器失效 UB。

**#53 · low · 未复核** `Engine/Scripting/CoreCLRHost.cpp:127` — Load 失败路径资源处理不完整：重试即泄漏 Fxr+dlopen 句柄；getDelegate 失败时 hostfxr ctx 未 close；错误回调注册是 per-thread 只覆盖装载线程。证据：`fxr_ = new Fxr{}` 无条件赋值，IsLoaded() 只看 loadAssembly_；hostfxr_set_error_writer 按 vendored 头注记是线程本地注册。

**#54 · low · 未复核** `Engine/Scripting/ScriptHost.cpp:483` — 两个死必需导出：dmUnload_ 与 batchTickFn_ 在 Initialize 必需导出闸中强制要求，但全仓无任何调用点。证据：grep 验证二者仅出现在赋值与必需检查 :518——实际 tick 走 scriptsTickFn_，卸载由 dmReload_ 全包；未来 Entry 删掉这两导出会让 Initialize 无谓失败。

**#55 · low · 未复核** `Engine/Scripting/ScriptHost.h:290` — scriptsNeedTick_ 是只读不写的死标志：『无脚本实例则跳过托管往返』的早退永远不可达，空场景照付每帧托管 tick。证据：全仓只有初始化 = true 与两处读，无任何写 false；空场景每帧仍做 ~66µs 量级托管往返（04 §2.1 实测口径）。

**#56 · low · 未复核** `Engine/Scripting/ScriptHost.cpp:95` — NativeSpawnSprite 就地建实体，偏离设计基准『结构变更全走命令缓冲、帧首生效』的成文语义，且行内注释声称两路一致并不完全成立，文档未更新。证据：04:212 明文命令缓冲语义；NativeSpawnSprite 直接 Create+Emplace，spawnSprite 与 SceneOps/InstantiatePrefab 两条生成路径的可见性差一帧。

#### C# SDK 与托管宿主

**#57 · low · 未复核** `Engine/Scripting/dotnet/Lemon.Entry/DomainManager.cs:96` — LoadScript 幂等分支忽略路径参数：已加载时对不同 assemblyPath 也返回成功且不装载新程序集，调用方无从分辨「装了请求的域」与「维持旧域」。证据：幂等分支只查 s_alc != null 即 return；ScriptHost.cpp:523-529 对返回 1 一律置 userLoaded_。

**#58 · low · 未复核** `Engine/Scripting/dotnet/Lemon.SDK/Behaviours.cs:100` — 脚本类型标识用裸类型名（t.Name 不含命名空间）：跨命名空间同名类都注册时 TypeIdOf 按名返回先注册者，第二个类的 AddComponent<T>() 会装配到第一个类的实例。证据：:100/:121-126/GameObject.cs:42。

**#59 · low · 未复核** `Engine/Scripting/dotnet/Lemon.SDK/Anim.cs:33` — GUID hex 解析口径不一致：Anim.ClipId 用 ulong.Parse（坏 hex 直接抛异常），同文件 GuidOf/ResolveClip 与 Audio.Play 全部 TryParse 静默降级。证据：Anim.cs:32-33 vs :66-69/:171-174 与 Audio.cs:32-38（注释还自称 SDK 同款 TryParse 口径）。

**#60 · low · 未复核** `Engine/Scripting/dotnet/Lemon.SDK/GameUI.cs:293` — PutBytes 截断可落在 UTF-8 多字节序列中间，产出非法 UTF-8——与 NativeApi.CopyUtf8 在 M5 批④后修④专门修复的缺陷同类；仅 >255B 字段名 / >64K 值串触发。证据：:289-295 用 Math.Min 截断无多字节边界回退；对照 NativeApi.cs:253-258 修复注释。

#### 玩法运行时系统与核心基座

**#61 · low · 未复核** `Engine/Systems/Systems.cpp:351-366` — timeScale=0 冻结不彻底：冻结瞬间已到期的刷怪冷却仍执行出生并消耗 RNG，违背注释自述的冻结不变量。证据：Systems.cpp:267 注释明言『timeScale=0 冻结波次、RNG 不消耗』；但 dt=0 时 FixedTick 照跑，DirectorSystem 对到期冷却（≤0）继续走出生分支；SpawnSystem 同款（burst 全额泄漏）。

**#62 · low · 未复核** `Engine/Physics2D/SpatialHash.h:64` — Raycast 头文件契约宣称『DDA 走格』，实现是采样粗扫 + 启发式早退，且 probeRadius 大于步长（cell/2）时理论上可漏掉最近命中。证据：实现（SpatialHash.cpp:160-202）自述『粗扫…DDA 优化待 profile 数据』；当前零调用方（grep 仅 tests 命中），风险休眠但 API 文档已失真。

**#63 · low · 未复核** `Engine/Core/Pool.h:1-8` — 通用 Pool<T> 模板全工程零生产用户，且文档声称的 DestroyCommit 阶段 FlushReleases 集成点并不存在。证据：grep 仅 tests 一处 include；DestroyCommitSystem::Tick 及全 Engine 无一处 FlushReleases 调用；另 Release 无双重归还防护。建议接线或删除。

**#64 · low · 未复核** `Engine/Systems/Systems.cpp:507` — Chase 并行段拿到目标实体后回源 registry 直读 Transform，弃用 TargetBoard 已快照的 pos，违背自家并行契约条款并多付热路径随机访问。证据：TargetBoard::TargetEntry 本就携带 pos 快照（Systems.h:26-29 注释自述）；03 §4 条款 2 要求跨实体读走帧内快照；Shooter 串行段同款（:558）。

**#65 · low · 未复核** `Engine/Systems/Systems.cpp:1052-1053` — Animator 回绕 while 循环对 time=+inf 无防御，档面/脚本可写字段可造成整帧死循环挂起。证据：三处同构 `while (an.time >= period) an.time -= period;`；speed 是序列化+C# 热调参字段，JSON 大数经 float 即 inf（NaN 恰好免疫）。对照 ClipTable::Add 对 fps 的防御，此处缺一道钳制。

**#66 · low · 未复核** `Engine/Physics2D/SpatialHash.h:27` — 不必要地整体 include ECS/Scene.h，把 entt.hpp 传递扩散到 Physics2D 全部消费者，扩大 01 §5 头文件纪律的缺口（与 #43 同族）。证据：头内声明只用到 Scene&（可前置声明）；改前置声明 + .cpp 内 include 即可收口。

**#67 · low · 未复核** `Engine/Systems/Systems.h:1-2` — 文件头『17 系统（13 真实现 + 4 里程碑占位）』计数失真：实际管线 20 个系统；同文件网格 cell 注释亦自相矛盾（『cell 64px』vs kCell=32）。证据：InstallDefaultSystems 实际 20 次 AddSystem（AnimGraph/Tween/Audio 尾插后未回头改头注）。

#### 音频系统

**#68 · low · 未复核** `Engine/Audio/BakedClip.cpp:138` — loop 秒值 float→uint32 无界转换是未定义行为：手改 .meta 给超大 loop 值可触发。证据：转换在 std::min 之前；meta 解析侧对 loop 只查 x>=0 && y>=x 无上限——同文件对载荷回绕已有完整防线（threat model 含手改文件），此处是同类防御缺口。

**#69 · low · 未复核** `Engine/Audio/AudioEngine.cpp:388` — 流式声部在设备回调内逐帧做 4 字节环读（每次 Read = 2 次 acquire + 1 次 release + 2 次 memcpy），未按块批化。证据：MixVoices 流式分支每帧 uint8_t fb[4] 环 Read；约 4.8 万次 Read/秒（量级约 0.1% 单核，非当前瓶颈），但是音频热路径上最现成的批化点。

**#70 · low · 未复核** `Engine/Audio/AudioEngine.cpp:74` — StreamFeed::consumed 死字段：声明后无任何读写。证据：grep 全 Engine/Audio 仅此一处出现；判据确实走了 cursor，观测无人消费。

**#71 · low · 未复核** `Engine/Scripting/dotnet/Lemon.SDK/Audio.cs:109` — C# Audio.Paused 属性只有 setter 无 getter，暂停态查询不可达，与 MasterVolume 的 D6 get/set 对称拍板不一致。证据：C++ 侧观测已存在（AudioChannel::pausedStaged()）但未桥 vtable；游戏侧只能自行记账暂停态。

#### UI 系统

**#72 · low · 未复核** `Engine/Ui/UiSubsystem.cpp:400` — 容器状态建立后，后续 SetItems 传入不同的 templateName 被静默忽略、沿用首个模板克隆——违背 ADR-014 M2 响亮失败纪律。证据：直接返回已建容器，templateName 参与容器键但不参与校验；同函数其余三处缺失均 ContractFail。

**#73 · low · 未复核** `Engine/Ui/UiSubsystem.cpp:1052` — SetDpReferenceHeight 从非零改回 0 后 dpRatio 不复位为 1，ctx 保持旧缩放比率。证据：门控无 else 复位分支；契约『0 = 不缩放（默认，px=dp）』。当前唯一调用方恒设 720，潜伏契约缺口。

**#74 · low · 未复核** `Engine/Ui/UiSubsystem.cpp:231` — shownDuringPlay 是只写不读的死状态位——清场判据升级为 origin 后该位遗留，注释仍称『EnterPlay 归位判据』与实现漂移。证据：grep 全文件仅见赋值无读取；字段注释未随 2026-09-29 升级同步。

**#75 · low · 未复核** `Engine/Ui/SdlTextInputHandler.cpp:24` — IME 组合态跨焦点切换未复位：OnDeactivate/OnDestroy 只清 ctx_ 不清 start_/end_，换控件后首个编辑事件按旧区间 SetText 可能改写新控件文本。证据：HandleEdit 在 composing 时直接用旧区间写新控件。低概率边界（组合中切焦点）。

#### 编辑器应用壳

**#76 · low · 未复核** `Editor/App/EditorApp.cpp:584` — audio_.Tick 仅在 Play 分支驱动：Edit 态试听的流式声部停止/播完后 stream->dead 永不置位，静音降级模式下逻辑游标冻结致同 clip 重触发节流窗永不过期。证据：Tick 是 done 声部回收与静音模式 AdvanceLocked 推进 mixFrames_ 的唯一驱动；else 分支无对应调用。

**#77 · low · 未复核** `Editor/EditorContext.cpp:229` — OpenSceneRecovery 整体替换场景内容但不清 Undo 栈——与 OpenScene:87 / NewScene:68 已修的漏配同型。证据：函数体内无 undo_.Clear()；当前调用图仅启动恢复模态与终验（新进程 undo 必空故暂不可触发），但缺同款防御。

**#78 · low · 未复核** `Editor/App/EditorAppChrome.cpp:350` — 命名布局的保存/删除把用户自由文本输入的名字直接拼路径，输入含 ../ 即越出布局目录写/删任意 .ini 文件——与 DiscardAutosave 已建立的『外部输入不得成为删除原语』防御纪律不一致。证据：保存模态 InputText 自由文本 → 直接拼接 ofstream 写入；删除路径同样无净化；对照 EditorContext.cpp:240-243 显式前缀校验。

**#79 · low · 未复核** `Editor/EditorContext.cpp:511` — Prefab 实例化成功日志打印 s.AliveCount()（场景总实体数）冒充该 prefab 的实体数，误导排障。证据：AliveCount 是整个 ActiveScene 的存活计数。

#### 编辑器冒烟与自动化框架

**#80 · low · 未复核** `Editor/App/EditorAppSmoke.cpp:794-815` — SubtreeSizeOf 固定 64 深度栈对超深子树静默截断，计数偏小无任何断言或日志。证据：:810 溢出即丢子树，返回值直接喂 C8 断言；另函数非 static/匿名却无头文件声明，具外部链接。

**#81 · low · 未复核** `Editor/App/EditorAppSmoke.cpp:66-99` — 夹具 GUID 双源：EditorAppSmoke.h 已定义 kAnim*Guid 常量族，夹具 JSON 内又硬编码同值 hex 字符串 21 处。证据：grep -c 5bd31a7c = 21；改任一 guid 需人工同步多处，漂移即断言面错位。

**#82 · low · 未复核** `Editor/App/EditorAppSmokeUirml.cpp:93-94` — 程序化进 Play 双路径：TryEnterPlay 封装与手动拼装并存且步骤面已漂移。证据：手动路径漏掉 TryEnterPlay 内的 paused_ 复位、MountPlayAudio、WirePlayAudioBackend 三步；bench-survivor 播种同款手动拼装；TryEnterPlay 日后新增任何前置这两条路径都会静默漏掉。

**#83 · low · 未复核** `Editor/App/EditorAppSmokeTpl.cpp:617-627` — 诊断快照的 gems/mobs 注释与打印标『峰值(peak)』，实现却是每 60 帧累加和。证据：只加不清、不取 max——数值随局时长大致线性增长，与『峰值』语义不符。

**#84 · low · 未复核** `Editor/App/EditorAppSmoke.cpp:115` — SeedBenchSurvivorScene 硬编码 spriteIdBase=100，与 OpenProjectPipeline 的动态基号（SpriteCount()+1，当前恰为 100）隐性耦合。证据：程序化图集页数量一变，bench 临时项目的 spriteId 记账基号即静默错位。

**#85 · low · 未复核** `Editor/App/EditorAppSmokeUirml.cpp:7-8` — 机械外迁残留：同头文件连续重复 include 两行、SeedSmokeScene 内无意义 (void)0; 死语句；另两文件对同一字符串各定义一个命名不一致的 static 常量。

#### 编辑器资产管线

**#86 · low · 未复核** `Editor/Assets/AssetDatabase.cpp:800` — Rename 只检查源文件改名的错误码，.meta 随行改名的失败被静默忽略——meta 留旧路径时新路径 SyncMeta 视为无档、guid 重发，场景引用断链且无直接红字。证据：两次 fs::rename 各自 error_code，只检查 ec1。

**#87 · low · 未复核** `Editor/Assets/AssetGpuCache.h:32` — Evict 的头注释称「纹理/缩略图释放」，实现全程保留（幽灵页到重启），且仍用已退役的「墓碑」措辞。证据：实现只 LEMON_WARN 幽灵页警告；AssetDatabase.h 已按 2026-10-01 墓碑退役修订而此文件未同步。

**#88 · low · 未复核** `Editor/Assets/AssetDatabase.cpp:155` — HexToGuid 不校验解析后恰好消耗 16 个字符——短 hex 串被接受为截断值而非报错。证据：循环条件 `*p && p - hex < 16` 后无长度收口；消费点 guid==0 判非法——短串非零即漏过。

**#89 · low · 未复核** `Editor/Assets/AssetDatabase.cpp:85` — FindBySpriteId/SpriteIdRegistered 不过滤 missing 条目，与 FindClipByLowId/EntriesInDir 口径不一致——Remove() 后到下轮 Rescan 前的同帧窗口内已删资产仍报「已登记」。证据：均无 e.missing 守卫，对照同文件 :106/:116 有。

**#90 · low · 未复核** `Editor/Assets/ControllerEdit.cpp:44` — AppendFloat 非整值恒输出一位小数：controller 过渡条件阈值 0.15 保存后变 0.1/0.2——游戏逻辑值经保存-重载静默漂移。证据：`v == (float)(long long)v ? %lld : %.1f`；一位小数口径仅文档化于 fps 场景。

#### 编辑器面板

**#91 · low · 未复核** `Editor/Panels/ProfilerPanel.cpp:46-49` — BeginTable 返回 false 的早退 return 跳过了 ImGui::End()（窗口栈失衡）：当前 flags 下不可达，但系统表一旦加滚动 flag 即触发，属 ImGui 契约违例的地雷。证据：核查 vendored imgui_tables.cpp:348-355 早退条件。

**#92 · low · 未复核** `Editor/Panels/ProfilerPanel.cpp:95` — 「GPU 列」复选框是死控件：showGpu_ 只被写入从未被读取，勾选无任何效果，误导用户。证据：grep 全 Panels 目录仅声明与写入两处；GPU 时间在表格上方无条件显示。

**#93 · low · 未复核** `Editor/Panels/ViewportPanels.cpp:324-329` — 拖实体入 SceneView 视口摘根不进 Undo：同一「摘根」操作在 HierarchyPanel 走完整结构轨，视口拖放路径漏推——同一操作两种入口 Ctrl+Z 行为不一致。证据：仅 SetParent+dirty；对照 HierarchyPanel.cpp:248-258 同 payload 完整走 undo。

**#94 · low · 未复核** `Editor/Panels/InspectorPanel.cpp:779-801` — ScriptBox 槽位换绑（combo 换脚本类型）无任何 Undo 覆盖：ScriptBox 不入 ComponentRegistry，属性轨够不到；相邻的移除/挂载操作都有结构轨——同类操作覆盖不一致。证据：combo Selectable 直接调 SetSlotScript（仅置 dirty）；对照 :814-818/:857-859 均有 undo。

#### 编辑器工具、交互与模板

**#95 · low · 未复核** `Editor/Interaction/ViewportRenderer.cpp:309-326` — 换项目路径 Registry().Reset()+Build() 直接覆盖 page_/iconPage_/字体页/两个采样器句柄，旧活体 GPU 资源从未 Destroy——每次项目切换泄漏一组纹理+采样器。证据：Build() 全函数无销毁旧句柄的调用；AtlasRegistry::Reset() 仅 clear 登记表；注释「释放由 ProceduralAtlas 重建覆盖」与事实不符——覆盖句柄≠释放资源。

**#96 · low · 未复核** `Editor/Templates/VsTemplateGen.cpp:1734-1739` — yami 素材拷贝循环 fs::copy 后不检查 ec——磁盘满/权限失败时仍继续生成并最终报 OK；与同文件已修复的音频拷贝失败即断（上轮 #26）同类问题未修全。证据：循环结束后无任何 if (ec) 判断；对照 :87-94 音频路径已加红字+return false。

**#97 · low · 未复核** `Editor/Tooling/EditorLog.h:29-33` — 日志环用 vector + 每条 erase(begin())：饱和后每条日志搬移 2047 个 EditorLogLine（~80KB memmove），且发生在引擎日志锁内的 sink 路径。证据：注释自述「sink 锁内回调……只做拷贝入环，须快」；环形下标或 deque 可零成本消除。

**#98 · low · 未复核** `Editor/Interaction/ViewportRenderer.cpp:386-394` — RT 尺寸变化即 WaitIdle+销毁重建：拖拽 dock 分隔条期间逐帧命中，交互全程每帧一次全 GPU 停等 + 纹理销毁重建 + 描述符集重注册。证据：WaitIdle 位于尺寸不匹配分支内；调用方每帧以上报内容尺寸调用 EnsureRenderTarget；注释「尺寸变化低频」不成立于拖拽场景。

**#99 · low · 未复核** `Editor/Tooling/ThumbCache.cpp:60-67` — Destroy 对每个非借用项各调一次 device->WaitIdle()：Clear() 最多连续 128 次全队列停等；LRU 饱和后每浏览一张新图也触发一次。证据：WaitIdle 在 Destroy() 内逐项调用；注释自称「逐出低频」但 kCap 饱和后逐出即每请求一件。

**#100 · low · 未复核** `Editor/Templates/VsTemplateGen.cpp:649` — 头文件宣称模板 GUID 常量「上移单源、不再散落」，但生成的 C# 脚本内以字符串字面量重复同一批 GUID（15+ 处）——改 C++ 常量重新生成后 C# 仍指旧值，模板静默断引用。证据：VsTemplateGen.cpp:649/740-743/1111-1120 硬编码 hex 字面量与头文件常量平行维护；生成器已有 GuidToHex 插值先例而 C# 侧未用。

#### 系统级架构总审

**#101 · low · 未复核** `docs/EngineDesign/01-Architecture-Overview.md:148` — §5 仓库目录树与现状明显漂移且未按其自称的「与骨架冲突时先改本文」流程修订，作为「动工前必读」的地图会误导新人。证据：目录树列 CSharp/、External/、Engine/{Math,Assets,Input,Navigation}/；实况为 Scripting/dotnet/{Lemon.Entry,Lemon.SDK}、Core/Math.h、ECS/Input.h（头注释自注的 SDL 接驳未兑现）、Engine/Assets 待 M7a 批②；M7a 批⑧ 收官项里才有「01 分叉消除」。

**#102 · low · 未复核** `Engine/Scripting/ScriptHost.h:141` — 2026-09-30 路线图重排（独立运行时提前至 M7a）后，多处引擎代码注释仍指向旧口径 M8，里程碑指涉无随迁机制。证据：ScriptHost.h:141-142、UiSubsystem.h:41、World.h:30 均写 M8；对照 08 文首重排注记与 M7a.md §1 事实#7 仍按旧口径。

**#103 · low · 未复核** `Engine/ECS/World.h:35` — 游戏 UI 三通道并存：RtUi（8 槽 ImGui HUD）与 RtUiCards（三选一）仍是在役通道，与 RmlUi UiSubsystem 构成两套游戏 UI 语义（状态、输入让出、渲染路径互不相通），M8 前持续双线维护。证据：GameView 仍逐帧渲染两旧通道；输入让出门仅判 RmlUi 侧，ImGui 呈现的 Cards 无对应让出语义。

## 4. 处置建议（按评审总结论整理，优先级从高到低）

1. M7a 计划文档补「ECS→渲染提取」搬运项（#6）——修文档即可，成本最低、防第三套实现；
2. 设备丢失恢复链：bridgeCache 跨重建失效（#1）+ ImGuiBackend 注册重建回调（#24）+ 编辑器路径纳入设备丢失验收；
3. 并行销毁主线程稳定归并（#2，03 §4 契约），并以多 worker 同帧销毁场景验证状态哈希稳定；
4. 用户可直接触发的正确性：UI 多事件丢失（#3）、Clip/AnimSet JSON 转义+roundtrip（#5）、Play 中编辑操作跨世界（#4）、SetItems 字节长度（#22）；
5. 引擎侧防线补齐（#10 死实体入档、#19 NaN 防御、#30 坏档抛穿、#21 烤制护栏）——与「坏档红字跳过不炸 Play」的自家哲学对齐；
6. 契约-实现收敛批次：#11/#20/#47/#62/#87 等注释与实现相反项，趁修复轮一并清账。

## 5. 未覆盖范围

- tests/、spike/、Samples/、Templates/、demo/、cmake/ 构建脚本不在评审范围（本轮限定 Engine/ 与 Editor/）；
- editor-regression.sh 17 步回归与金回放冒烟未运行（需 GUI 会话与较长时长；2026-10-01 的 17/17 为文档记录，非本次验证）；
- vendored 第三方代码（miniaudio.h、stb_vorbis.c、hostfxr.h、RmlUi 等）不深审，仅核对登记（由架构总审执行）；
- 性能结论基于静态代码评审，未做运行时 profiling。
