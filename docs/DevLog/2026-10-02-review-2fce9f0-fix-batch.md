# review 2026-10-02（基线 2fce9f0）修复批 ①——高优全清 + 处置建议点名项

> 范围：[评审报告](../Reports/2026-10-02-code-review-2fce9f0.md) §4 处置建议 1–6 组的前 17 项：
> **high 全部 6 条（#1–#6）+ medium 6 条（#24/#22/#10/#19/#30/#21）+ 契约收敛批 5 条（#11/#20/#47/#62/#87）**
> （顺手带低severity #60 值串截断边界）。引用记法 = 报告「review 2026-10-02 #N」。
> 未入本批的 medium/low（#7–#9/#12–#18/#23/#25–#29/#31–#44/#45 以下）待后续批。

## 修复明细

### 设备丢失恢复链（#1 + #24，处置建议 2）

- **#1 bridgeCache 跨重建失效**：`RHI.cpp` HandleDeviceLost 在销毁旧 VkDevice/VmaAllocator 后
  重置 `bridgeFilled`——下次 `GetInternalBridge()` 重抓新句柄。修复前 RmlUi 后端
  RecreateAfterLoss→CreateAll 拿到的仍是已销毁句柄，其上 vkCreateDescriptorSetLayout/
  vkCreateGraphicsPipelines/vmaCreateBuffer = UB。
- **#24 ImGuiBackend 挂上重建链**：RHI 新增 **pre-destroy 回调相位**
  （`AddPreDestroyCallback/RemovePreDestroyCallback`，HandleDeviceLost 在销毁任何句柄前
  调用——设备已丢失但句柄未销毁，直接释放调用规范允许）。ImGuiBackend 注册两头：
  pre-destroy → `ImGui_ImplVulkan_Shutdown()`（合法释放后端自有 descriptor pool/
  管线/字体纹理）；post-recreate → 重新 `ImGui_ImplVulkan_Init`（InitInfo 组装抽
  `Impl::InitVulkanBackend` 复用）。注册序 = EditorApp 装配序（ui_ 先于 viewport_/
  gameUi_）→ 重建回调最先执行，后续持有者重注册视口纹理时后端已就绪。
  - **跨代描述符集防误释放**：1.92.9b `ImGui_ImplVulkan_AddTexture/RemoveTexture` 是
    无注册表旧式 API——RemoveTexture 盲目对当前池 `vkFreeDescriptorSets`。胶水层加
    `liveViewportSets` 记账：Unregister 只释放本代登记集，旧代集（已随旧池销毁）
    安全跳过（RebindProceduralIcons 等重建路径的既有 UB 窗口一并闭合）。
  - 降级守卫：重建后 Init 失败（红字）不再断言中止——BeginFrame/Render 跳过后端调用。
- 未做（处置建议 2 第三半句）：编辑器路径纳入设备丢失验收（需 GUI 会话 + SimulateDeviceLoss
  编辑器接线，登记后续；本机 rhi-smoke `--device-loss 30` recovered 复验通过）。

### 并行确定性（#2，处置建议 3）

- ProjectileLifetimeSystem 弃「worker 内直接 Destroy」（入队序 = 互斥锁获取序，跨线程
  漂移 → CommitDestroys 提交序漂移 → 池 swap_and_pop 终态/槽回收序不定，违反 03 §4
  契约第 3 条），改 **chunk 分桶收集意图**（TargetBoard chunkTeams_ 同款"只增不减"缓冲）：
  ParallelFor 返回后主线程按 chunk 序提交 = **串行迭代序**——单线程档行为/哈希零变化
  （金回放零重录），多 worker 与单线程逐位同构。
- Scene::Destroy 注释改写：锁只保证不炸不保证确定；并行段销毁必须走收集-归并。
- **回归锁 + 阴性验证**：`TestVerifyParallelDestroyDeterminism`（4096 投射物半数到期，
  16 chunks，threads=1 vs 8 的状态哈希逐位相等 + 销毁后 64 次重建的槽回收序一致）。
  临时注入旧 bug 形态（worker 内直接 Destroy）→ `FAIL: pdestroy: mt hash == st hash`
  → 还原后复绿。实证锁有效。

### 用户可直接触发的正确性（处置建议 4）

- **#3 DrainEvents 多事件丢失**：`dst[k] = events.front()` 拷 n 份 → 改 `dst[k] = events[k]`
  （复选框单击 Click+Change 双事件即触发面）。单测注入钩子不存在（事件仅由 RmlUi 监听器
  产入）、scripted 冒烟走假钩子——真实多事件链的机器覆盖待后续（修复为单行索引纠正）。
- **#5 JSON 转义 + roundtrip**：新增 `JsonEscape`（ClipEdit.h 导出；nlohmann dump 单源，
  非 ASCII 原样）用于三个手写序列化器的全部名字字段（clip name / animset 段名 /
  controller name·entry·状态名·param 名·from·to）；AnimSetToJson 段行、ControllerToJson
  过渡头弃定长缓冲（char[128]/[192] 静默截断源头消除）。**落盘前 roundtrip 预验**：
  AnimationPanel TrySave/TrySaveSet 生成后 `Parse(ToJson(x))` 对拍（name/frames、
  segments），失败拒写——坏档覆写 = 数据丢失的路径性闭合。测试：引号/反斜杠名 + 200
  字符长名 roundtrip、100 字符段名、golden 格式逐字节不变（ASCII 路径零扰动）。
- **#4 Play 中脚本/Prefab 操作跨世界**：七个函数（AttachScript/SetSlotScript/
  RemoveScriptSlot/MakePrefabFrom/Apply/Revert/BreakPrefabInstance）按 CreateEntity/
  DuplicateEntity/DestroyEntityTree 已确立模式改 `ActiveScene()`；dirty 全部改
  `!Playing()`（EditorContext.h「Play 中编辑不动编辑侧脏标记」）。附加：
  ①Play 中挂/换脚本当帧 `ResolveSlotBehaviour` 挂活实例（RefreshScriptsAfterReload
  同款，05 §4「Play 中允许编辑」兑现）；②Revert 在 SceneDestroyEntityTree 后补
  `CommitDestroys()`（同帧 Hierarchy 闪烁 + 结构轨 after 快照含待删实体 → Redo 复活
  的实录事故路径，#10 编辑器侧半边）。
- **#22 SetItems 字节长度失步**：GameUI.cs 行块 u8 长度前缀改 `PutStr8`（长度 = 实际
  UTF-8 字节数，非 char 数；中文 key 不再错位行块）；超 255B 截断退码点边界（连带修
  #60 的 PutBytes 值串同类截断）。ASCII 输出与旧版逐字节相同（script-tests TestUiSdk
  字节对拍绿）。非 ASCII key 的 C# 夹具用例待后续（需扩 UiProbe 播种面）。

### 引擎侧防线（处置建议 5）

- **#10 死实体入档**：SceneArchive::Save 过滤 DestroyQueueTag 实体；SaveEntityTree
  剪除待销毁成员（根待销毁 = 拒绝导出）；WriteEntity 恒不序列化标记（fieldCount==0
  命中才比名，热路径零 strcmp）；ReadEntity 拒读标记（历史坏档不再复活僵尸）。
  测试：销毁窗口期 Save 排除 + 读档无僵尸 + 构造含标记旧档拒读。
- **#19 TargetBoard 坏坐标**：Build 对非有限坐标剪除（NaN 的 float→int 本是 UB）、
  bbox 格数超 `kMaxCells`（1M 格 = 128KB occ，32768px 轴向跨度）弃格；Grid::Nearest
  补 occ.empty() 线性兜底 + NaN 查询点守卫。1e9×1e9 两簇原要 ~122TB 位图 =
  bad_alloc terminate，现降级线性照常工作。测试 TestTargetBoardBadCoordDefense。
- **#30 坏档抛穿**：ParseClipJson legacy "loop" 补 is_boolean 预检（"loop":1 手写档
  原裸 get<bool>() 抛 type_error = std::terminate，违背「坏档不炸编辑器」契约）。
  测试：`"loop":1` 拒入不抛。
- **#21 烤制护栏**：BakeAudioFile 解码载荷上限 = 装载侧 kMaxBakedPayloadBytes 同源
  1GiB（≥约 89 分钟立体声直白拒绝，不再无界解码后产出永久不可载的产物；上限同时
  钳住 resize）；装载侧超限报错单列（不再混进「字段不一致」误导）；后台烤制线程
  lambda 补 try/catch（工作线程未捕获异常 = std::terminate 崩整个编辑器，按件隔离
  红字后继续吃队列）。

### 契约收敛批（处置建议 6）

- **#11** Hierarchy.h SceneDestroyEntityTree 注释改与实现一致（两阶段入队；即时性
  契约是错的，需立即生效由调用方 CommitDestroys）。
- **#20** 断言语义按实现收敛（不改为编译掉：图集槽冲突等防线在 release 只有断言一道，
  编译掉即裸奔）：01 §7 加收敛修订注记；Log.h 宏注释明示恒生效；Window::Create 两处
  `LEMON_ASSERT(false)+return nullptr` 死错误路径改 `LEMON_ERROR+return nullptr` 真路径。
- **#47** BitmapFont::Init 实现头文件承诺的槽位冲突 return false（先查后建，不落纹理；
  原恒 true、冲突由 RegisterAtlas 断言中止）。
- **#62** SpatialHash Raycast 头注释按实现改写（采样粗扫非 DDA；probeRadius > 步长的
  保守边界如实登记；零调用方休眠 API）。
- **#87** AssetGpuCache::Evict 注释改与实现一致（幽灵页保留到重启；墓碑措辞随
  2026-10-01 墓碑退役修订）。

### M7a 计划补项（#6，处置建议 1）

M7a.md：现状盘点加事实 #12（ECS→渲染提取两套实现、引擎管线 Extract 空转）；批③ 搬运
清单加「ECS→渲染提取」行（ExtractScene 去 EditorContext 锚定 + SystemStage::Extract
首个真实现 + World::Step 补跑 Extract；ViewportRenderer/GameEntry 两薄壳）；批④ 装配
序列主循环点名用批③ 下沉件——防 GameEntry 写出第三套实现。

## 验证

- `cmake --build build/mac` 全目标 ✓；`ctest` **3/3**（engine-tests **34071 checks**
  （+31）/ imgui-isolation / script-tests 1771）✓。
- #2 阴性验证：注入旧 bug → `FAIL: pdestroy: mt hash == st hash` → 还原复绿。
- `rhi-smoke --frames 60 --device-loss 30`：`deviceLoss=recovered` ✓（HandleDeviceLost
  改动 + preDestroyCallbacks 空表遍历实跑）。
- `smoke-audio`（临时夹具）：`entries=1 baked=1 voices=1/0 => OK` ✓（#21 链）。
- `smoke-uirml --frames 500`：`=> OK` ✓（#3/#22 所在 UI 链；items/ev 探针本次走无脚本
  模式按设计豁免）。
- `smoke-template --frames 3200`：主链全绿（hud/saveLoad/wave/kills/cards/death/flow/
  aud 全 YES、saves OK、second-project ids OK）；末尾通用 overlay 像素断言 FAIL——
  **stash 基线对照同 FAIL**（评审 #28 已登记的"裁决后 overlay 依赖未声明前提"，环境
  性既有，非本批回归）。`smoke-anim` 同理（基线同 FAIL——本终端会话夹具环境问题）。
- 金回放：本批零 vtable/组件 id/系统序变动；#2 修复明确保持单线程档行为零变化
  （chunk 序 = 串行迭代序），零重录预期成立（17 步回归待 GUI 会话跑全量）。

## 遗留与移交

- 设备丢失**编辑器路径**验收（SimulateDeviceLoss 接线编辑器 + 真会话观察）——后续批；
- DrainEvents 多事件真实链（RmlUi 复选框双事件）机器覆盖、SetItems 非 ASCII key 的
  C# 夹具用例——需扩冒烟播种面，随 M6b/M7a 冒烟维护批；
- smoke-anim / smoke-template overlay 在本终端会话的既有 FAIL 归因（#28 登记项）——
  与本批无关，全量 17 步回归（GUI 会话）待跑；
- 报告其余 medium/low（含 #8 纹理同步上传、#12 槽名截断、#14 域线程违例、#25 DPI
  双缩放、#26/#37 结构轨全景快照、#31 Rescan 增量、#43 EnTT 头泄漏守卫等）按
  处置建议后续批消化。
