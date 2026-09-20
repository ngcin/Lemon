# Lemon 开发日志（bench 数字与事件记录，08 §6）

> 纪律：每步验收的实测数字记此处；性能回退 >10% 标红。日期均为 2026 年。
> 测试方法与判读见 `EngineDesign/09-Testing.md`（本文件记数字流水）。

---

## 2026-09-20 · M4.6a 会话闭环完成（九条目 + 交互冒烟负向实验）

按 [M4.6-Editor-Usability-Plan.md](EngineDesign/M4.6-Editor-Usability-Plan.md) §4 实施
（commit `2273e7f`）：无项目引导卡（两按钮直达 + 最近列表；状态栏红字）、FilePicker
目录选择模式 + 模态化（开着选择器不能再点背后 UI——实测竞态修复）、向导父目录浏览、
打开项目改选项目根目录、最近项目 `$HOME/.lemon/recent.json`（5 条 + 自动重开 +
`--no-reopen`；冒烟/终验不记，防 /tmp 项目污染列表）、标题栏 `<场景>[●] — <项目> —
Lemon`、Hierarchy 重命名（F2/右键/叶子双击 → Enter/ESC/失焦；属性轨 Undo）、Edit 菜单
Undo/Redo 真接线（原快捷键通、菜单恒灰）、`--smoke-close clean|dirty` 关闭状态机冒烟。

**负向实验（§4-9 验收）**：故意移除干净场景退出分支 → `--smoke-close clean` 立即
`exitedEarly=0 => FAIL`（exit 1）——交互冒烟防线真实生效，还原后 OK。首做负向时踩
自己一脚：把语句注释成了"语句+注释"（等于没禁用），实验"通过"——负向实验没变红
本身就是实验失败，先查实验自身再下结论。

**实施偏差（进 M4.6 册 §4-7 注记）**：父节点双击与 ImGui 展开/收起冲突，父节点重命名
走 F2/右键（叶子双击原样）。

**测试隔离坑**：给冒烟设 `HOME=/tmp/...` 反而打崩 `dotnet build`（DOTNET_CLI_HOME
找不到用户目录）→ `--final` 假失败（热重载 0 次）。终验/交互验证一律用真实 HOME；
recent.json 验证前备份、验完还原。recent 链路端到端（记录→自动重开→--no-reopen
跳过）实测三态通过。

**回归**：ctest 3/3；M4.4 冒烟 600 帧 `errors=0`（另有无项目 120 帧 PASS——引导卡
渲染入面）；`--final` 620 帧全 OK（热重载 play=1334ms/edit=1326ms、stateBag 66/66、
fps 59、冷启 342ms）——M4.5 判据零回退。

---

## 2026-09-20 · 真人实测三修（关闭无响应/无项目导入/打开项目）+ M4.6 规划定稿

M4 收官当日作者首次连续使用编辑器，暴露 3 个阻断级 bug（全部"只有真人点得到"的
交互路径），当日修复并回归（commit `b7094a9`）：

1. **点关闭按钮无响应，场景变脏后才弹确认**：主循环退出裁决只写了 `dirty` 分支
   （`exitRequested_ && ctx_.dirty` → 确认框），干净场景下 `exitRequested_` 没有任何
   消费路径——anim-smoke 骨架遗留。修复 = 裁决二分（脏确认/干净即退）。
2. **无项目状态导入必报"导入失败（IO）"**：`root_` 为空时 `AssetsRoot()`="/Assets"，
   拷贝到文件系统根必失败。修复 = 菜单层守卫（可操作提示）+ `ImportFile` 底层守卫。
   上游缺口 = 编辑器允许无项目运行却无引导（M4.6a 横幅补）。
3. **"打开项目..."菜单只打日志**：从未接线（M4 按 `--project` 单会话设计）。做成真
   功能：FilePicker `.lemon` 模式 → 会话内切换（watcher 先停再启/重建回调防叠加/
   无 Game 清旧脚本宿主/脏场景先确认/NewScene 新会话）。
4. 顺手修两枚同族雷：`playing_` 冗余态从未同步 → Play 中 Ctrl+S 会把 **Play 世界**
   存进编辑场景（数据丢失级；`BuildUI` 每帧对齐真值）；ESC 直接触发退出（骨架
   遗留便利键）→ 改 Play 中 ESC=Stop（边沿检测）。

**教训（进 M4.6 §1）**：冒烟框架驱动数据与渲染，从不模拟菜单/关闭/快捷键——交互
路径零自动回归是三 bug 共同根因。M4.6a 把 关闭/保存/打开 三条状态机脚本化进冒烟。

回归：ctest 3/3；M4.4 冒烟 600 帧 `errors=0`（冷启 551ms）；`--final` 620 帧全 OK
（热重载 play=1327ms/edit=1317ms、stateBag 66/66、fps 59、进出 4.3/0.4ms 逐字节、
autosave 链、冷启 367ms）——M4.5 指标零回退。

同日定稿 [M4.6-Editor-Usability-Plan.md](EngineDesign/M4.6-Editor-Usability-Plan.md)
（会话闭环 M4.6a ~1 周 + 编辑效率 M4.6b ~1–1.5 周；出口 = 30 分钟零命令行全流程）。
已知限制记录在案：会话内切两个有贴图的项目时新项目 spriteId 红字自愈重编号
（重启编辑器打开则无；彻底解 = 场景引用 guid 间接化，M5 评估）。

---

## 2026-09-20 · M4.5 热重载 + 项目向导 + 自动备份 + 终验（M4 收官）

按 M4-Editor-Plan §5 M4.5 顺序实施。动工前先跑 ADR-010 既定探针复测（结论同
M3-2b 矩阵：UCO 一次性线程 OK / 域线程全形态 pin / **pin 后重载可用**——A 线定案，
结论写回 ADR-010 同日修订）。

**热重载（A 线整域重建，§3.7）**：

- C# 侧：`StateBag`（键→装箱值；白名单 = 基元/枚举/Vec2，SDK 常驻 ALC 身份——
  用户 struct 装箱即 pin 旧域，一律拒绝）+ `LemonBehaviour.OnHotReloadOut/In`
  虚方法 + `Behaviours.CaptureForHotReload`（待恢复包独立于 Reset，换装间存活；
  Attach 时同 (类名,实体) 命中即恢复）+ `DomainManager.ReloadScript`（域线程捕获
  →UCO 线程短轮询 3×GC+5ms 尽力卸载→泄漏计数→新 ALC）+ 导出 `lemon_dm_reload/
  lemon_hr_reloads/lemon_hr_leaks`。
- C++/编辑器：`ScriptHost::HotReloadAssembly`（导出惰性解析，旧 Entry 程序集 =
  优雅降级）；Game/ 第二 FileWatcher（500ms 轮询）→ 0.4s 防抖 + `.cs/.csproj`
  过滤 + obj/bin/点目录排除（**防 dotnet build 自写自触发的死循环**）→ 外置
  `dotnet build` → 换装 → `RefreshScriptsAfterReload`（Edit 刷 typeId / Play 原位
  重装配走新域 Awake/OnEnable/OnHotReloadIn）；Console 计时 + 泄漏红字、Profiler
  换装/泄漏常驻显示、菜单"重新编译脚本"手动入口。
- 实测：**Play 中换装 1244ms / Edit 态 1216ms（≤2s 判据，含增量编译）**；泄漏
  每次 +1（A 线已知，~百 KB 级/次）；watcher 自动触发实测（sed 两次写被防抖吸收
  一次，双触发幂等无碍）。

**项目向导（blank 模板，06 §1/§7）**：

- `ProjectWizard`（lemon-editor-core，可单测）：目录骨架（Assets/Scenes/Prefabs/
  Game/Data/.lemon/Builds）+ project.lemon（name/engineVersion/guid）+ 项目
  .gitignore + 种子资产 spawn.png（32×32 柠檬黄，固定 guid）+ Game/ csproj
  （HintPath 绝对锚定 SDK，创建期固化）+ GameMain/InputMover/Spawner（**模板自带
  StateBag 迁移范式**；kSpriteGuid = 种子资产 guid 直连）+ Scenes/Main.scene。
- 编辑器：File→新建项目... 模态（名+父目录）→ 创建即 OpenProjectPipeline（编译
  Game/ + 装配宿主 + 开双 watcher）→ 打开 Main.scene，零配置直接 Play。
- 资产扫描根改**项目根**（06 §1 对齐）：根级 Prefabs/ 入索引（M4.4 落位偏差
  消除），Game/Scenes/Data/Builds/obj/bin 排除；relPath 统一项目根相对；旧
  manifest 键（相对 Assets/）同号迁移（spriteId 不漂）。

**自动备份与崩溃恢复（§3.8）**：

- `EditorContext`：TickAutosave（300s 节拍 + dirty + 非 Play + 非空场景门）/ 
  AutoSaveNow → `.lemon/autosave/<场景名>.scene`（单份滚动）；DetectAutosaveRecovery
  （mtime 新于盘档或盘档缺失）；OpenSceneRecovery（载入内容、scenePath 保持指原
  .scene、dirty 置位——落盘与否用户决定）；正常 SaveScene 后清除快照（防陈旧提示）。
- 启动恢复提示模态（恢复/忽略）；终验内全链断言（快照→检出→恢复保持 dirty→
  落盘→清除）。

**Profiler GC 红字（§6 #7）**：GcAllocated 每帧差分（首帧立基线）>0 红字 +
"零分配 ✔"绿态；热重载换装/泄漏计数常驻（红字 = 泄漏 >0）。

**验收（§6 判据项 + 回归）**：

| 项 | 判据 | 实测 | 结果 |
|---|---|---|---|
| #1 出口总判据 | 新建项目→零代码→走地图+刷怪可 Play | `--final`：向导→判据场景 14 实体（地面平铺+角色 InputMover+刷怪器 Spawner）→Play 可跑 | PASS |
| #2 热重载 | ≤2s（Edit/Play 各一）+ 泄漏可见 | Play 1244ms（StateBag 续跑 66/66 精确断言）/ Edit 1216ms（新类注册表可见）；leaks=2 红字 | PASS |
| #3 冷启动 | <2s | 终验口径（主循环首帧，向导另计）340ms；M4.4 冒烟口径 986/557ms | PASS |
| #6 Play 性能 | 判据场景 ≥45fps | 59fps（min，剔除预热 60 帧+换装窗口 90 帧） | PASS |
| #7 GC 纪律 | 面板红字口径正确 | 每帧差分实现 + 终验脚本热路径零分配（errors=0） | PASS |
| #13 工程回归 | 全绿 | engine-tests **12972**（+37：wizard/autosave/扫根断言）× Release+ASan/UBSan；script-tests **1275**（+7：dm_reload 换装探针）；ctest 3/3；M4.4 冒烟回归全 PASS | PASS |

（#1 的录屏属人工项：步骤清单 = `--final` 冒烟的自动化等价序列，量化表如上。）

**过程坑（4 则）**：

1. **ofstream 未 close 就触发编译**：终验改写 .cs 后在同作用域内立即 dotnet
   build——流缓冲未落盘，编译读到半截文件 rc=1（首轮 Play 换装失败根因）。
   修复 = 写盘作用域收窄先 close。诊断时 build 输出 >/dev/null 不可见也延误定位。
2. **ImGui::GetTime 在主循环外冻结**：OpenProjectPipeline 里用它测 Game 编译耗时
   恒 0ms（g.Time 只在 NewFrame 推进）——换 steady_clock。
3. **终验 projectDir 双轨**：向导把新根写进 launchCopy_ 而后续分支读入参 launch
   → 空路径跳过整条管线（无宿主/无资产/0 实体的连锁假象）。统一读 launch_。
4. **lemon-tests 链接 stb**：ProjectWizard 进 core 后引用 stbi_write_png，而
   StbImpl.cpp 原在 lemon-editor 目标——静态库按需拉取对象，单测此前从未拉过
   AssetGpuCache.o 所以没炸。StbImpl.cpp 移入 core（编辑器经 core 链接同一定义）。

**M4 收官**：M4.0–M4.5 全部完成（总判据链全量化 PASS）。遗留 M5 项：B 线换装
（探针转绿即启用，代码就绪）、逐字段 override（ADR-009 修订待记）、多窗口、
`--smoke` ctest 条目（09 §8 顺延）。

---

## 2026-09-19 · M4.0–M4.3 编辑器四阶段（ImGui docking 壳 → 数据面板 → SceneView → Play 沙盒+Undo）

按 M4-Editor-Plan.md §5 顺序实施，每阶段可运行验收 + 截图目检（`--screenshot` + RHI
调试截屏通路：交换链加 TRANSFER_SRC，EndPass 后 blit 中转缓冲取回）。

**M4.0 骨架**（ImGui v1.92.9b-docking + stb 锁 commit 入 CPM 缓存，THIRD_PARTY/07 登记）：

- 边界纪律双豁免口落地：`rhi::VulkanInteropHandles`/`NativeCommandBuffer`（引擎侧）+
  `Editor/App/ImGuiBackend.cpp`（编辑器侧唯一含 Vulkan 头 TU，07 例外登记）；
  `tests/imgui_isolation.cmake` ctest 断言 Engine/ 树零 ImGui 引用。
- 内核配套：Log 汇聚通道（sink+分级计数，#14）/ Window 事件观察者（IME 事件桥）/
  RHI 调试截屏。v1.92 API 适配三坑：`ScaleAllSizes`、`IsGlyphInFont`（字体探针）、
  新纹理后端不手工 `Fonts->Build()`。
- 验收：冷启动 1741ms（首启含管线编译）→ 543ms（管线缓存二启），<2s ✔；CJK 走系统
  STHeiti（TrueType 轮廓实查；内嵌 OFL 子集留打包期）；验证层零报错；默认布局截图
  目检（左 Hierarchy/右 Inspector/中央标签页/底部 Console+Assets）。

**M4.1 数据面板与场景 IO**：

- 内核 #1 `ECS/Hierarchy.{h,cpp}`：SceneSetParent 双链手术/防环（后代 DFS+深度≤8）/
  世界矩阵合成（Mat3x2 参照断言）；#5 Meta.guid（40→48B，C# 镜像同步，破 M3 前回放档）；
  #6 FieldEditorMeta 平行表（Range/Degree/ColorHex/Bool8/Enum/Hide，探针协议不动）。
- 面板：Hierarchy（树/拖拽父子/Ctrl 多选/右键/搜索）、Inspector（反射驱动 27 组件
  全字段可写 + Add/Remove + 数组段只读 + 运行时字段灰显）、Console（实装）、Profiler
  （Profiles/GPU 时间戳同源）；FilePicker 内置（无 OS 对话框依赖，可无头冒烟）。
- 场景 IO：打开/保存/另存/Ctrl+S/脏标记/关闭确认模态；`--scene`/`--save-scene` CLI
  roundtrip 验收（播种 5 实体含父子链 → 保存 1938B → 重开 5/5 守恒 PASS）。
- 测试：+42 checks（Hierarchy 链生命周期/防环/世界矩阵 vs Mat3x2/guid 往返/编辑器
  元数据健全性）= **12872×1 全绿**；script-tests（C# 布局镜像）PASS。

**M4.2 SceneView 与编辑交互**：

- 内核 #11：`TextureDesc.renderTarget` + `CommandList::BeginOffscreenPass`（EndPass 按
  目标分流：交换链→PRESENT_SRC，离屏→SHADER_READ 供 UI 采样）；SpriteBatcher::Init
  增 colorFormat 参数（离屏 RGBA8Unorm vs 交换链 BGRA8Srgb，验证层实抓格式 VUID）。
- `Editor/Interaction/ViewportRenderer`：提取系统（#1 世界变换消费/#2 销毁禁用差集
  释放/#4 场景代际失效）+ 程序化测试图集（8×64px 调色板 + ASCII 字体页，零外部素材）+
  双视口离屏 + overlay 通道（细条模拟线/框，层 63 顶置）+ 实体名标签（zoom 反缩放）。
- SceneView：pan（中键/空格）/zoom at cursor/F 框选聚焦/拾取（世界 AABB 旋转逆变换）/
  Gizmo 三态（移动十字/旋转圈 32 段/四角缩放，吸附 8px/15°/0.25）；GameView letterbox
  16:9。spriteId 1 起始坑（Atlas 句柄 0 无效，验证层前 assert 抓出）。
- 验证层修 4 真错：离屏管线格式 / 时间戳超写（离屏 pass 不记）/ RT 重建在用销毁
  （WaitIdle）/ 退出时序（ImGui 资源先于 idle 释放）。viewportVisible=4 + 截图目检
  （sprite/标签/网格/选框/手柄/角标全到位）后 errors=0。

**M4.3 Play 沙盒 + Undo 双轨 + 输入路由**：

- `EditorContext` §3.3 全形态：edit/play 双 World、Active 视图（面板/提取统一切换）、
  进出 Play 全 checklist（快照固化→Load 重建→清 Undo/存 guid 选中→计时）。
- **验收 #4/#5 实测：进 Play 0.5ms / 退 Play 0.3ms（判据 500/300ms，千倍余量）；
  Play 中编辑落 Play World（挪主选中实体）→ Stop 后序列化与进 Play 前快照
  逐字节一致（`--play` 冒烟断言，byte-exact=YES）**——零状态泄漏结构保证成立。
- Undo：属性轨（组件字节快照，guid 找回；Inspector 控件 IsItemActivated/Deactivated
  合并 + Gizmo 拖拽整段一条）+ 结构轨（场景 JSON 双快照，SceneArchive 单通路）；
  上限 100 FIFO；Play 禁用；Ctrl+Z/Y/P。
- 输入路由 §3.6 子集：GameView 聚焦+悬停门控 → WASD/箭头/空格 → InputState →
  Play World（WantTextInput 屏蔽）。
- Pause=dt0（Essential 照跑：销毁提交不断）+ 单步按钮。

**回归**：lemon-tests 12872 / script-tests / imgui-isolation / anim-smoke（300 帧
验证层零错）/ bench-sprites 50k 1 batch —— 全绿。编辑器冒烟
`--smoke --play --frames 180 --validate --screenshot` = 冷启 578ms、uiVtx、CJK、
实体守恒、viewportVisible、**errors=0、play 往返 OK** 五重断言 PASS。

**遗留（M4.4/M4.5 待做）**：资产管线（GUID/.meta/manifest/PNG 导入/stb_image）→
AssetBrowser/Inspector 资产槽；C# ScriptBox 装配通路 + SDK 增量 + 热重载（ADR-010
探针复测先行）；项目向导/自动备份/终验收录屏。

## 2026-09-19 · M2 修复轮（ISSUE-1..8 全部修复：5 文件 ~25 行，12829×3 全绿）

用户批准后按清单修法逐项实施（清单 §7.2 有逐文件明细）：

- **P1 三项**：ISSUE-5 Flee 排除自身（原速度被主动清零=实体冻结）；ISSUE-2 Load 按
  段表钳制数组 count（原 ASan 实锤 heap-buffer-overflow @ Systems.cpp:501）；
  ISSUE-1 schemaVersion 显式判型（原字符串版本抛 type_error 抛穿加载器）。
- **P2/P3 五项**：ISSUE-3 管线双断言（重名查 systems_ / RunStage 长度断言）；
  ISSUE-4 Spawn 告警门控+成员化；ISSUE-6 Patrol 折返同帧改向；ISSUE-7 触发器互不
  触发守卫；ISSUE-8 kEquipment 删 relicIds 双登记（合法档假告警）。
- **验证**：单测 12829 checks ×3（Release/ASan+UBSan/TSan）零报告；探针复跑假告警
  消失、越界消失；回放旧档兼容 PASS×2 + 新档重录 PASS×2（修复对 bench-sim 逐帧
  零漂移）；bench-sim avg 2.68ms ≤ 8ms。

## 2026-09-19 · M2 验证轮（复核清单问题：8 项全属实，ISSUE-2 ASan 实锤，Engine 仍零修改）

对 M2-Review-Checklist.md 登记问题逐项独立复核（源码逐行 + Release 复跑 12821 OK +
ASan 探针）。结论：全部属实，其中 1 项范围修正、1 项新补登：

- **ISSUE-2 越界实锤**：`{"count":200}` 无数组键读入 → StatSystem tick 即
  `heap-buffer-overflow @ Systems.cpp:501`（1000 实体命中池边界；8 实体越界落
  池内、ASan 静默）。**范围修正**：StateHash 有 clamp（StateHash.cpp:63）
  安全，越界消费方仅 StatSystem——清单初版误报已更正。
- **补登 ISSUE-8（P3）**：Equipment.relicIds 字段+数组段同名双登记 → 合法档每次
  读入必发一条 type mismatch 假告警（数据无损）。
- ISSUE-5 后果确认更重：Flee 实体速度被主动清零并覆盖 Chase 速度 → 完全冻结。
- 清单引用更正两处（E10→N5、P6→N10），详见 Checklist §7.1。

## 2026-09-19 · M2 复核轮（只读审计：Engine 零修改，21 组新测试 + 7 项问题登记）

**约定**：应用户要求本轮不修改任何 Engine 代码，只新增测试与文档。上轮 11 项修复的
验证延续（18000 帧双档回放双 PASS 后进行）。

**产出**：
- 单测 11545 → **12821 checks**（新增复核节 21 组 / +1276 checks），Release +
  ASan/UBSan + TSan 三套全绿零报告。
- 新文档 `EngineDesign/M2-Review-Checklist.md`：代码结构地图、分层纪律 grep 实证、
  模块不变量清单（Core/ECS/序列化/空间/系统/回放共 60 项）、问题登记、覆盖矩阵、缺口。
- **新登记 7 项问题（全部未修，证据测试固化现状）**，其中 P1 两项：
  - **ISSUE-5（P1）Flee 特性完全失效**：AISystem 调 NearestAny 未排除自身 →
    自己 d²=0 恒为最近威胁 → 逃逸速度恒零。bench-sim 无 Flee 实体从未暴露。
  - **ISSUE-2（P1）恶意档越界**：数组段 count 无数组键时不受截断 →
    StatSystem 越界读写（StateHash 有 clamp 安全——验证轮更正；ASan 已实锤）。
  - ISSUE-1（P1）schemaVersion 字符串抛异常抛穿 Load；
    ISSUE-3（P2）AddSystem 重名断言空转 + RunStage 未排序越界；
    ISSUE-4（P3）SpawnSystem 无工厂告警无条件触发；
    ISSUE-6（P3）Patrol 折返帧速度滞后一帧；
    ISSUE-7（P3/设计）触发器无层过滤，重叠互触发。
- 分层纪律 grep 实证全合规（entt 封装/Vulkan 零泄漏/nlohmann 收敛/随机源统一）。

| 验证 | 结果 |
|---|---|
| 单测三套 | 12821 OK ×3，ASan/UBSan/TSan 零报告 |
| Engine 改动 | 0 行 |

---

## 2026-09-19 · M2 全量复审（提交后审计轮：11 项修复 + sanitizer 三件套零报告）

**背景**：M2 七提交（78d921f..efb78eb）落库后做从头复审——全部 M2 源文件净室读码 +
全新构建目录重建 + 双档回放复跑 + ASan/UBSan/TSan。**修复前基线全绿**（问题均为
潜伏路径：未覆盖的组件组合 / 畸形输入 / 并发时序），修复后 11545 checks、
双档回放 PASS、bench-mow 无回退、sanitizer 零报告。

### 修复清单（真问题 11 项）

| # | 问题 | 根因 | 修复 |
|---|---|---|---|
| 1 | **Scene::Destroy 数据竞争**（UB） | ProjectileLifetimeSystem 在 ParallelFor worker 里并发调 Destroy，裸 vector push_back | destroyMutex_ 保护队列 + DestroyQueueTag 打标同锁；CommitDestroys 锁内 swap 出队 |
| 2 | **AliveCount 虚高** | entt 3.15 实体池删除策略 = swap_only：销毁槽位以 tombstone 留在 packed 数组，storage size() 含回收位 | 改 `createdTotal_ - destroyedTotal_` 精确计数（bench alive 10015→10002 修正） |
| 3 | **Each() 遍历到死亡槽位** | 同上：tombstone 以换代句柄混进遍历 → Save 会把已销毁实体写进存档 | Each 内 registry.valid 过滤 |
| 4 | **.lscene 数组段丢失** | StatusEffects.active / Inventory.items 只写 count 不写内容；Equipment.relicIds[3] 登记成单个 UInt32（只存首个） | ArraySegMeta 元数据（元素字段表）+ SceneArchive 读写 + StateHash 统一走段表（relicIds 哈希补全 12B） |
| 5 | **PassFilter 越界判断反转** | `team<32 && !mask` 写法使 team≥32/layer≥16 反而跳过过滤被放行 | `>= 上限 ∥ 不匹配 → 不命中`（与注释语义一致） |
| 6 | **AISystem Chase 块缺守卫** | Pool<Chase> 切分不含伴生组件约束，缺 Transform2D/Velocity 的实体 try_get 解引用空指针 | all_of 守卫（与 Separation 同型） |
| 7 | **Load 异常抛穿** | 字段类型错（"pos":"x"）/entities 非数组/components 非对象 → nlohmann 异常直接炸编辑器 | 字段级 try/catch 降级 + is_array/is_object 结构校验 |
| 8 | **同帧多源双死** | Hazard 不设 iFrames，已死目标被多 Hazard/弹重复结算 → 多个 Death 事件 | 两处伤害路径 `cur<=0` 早退 |
| 9 | **XpProgress 死循环风险** | xpToNext 资产配 0 时 ceil 收敛卡死升级环 | `max(1.0f, ceil(...))` |
| 10 | **6 字段误序列化** | Spawner.cooldown / Hazard.tickPhase / Projectile.age+hits / Health.iFrames / Trigger2D.inside 注释标"运行时"但漏 kFieldRuntime | 全部补 FIELD_RT；Trigger2D._pad 复用 hack 改显式 fired 字段（RT） |
| 11 | World::Step 无 active 场景空指针；SpawnSystem 无工厂告警放循环内吞掉后续 spawner 冷却推进 | 边界 | 空步 return；告警外提 |

**单测新增 7 组 34 checks**（11511→11545）：数组段 roundtrip 保真、RT 字段不入档、
恶意 JSON 容错、越界 team/layer 不命中、并发 Destroy（4000 实体 4 线程）、
DestroyQueueTag 生命周期、无场景空步、双死事件恰一。

### 验证矩阵（修复后）

| 验证 | 结果 |
|---|---|
| 单测（Release / ASan+UBSan） | **11545 checks OK** ×2，sanitizer 零报告 |
| bench-sim 1 万怪 1800 帧 | avg 7.37ms（后台负载下，判据 ≤8；首轮实录 5.10） |
| 确定性回放双档（修复后重录 18000 帧，哈希函数已改仍逐帧一致） | **双 PASS**（单线程 avg 9.35ms 含逐帧全量哈希 / 多线程 2.67ms；mismatches=0） |
| bench-mow M1 回归 | PASS（cpuRender 4.83ms） |
| ASan+UBSan bench-sim 600 帧×双档 | 零报告 + 回放 PASS |
| TSan（tests + bench-sim） | 零数据竞争（对 #1 修复的直接验证） |
| 构建警告 | Lemon 自有代码 4→0（SDL 第三方 1219 条不属治理范围） |

**遗留讨论项（未修，见 M2 收官汇报）**：Hazard 命中半径 48 硬编码（组件无 radius
字段，M5 资产化时补）；Spawner.cooldown 读档回落 0 立即触发一轮（配额语义可接受）；
spike/03-csharp/dotnet 构建产物误入版本库（建议 .gitignore + untrack，待用户定）；
JobSystem 嵌套 ParallelFor 禁用约束已文档化（无作业图需求前不实现）。

---

## 2026-09-19 · M2 ECS 运行时完成（World/ECS 骨架 → 16 系统管线 → bench-sim 确定性回放）

**环境**：macOS 24.6 / Intel 6C（本机）；bench-sim 纯 CPU 无渲染（不开窗口/不初始化 Vulkan）。
**交付**：`Engine/Core`（JobSystem/Random/Pool/RingQueue/FunctionRef）→ `Engine/ECS`（Entity/Scene/World/
SystemPipeline/ComponentRegistry/TeamTable/StateHash/Events）→ `Engine/Components`（27 组件目录+登记表）→
`Engine/Physics2D`（SpatialHash 查询层）→ `Engine/Systems`（16 系统：12 真实现 + 4 里程碑占位）→
`Engine/Serialization`（.lscene v1 + 迁移链骨架）→ `Samples/bench-sim`。
**决策记录**（开工前与用户对齐）：完整 JobSystem（非单线程起步）；最小 .lscene 序列化（引 nlohmann/json v3.11.3）；
F3 = 统计层+文本（ImGui 版 M4）；Tracy 暂缓。

### M2 验收（08 §3）

| 判据 | 结果 | 实测 |
|---|---|---|
| bench-sim 1 万怪全系统 ≤8ms/步 | ✅ | **avg 5.10ms**（多线程 5 worker，引擎默认形态）@ alive 10053；单线程诊断档 avg 19.9ms（并行是 8ms 判据的必要条件——完整 JobSystem 决策的实证） |
| 确定性回放：同输入 5 分钟逐帧一致 | ✅ | 18000 帧 × **双档 PASS**（--threads 1 / 多线程 5 worker，逐帧状态哈希 mismatches=0；录制档 avg 9.7/2.6ms 含每帧全量哈希 ~3ms 开销） |
| F3 数据齐全 | ✅ | 每系统 μs（last/max/累计）+ 实体/事件/池统计，`--stats` 输出（ImGui 面板 M4 接同一数据源） |
| 单测 | ✅ | **11511 checks OK**（M1 167 → M2 11511：RNG/Job/池/环形队列/ECS 生命周期/序列化 roundtrip/哈希/管线端到端） |
| M1 回归 | ✅ | bench-mow 120.1fps（基线 107fps，无回退） |

### bench-sim 1 万怪分解（3600 帧，多线程 5 worker）

| 系统 | avg | 说明 |
|---|---|---|
| Separation | 1.57ms | 分离力（密度截断后，见优化 3） |
| AI | 0.26ms | 目标板最近邻（见优化 2） |
| SpatialHashRebuild | 0.18ms | 10k 实体重建（单线程 std::sort，预算 1.2ms 内） |
| Movement | 0.09ms | 积分+边界钳制（并行） |
| Hitbox/Spawn/Stat/回收等 | ~0.03ms | — |
| **合计系统时间** | **~2.1ms** | avg 5.10 含 ParallelFor 派发与调度开销 |

### 性能优化记录（1 万怪语境，保留过程）

1. **Spawner 配额失控**（首轮 alive 涨到 5820、分离力 6ms@1k）：SpawnSystem 无 maxAlive 语义，
   生成率 > 死亡率 → 怪无限增长 → 分离力 O(n²)。修复：per-team 存量普查（30 tick 周期）+ maxAlive 配额。
2. **AI retarget 尖峰 85ms**：Chase 用 aggroRange=全场 的 OverlapCircle 找最近目标 → 每 6 tick
   全员扫全部 cell（O(n×cells)，玩家队只有 1 个实体——大半径哈希查询是错误算法）。重构为
   **TargetBoard**：按目标 team 预收集位置（一遍 O(n)），逐怪线性最近邻 O(teamSize)。
   尖峰 85.6→6.7ms、avg 0.26ms。稀疏目标走板、密集查询（命中/分离/磁吸）走哈希——各得其所。
3. **分离力密度截断**（03 §14"密度上限"的实测落地）：万怪堆叠玩家时 cell 内遍历退化 O(n²)
   （实测 6ms@1k）。每实体只处理前 10 个有效邻居，哈希回调序 = cell→id 升序 → 截断确定（回放安全）。
   1 万怪分离力 → 1.57ms。

### 事故与修复（对齐 M1 风格保留过程）

| # | 事故 | 根因 | 修复 |
|---|---|---|---|
| 1 | 序列化 roundtrip 丢 parent | EntityRef 写出与实体编号同遍历（EnTT 遍历序 ≠ 创建序，父实体未编号即被引用） | 两遍式：先全部编号再写字段；输出按句柄排序（roundtrip 不动点成立） |
| 2 | 全部敌对判定静默失效（弹穿过玩家不命中） | World 的 TeamTable 默认构造（全 Ghost）未装 Default 表 | World 构造装默认表；教训：**关系表零值必须选安全方向**（Ghost），且默认表要进构造 |
| 3 | SpawnSystem 首帧 SIGSEGV | 普查倒计时初值溢出跳过首次普查 → teamCounts 空 → 下标越界 | 倒计时 0=本帧普查，首帧必查 |
| 4 | Shooter 弹体势力硬编码 team3 | 玩家弹幕队写死，怪射玩家的弹敌我判定反转 | 弹体势力继承射手 Meta.team |
| 5 | entt 空组件 emplace/get 返回 void | 3.15 对 is_empty 组件特化（tag 无数据） | Scene 封装层特判（共享空实例引用），业务无感 |

### 移植与依赖登记

- Luma `Event/JobSystem`（MIT，B 级）：结构移植 + 01 文档点名的 Schedule 值语义修正
  （packaged_task 移动入队，消 IJob* 生命周期陷阱）+ ParallelFor + 单线程诊断档；源文件头保留版权注记。
- nlohmann/json v3.11.3（MIT，CPM 锁 tag）：.lscene 序列化；THIRD_PARTY.md + 07 文档已登记。
- EnTT v3.15.0 从 spike 转正为引擎内核依赖（PUBLIC 链接，封装层 Engine/ECS 内允许、业务侧禁直用）。

### 复现命令

```bash
cd GameEngine/Lemon && cmake --build --preset mac
./build/mac/tests/lemon-tests                                   # 11511 checks OK
./build/mac/Samples/bench-sim/lemon-bench-sim --n 10000 --frames 3600 --stats   # 性能
./build/mac/Samples/bench-sim/lemon-bench-sim --n 10000 --frames 18000 --threads 1 --record r.rpl
./build/mac/Samples/bench-sim/lemon-bench-sim --n 10000 --frames 18000 --threads 1 --replay r.rpl  # PASS
```

---

## 2026-09-18 · M1 渲染内核完成（S0–S8，8 commit）

**环境**：macOS 24.6 / AMD Radeon RX 590 / MoltenVK api 1.3.357 / Vulkan 验证层全程开启

### M1 验收（08 §3 / 02 §9）

| 判据 | 结果 | 实测 |
|---|---|---|
| bench-mow ≥60fps（10 万精灵+5 万粒子） | ✅ | **107.0 fps**（IMMEDIATE，全可见，GPU 1.52ms）；FIFO 贴 vsync 61–62fps |
| CPU 渲染线程 ≤4ms（02 §9 = 压测 A 语境） | ✅ | **3.05ms**（1 万精灵+10 万粒子全可见：extract 1.96 + bake 1.08 + record 0.008）|
| 粒子 10 万 ≤4ms GPU | ✅ | **0.74ms**（bench-particles 存活 8.7 万时 222fps）|
| 图集切换不闪帧 | ✅ | 两图集（精灵槽0/字体槽1）三段合批，**批数恒定 4**（精灵1+粒子2+文本1），帧间零波动 |
| 设备丢失模拟自动恢复 | ✅ | 全规模注入后帧计数保持、画面恢复、**验证层零错误** |

### 分场景实测（bench-mow，IMMEDIATE）

| 场景 | fps | CPU 渲染 | GPU | 批数 |
|---|---|---|---|---|
| 10 万精灵 + 5 万粒子（全可见） | 107.0 | 6.32ms（extract 4.07 + bake 2.24 + record 0.01）| 1.52ms | 4 |
| 1 万精灵 + 10 万粒子（压测 A 构成） | 217.0 | **3.05ms** | 0.77ms | 4 |
| 10 万精灵 + 5 万粒子 + zoom 2.58（可见 15%）| 122.9 | 5.17ms | 1.35ms | 4 |

> 全可见 15 万实例 CPU 6.3ms 超出的"4ms"是压测 A（1 万实体基数）预算，非 bench-mow 判据；
> 剔除遍历本身 O(全实体)，10 万实体光遍历+插值 ≈1.9ms 起步。M2 若需要可上分块剔除。

### 单项 bench

| 程序 | 负载 | 结果 |
|---|---|---|
| rhi-smoke | 2000 实例全链路 | FIFO 60fps，GPU 0.054ms，验证层 0 错误 |
| bench-sprites | 10 万精灵完整流水线 | **152.3fps**（IMMEDIATE），渲染 CPU 3.76ms，1 批 |
| bench-particles | 10 万预算粒子 | **222.5fps**，GPU 0.741ms，2 批 |

M0 基线对照：spike-02 裸实例化 270fps（仅写 24B/实例）；新流水线 152fps = 多付提取-双缓冲-
插值-剔除-排序-合批全链路代价，15 万实例总 CPU（模拟+渲染）9.3ms 仍余 40% 帧预算。

### 性能优化记录（保留过程，数字为优化前后实测）

1. **批分组摘要碰撞**（粒子 5k 切 672 批）：排序键只放批键哈希 16 位摘要，两混合模式
   摘要碰撞 → 相邻不同键反复切批 → 改完整 64 位 key.hash 分组 → 2 批，record 2.07→0.13ms。
2. **粒子提取桶化**：std::sort O(n log n) 4.9ms → 计数桶 O(n) 1.6ms（粒子层内 order 恒 0）。
3. **sin/cos 查找表**（Core/Math FastSin/FastCos，4096 项 + 线性插值，误差 <1e-3）：
   bake 3.12→2.07ms（15 万实例仿射是热路径）。
4. **单遍提取 + 搬运分桶**：两遍遍历（重算剔除/插值）→ 单遍生成 + 槽缓存 + 56B 纯搬运。
5. **精灵排序免除**：键桶化后 order 全零时桶内池序即稳定序（bench 场景免 std::sort）。

### 事故与修复（验证层/看门狗战果）

| # | 事故 | 根因 | 修复 |
|---|---|---|---|
| 1 | **整机卡死**（bench-sprites 首跑 GPU 877ms/帧，WindowServer 拿不到交换链图像）| bench scale 语义错：传了像素直径 4–14 作"精灵尺寸倍率"→ 每精灵 256–896px → 5000× 过采样 | 尺寸语义对齐（÷64）+ **全 bench 帧时间看门狗**（EMA>250ms 自动中止）+ 探路纪律（小 N→FIFO→放大）|
| 2 | 设备丢失后验证层报 invalid VkBuffer 写描述符 | 恢复回调里旧句柄"看似有效"跳过重建 | 恢复回调先作废全部句柄再按需重建 |
| 3 | 时间戳池未重置 / UPDATE_AFTER_BIND 布局标志缺失 / 提交缺 vkEndCommandBuffer 等 | — | 验证层逐条抓出修复；两条新教训见下 |

**新增本机坑（供后续里程碑）**：
- UPDATE_AFTER_BIND 绑定要求 set layout 挂 `UPDATE_AFTER_BIND_POOL` 位 + SSBO 绑定需
  `descriptorBindingStorageBufferUpdateAfterBind` 特性（采样器无独立 update-after-bind 位）。
- 动态渲染下交换链获取屏障 oldLayout 必须写 UNDEFINED（PRESENT_SRC 只在首帧为真）。
- 时间戳池创建后必须 `vkResetQueryPool` 全量重置一次才能用。

### 产出清单（commit 829003b..HEAD）

- `Engine/`：Core(Math/Log) · Platform(Window) · Renderer(RHI/Atlas/Renderable/SpriteBatcher/
  Particles/BitmapFont/Camera2D/Quality + Shaders)
- `Samples/`：rhi-smoke · bench-sprites · bench-particles · bench-mow（验收场常驻回归）
- `tests/`：167 项纯逻辑断言（数学/批键/UV/相机/质量/粒子池/字体）
- 设计文档：02 分册新增 §11 M1 实测节；本日志

### 遗留（不阻塞 M2）

- bench-mow 全可见 CPU 6.3ms 的进一步压缩（静态 UV 尾巴跨帧复用、SoA 化提取）按需在 M2 性能
  周期做；当前预算语境已达标。
- 路径 B（顶点展开/chunk 烘焙）按计划 M6 Tilemap 时实现。
- TTF→位图图集离线生成器随 M5 资产管线；M1 用内置 5×7 像素字模。

---

## 2026-09-18 · M1 补测轮（覆盖缺口审查，闪烁修复之后）

用户问"还有哪些没测到"——审查发现五块盲区，逐一补测；**其中 mips 生成链是真 bug**。
全部在验证层开启下进行。方法与判读已固化到 `EngineDesign/09-Testing.md` §6。

| 缺口 | 发现与结果 |
|---|---|
| **mips 生成链**（`generateMips` 全工程零调用方，死代码） | **代码 bug**：`TransitionImage` 无 baseLevel 参数，循环内 UNDEFINED→DST / DST→SRC 屏障全部打在 level 0，blit 源层布局被反复打错、目的层从未进 DST——验证层必报错（证明从未跑过）。补 `baseLevel` 参数修复；rhi-smoke 新增 256×256 棋盘纹理（9 层）+ 顶部 256→8px 递减一排（采到第 5 层）+ bindless 槽 3 第二条 draw：**验证层零错误零警告**，mip 链生效（缩小后棋盘收敛为红灰混合） |
| **resize 满负载**（头注释宣称"拖拽自愈"但从未实测） | `Window` 新增 `RequestResize`；bench-mow `--resize-test`：5 次程序化 resize（1600×900→640×400→320×200→1680×380→复原）在 15 万实例下全部重建成功：seen=6≥requested=5、skippedFrames=0、**批数恒 4**、320×200 时可见实例正确降至 8.5 万（剔除联动）；最大单帧 ~1s 为 WaitIdle+重建的合理代价，看门狗不误杀 |
| 管线缓存加载命中 | 落盘此前已验证；本次确认第二次启动命中：`pipeline cache loaded: 9621 bytes` |
| 质量分级实时降档 | 首次实弹触发：40 万精灵（4× 验收负载）压出 EMA 24.7ms → `downgrade -> Med` → 持续超阈 2s → `downgrade -> Low`；全程 stddev 0.98ms、批数恒 4、环形缓冲扩到 40 万+ 干净（642bf51 悬空描述符修复在此规模复验通过） |
| 长时浸泡 | 7200 帧（2 分钟）：fps 60.2、recreates=0、skipped=0、质量保持 High（FIFO 16.6ms < 20ms 阈值，**无误降档**）——无慢泄漏/退化迹象 |

单测 167 项保持全绿。仍未覆盖（记录在案）：Dock 最小化时 acquire 的 0 尺寸路径
（resize 已覆盖退化尺寸分支）；多窗口 M1 范围外。

---

## 2026-09-19 · M3 C# 脚本层收官（bench-script 验收 + 三个深坑）

M3-0~M3-6 已全绿（布局护栏 27 组件 / blit roundtrip / PCG32 位对齐 / 域线程 / 批量 /
事件桥 / LemonBehaviour / 结构命令缓冲，1264 checks）。本日收官 M3-7 验收，过程中
连环踩出三个值得留档的坑——**全部是"10k 规模全绿、更大规模必崩"或"假绿"形态**。

### 坑 1（P0）：deque 不连续 × C# 线性步进 = 野指针

症状：bench-script ≥15k 弹 SIGSEGV（AV in `BoomerangSystem.ForEach`），10k 全绿；
单线程档同崩（排除并发竞态）。之前为修 vector 扩容悬垂把块缓冲改成了 deque——只看了
"push 不搬移元素"，漏了 **deque 分块存储、跨 chunk 不连续**；而 C# 侧
`fr->Blocks + b`、`Comps + slot*stride` 全是线性指针步进。
铁证：libc++ deque 对 24B `BatchBlock` 每块 ~170 元素，10k 弹 = 157 块（chunk 内，
碰巧合法）、15k = 235 块（第 170 块跨 chunk）——阈值正好卡在 170×64≈10.9k。
修复：三缓冲改回 `std::vector` + **每帧构造前按 countFn 预留总量**（容量足够 ⇒
连续与不搬移同时成立）。调试期加的构造校验通道先误导了一轮（校验自身没按
compCount 分槽读，自己就是崩溃点）——校验代码也要按被校验的不变量写。

### 坑 2（P0）：reserve 公式漏了末块补齐 → 构造尾部 realloc → 全帧悬垂

坑 1 修复的第二天形态：script-tests AV（`AddVelocitySystem.ForEach`），bench 反而全绿。
`FlushBlock` 每块恒插 `compCount×64` 个指针（末块不足 64 也整块插入），最坏指针数是
`ceil(n/64)×comp×64`；我只按 `n×comp` 预留，短 `comp×64`。bench 侥幸全绿是因为
72 万字节 reserve 被 malloc 按页取整**碰巧**盖住缺口（720000→720896B 恰 ≥90048 槽）；
script-tests 小规模无取整余量 → 立崩。教训：**"大数侥幸通过"本身就是分配余量
错误的信号**；预留公式必须按插入协议的最坏形状推导，不是按元素计数。

### 坑 3（P1）：GC 零分配判据的两个污染源（都被 16 帧采样窗抓出）

- `ScriptHost::GcAllocated` 每次采样都 `GetExport` 查导出指针——**每次调用在托管侧
  分配 ~8.2KB**（违反自家"启动期一次取全"纪律）。修复：指针缓存成员。
- tiered JIT 分层记账：缓存修复后默认分层下仍 9/10 进程出现 ~8.2KB×N 次非零增量
  （30k×300 帧实测 3 次）。处置：bench `main` 起点先于 CoreCLR 初始化
  `setenv("DOTNET_TieredCompilation","0")`（Tier1 全优化从头编译，120 帧预热后与
  分层稳态码质等价）。编辑器/游戏进程不受影响。
- 另修 csNet 口径：#14 profile 均值曾含 120 帧预热的 Tier0 慢帧（csNet > 整步 avg 的
  不可能值暴露问题）→ 预热后差分。这正是此前 cpp-compare 比值噪声大（1.27–2.35×）的主因。

### 验收终测（Release / 6C / .NET 10.0.12，方法见 09 §6.9）

| 判据 | 结果 |
|---|---|
| 5k 弹整步 ≤8ms | avg **0.248ms**（18000 帧全程） |
| C# 净时比（ADR-010 D5 分档） | 5k/10k/30k/100k = 1.92/1.67/1.52/**1.43×**；边际比 1.41×；固定往返 ~66µs |
| 确定性回放 | 18000 帧 × 双档（--threads 1/4）mismatches=0 |
| 毒脚本 | 恰 60 条红字后自动禁用，引擎不崩 |
| 托管分配 | **0 B / 18000 帧**（15/15 进程硬 0） |
| 断点通路 | 诊断 IPC socket + `Lemon.Domain` 线程名 + mac-debug 全量测试通过 |
| 回归 | engine-tests 12830 + script-tests 1264 + M2 bench-sim 重录金档双档 PASS（threads=1/4 mismatches=0；多线程档 avg 3.21ms） |

判据修订入 [ADR-010](./ADR/ADR-010-M3-Scope-Thread-RNG.md) D5/D6；汇总入
09 §7.6。其余已留档的坑（NativeWrite 对已有组件二次 emplace 损坏 entt 池、
C# 静态字段文本序初始化假哑火、ScriptAlc 类型身份唯一性）见对应代码注释与
M3-2b 诊断记录。

---

## 2026-09-19 · M3.5 集成冒烟 anim-smoke（ECS→Extract→窗口 首次打通 + 帧动画机制视觉级验收）

M4 前置预验证：SystemPipeline 的 **Extract 阶段自 M2 预留以来首次被真实消费**
（SpriteExtractSystem：Transform2D+SpriteRenderer → RenderableManager 全量同步，渲染侧
RunStage 驱动），同时验证帧动画核心机制（spriteId 切换 → Atlas UV → 换帧）。零外部素材
（程序化 16 帧 64px 单页图集 1024×64，槽0；字体页槽1）。两侧对照：左组 C++ 通路
（AnimatorSystem 推 time → 冒烟本地 FrameMapSystem 写 spriteId，M5 clip 表前的占位形态），
右组 C# 档① FrameScript（NativeApi Read/Write SpriteRenderer），底部 16 帧静止胶片条作
帧映射对照尺。

### 验收数据（Release / AMD RX 590 / MoltenVK，--validate 全程零报错）

| 判据 | 结果 |
|---|---|
| 300 帧 vsync 稳定 | fps 60.0（--no-script 档 61.9），step=0.13ms，extractStage=0.14ms |
| 提取完整性 | visible 28/28（6+6+16），batches=2（精灵+文本各一） |
| 换帧推进自检 | 每秒采样 probe spriteId：cpp=[11,5,15,…] 与理论帧序（0.625s/s→帧 10/4/14+基1）**逐点吻合**；cs=[11,5,15,…] 同速同相位吻合（脚本写回生效） |
| 系统净时 | Animator 0.0009ms / CppFrameMap 0.0010ms / SpriteExtract 0.0036ms / CSharpBatch 0.066ms（max 18.9ms 为首帧 JIT） |
| 双档 | --validate 与 --no-script 双档 exit OK；画面人工确认（追逐队形/胶片条对齐）待跑一次 `lemon-anim-smoke` 观看 |

### 过程问题全记录

1. **【P1·环境坑】验证层 LAYER_NOT_PRESENT(-6)：裸 library_path × 目标缺 RPATH。**
   首跑 `--validate` 即 `vkCreateInstance` 失败 OTHER(-6)，且与 CoreCLR 无关
   （--no-script 同炸）。`VK_LOADER_DEBUG=all` 三进程对照定位：brew 验证层 JSON 的
   `library_path` 是**裸文件名**，loader 按裸名 dlopen（搜索路径只有 CWD/Cryptexes/
   /usr/lib/dyld 缓存），能否命中全看二进制 `LC_RPATH /usr/local/lib`（brew 的 dylib
   符号链接在此）——bench-sprites 有（loader 经 dladdr 打印 Cellar 真实路径），anim-smoke
   没有 → 炸。**根源：6 个开窗口 sample/spike 的 CMakeLists 全带
   `BUILD_RPATH/INSTALL_RPATH /usr/local/lib`，headless 的 bench-script/bench-sim 不带；
   新写窗口 sample 时照抄了 headless 模板。** 处置：anim-smoke 补齐 + 注释机理。
   约定升级：**开窗口 target 必带此 RPATH**（M4 编辑器 CMake 直接继承本条，或收进
   lemon_add_sample 公共函数）。
2. **【P2·引擎设计确认】SystemPipeline 拒绝同名系统实例**（LEMON_ASSERT
   "duplicate system"，SystemPipeline.cpp:19）。冒烟最初注册两个 OrbitMotionSystem
   （左右组各一）被拒——系统名 = 身份键（profile 查找/RNG 子流 id/After 依赖名），
   同逻辑双实例天然冲突。设计合理，不改引擎；冒烟改单实例多组（Group{team,center,
   radius,speed} 表驱动）。留档：同系统多实例需求要么拆名要么组表化。
3. **【API 语义】Scene::View(...).each() 解包出 entt::entity，无 .id**——Sample 侧取
   句柄必须走 `Scene::FromEntt(ent).id`（封装层静态转换；entt 类型不得越过 ECS 层）。
   首编即抓，一次修复。
4. **【风险确认·未踩】档①-only 程序集的 LayoutTables 绑定路径**：本程序集 Configure
   只 Behaviours.Register 不 Scripting.Register，ComponentTable 绑定依赖
   DomainManager.LoadScript 先调 `Lemon.Scripting.Reset()` 触发静态构造（M3-7 兜底
   路径）。实测可用；若未来 Entry 删该调用，`GetComponent<T>` 将在 ComponentTable.Id
   抛 KeyNotFound。留档待 M4 复核（可在 SDK 布尔哨兵处加显式断言）。
5. **【设计落点】Extract 阶段消费形态（M4 SceneView 地基基线）**：Extract 阶段系统由
   **渲染侧** `RunStage(Extract)` 驱动（World::Step 只跑 Essential+FixedTick）；vsync
   1:1 下 alpha=1.0 取本 tick 精确态（bench 的 0.5 是满帧率插值路径，两者语义不同）。
   Entity→renderable 映射 unordered_map 惰性建，静态实体池全量刷新 0.0036ms；**M4
   需补：实体销毁 → rm.Destroy 释放路径、增量/脏标记、多 Scene 切换时的映射失效**。
6. **【测试缺口】画面级验收的自证边界**：无窗口捕获权限（Quartz 绑定缺失），截图验收
   未做；以帧推进采样自检（与理论帧序逐点吻合 + STALLED/ADVANCING 硬判定，exit 1 兜底）
   作为 headless 客观证据，观感（动画流畅/胶片条对齐/文本标签）留人工一次跑确认。
7. **【存量·顺手记录】SDK TestScript.csproj 两条 CS9196 警告**（`in Chunk` vs
   `ref readonly Chunk` 接口签名不匹配）——非本次引入，M4 清理清单 +1。

### 追记（同日）：anim-smoke 截图验收揪出引擎级潜在 Y 镜像（问题 8 + 修订）

用户人工截图确认布局/动画/换帧全活，但**整体垂直镜像**：胶片条（world y=600 设计在
底部）渲染在顶部、格内进度条翻到格顶、全部文本倒印。单根因定位：

- **机理**：`Math.h::Ortho` 按 GL 语义实现（注释自述"世界 Y 向下 → NDC Y 向上"），
  而 Vulkan NDC 是 Y 向下（-1=顶，+1=底）。世界下方被映射到 NDC -1 = 屏幕顶 = 整体
  镜像；四边形随位置翻转 + UV 不动 → 字形必然倒印。bench 全是对称轨道运动（旋转/
  圆周/粒子）且 bench-mow HUD 从未被人工目检过 → M1 起潜伏至今，anim-smoke 首个
  "非对称内容 + 有人看画面"的 sample 一发命中——**冒烟的价值正在于此**。
- **修订**：`Math.h::Ortho` 改 Vulkan 语义（sy 取正、ty 取负；世界下方 = NDC +1 =
  屏幕下方）；`engine_tests` 两处断言同步（ortho y-flip → y-down；Camera2D 同），
  12830 checks 全绿；02 §3.5 补坐标系约定条目。回归：bench-sprites / bench-mow
  （VERDICT PASS）复跑无回归（对称内容视觉不变），anim-smoke ADVANCING 双档 OK。
- **教训**：①"GL 坐标系直觉"直接搬到 Vulkan 是跨 API 移植的经典暗坑，数学层注释
  与实现自洽但与平台语义矛盾——单测只能锁实现，锁不住意图；②渲染约定必须有
  **非对称锚点**（文字/进度条/编号）进 GUI 验收清单，对称内容对镜像/旋转类缺陷
  天然免疫；③ M4 SceneView 接入前此项已清零。

---

## 2026-09-19 · M4.4 资产管线 + AssetBrowser + Prefab 最小集 + C# SDK 增量

按 M4-Editor-Plan §5 M4.4 实施。新目录 `Editor/Assets/`（AssetDatabase/AssetGpuCache/
FileWatcher）；构建分层 `lemon-editor-core`（无 ImGui 编辑器逻辑，lemon-tests 入测）。

**资产管线（06 §2 最小导入集）**：

- **AssetDatabase**（GUID/.meta/`.lemon/manifest.json`）：.meta 随文件走（重命名引用
  不断）；spriteId 持久记账（manifest `nextSpriteId` 只增不减，跨会话稳定）；删除 =
  墓碑（号保留，引用悬空红字，重启不回收——编号连续性是"AtlasRegistry 追加式 id ↔
  DB 分配号"对齐前提）；体检红字：孤儿 meta / GUID 冲突 / 缺失引用（LEMON_ERROR 进
  Console + smoke errors 计数）。FNV-1a 内容哈希做变更检测。
- **AssetGpuCache**（stb_image 解码只进此 TU）：每 PNG 独立纹理页（bindless 槽 2..，
  上限 64 槽 = M4 最小集约束，图集打包 M6 解）；一页一全幅 sprite；导入顺序 =
  spriteId 升序 → 与追加式登记天然对齐；漂移自愈（记账号 ≠ 登记号 → 以登记号重指 +
  红字）。缩略图 = 纹理经 ImGuiBackend 注册（Inspector 槽/Browser 网格同源）。
- **FileWatcher**：线程 500ms 快照轮询（Luma 同款语义）；置脏 → 主线程 RescanAssets
  增量导入。热重导入双分支都实测：同尺寸 = 原位重传（UploadTexture staging 同步）；
  尺寸变化 = WaitIdle 销毁重建同槽 + `AtlasRegistry::UpdateAtlasPage`（全幅 sprite
  像素尺寸随之刷新，uv 0..1 不变）。
- **AssetBrowser**：目录下拉 + 缩略图网格（sprite=纹理/其余=调色板页色块图标）+ 悬浮
  guid/尺寸 + 拖拽（"LemonAsset" 载荷：sprite 进 SceneView=建实体、进 Inspector 槽=
  设引用；prefab 进 SceneView/Hierarchy=实例化）+ 双击建实体 + 右键导入/重命名/删除/
  复制 GUID。
- **Inspector**：SpriteRenderer.spriteId 换资产槽（`FieldHint::AssetRef`，缩略图 +
  下拉 + 拖拽 + 右键清空；悬空引用红框 ⚠）；Prefab 头栏 Apply/Revert/Break（§3.9
  最小集；Revert 保持实例 guid，选中集不丢）；ScriptBox 段（类型下拉 + Add Script
  按注册类型列表 + 移除）。

**Prefab 最小集（§3.9）**：`SceneArchive::SaveEntityTree/LoadEntityTree` 公开 API
（引擎侧，实体段 codec 与 .scene 共用；跨树 EntityRef 置空、实例 guid 全换新）；
Hierarchy 右键"Prefab 化"导出 `Assets/Prefabs/<tag>.prefab` + Meta.prefabId 回链。
落位偏差记录：prefab 文件放 `Assets/Prefabs/`（06 §1 布局的根级 Prefabs/ 目录留
M4.5 项目向导一并落地，DB 单根扫描简化 M4.4）。

**C# 侧（内核 #7/#8）**：

- ScriptBox 拆独立头 + 扩 `scriptGuid`/`className[24]`；.scene 实体增 `"script"`
  成员（typeId 注册序不持久，className 是持久键，装载后宿主按名解析）。
- NativeApi 表尾 +4：getInput / spriteOfGuid / spawnSprite / instantiatePrefab
  （旧宿主零扰动；编辑器资产钩子 `SetEditorAssetHooks` 进程级注入，纯运行时 = 0）。
- SDK 新增 `Lemon.Input`（Axis/GetButton/Up/Down/Left/Right/Attack）、`Lemon.Assets.
  SpriteOf(guidHex)`、`Lemon.Instantiate.Spawn/Prefab`（Spawn 即时建实体，语义 =
  命令缓冲跨帧可见的一致性）；`Behaviours.RegisteredNames/TypeIdOf`；新导出
  `lemon_behaviours_list`。
- 编辑器 `--script <dll>`：CoreCLR 宿主 + EnterPlay 装配（ScriptBox.className →
  typeId → AttachBehaviour；playWorld SetScriptBackend）。

### 验收数据（Release / AMD RX 590 / MoltenVK / --validate）

| 项 | 结果 |
|---|---|
| engine-tests | **12935 checks 全绿**（+63：Atlas 页热更新/AssetDatabase 生命周期/实体树档案/ScriptBox 档案/EditorContext Prefab 操作端到端） |
| script-tests | 1268 checks 全绿（SDK 表尾扩展 + 新 API 零回归） |
| imgui-isolation | PASS（AssetGpuCache 等新 TU 不含 imgui.h；lemon-editor-core 整层无 ImGui） |
| 编辑器基础冒烟 | 120 帧 errors=0（无项目路径也全功能） |
| 资产链冒烟 | `--project --smoke --frames 240`：固定 guid 资产在库 spriteId=104、页 96×48（中点改写落盘 → watcher → 重导入）、缩略图注册、errors=0 PASS |
| 脚本链冒烟 | `--project --script TestScript.dll --smoke --play --frames 240 --validate`：**playAlive=42（播种 6 + SpawnerBehaviour 36 只，走 Assets.SpriteOf(固定guid)→导入 sprite）**；Play 进/出 3.4/0.6ms 逐字节一致；viewportVisible=41；验证层零报错 PASS |
| 热导入双分支 | 尺寸变化（64×64→96×48，页重建+UpdateAtlasPage）与同尺寸（96×48→96×48，原位重传）各实测一轮，验证层干净 |
| 场景 IO | 带 script 段保存/重开 roundtrip PASS（`"script":{"class":"SpawnerBehaviour","guid":0}` 持久） |
| 回归 | anim-smoke 300 帧 exit OK / bench-script PASS（GC 稳态 0B） |

### 过程问题记录

1. **【实测坑】DomainManager.LoadScript 要求绝对路径**（ALC LoadFromAssemblyPath
   约束）——编辑器 `--script` 相对路径首跑炸 ArgumentException；EditorApp 侧
   `filesystem::absolute` 归一后修复。
2. **【潜伏 bug 顺手修】设备丢失路径 OnDeviceRecreated 未 Reset AtlasRegistry**——
   重 Build 直接 RegisterAtlas 会撞"槽位重用"断言（M4.2 起潜伏，仅真实 device loss
   触发）。修：Reset → 程序化页重建 → asset-gpu 回调按 DB 记账号升序重导入接续编号。
3. **【语义决策】删除资产不销毁 GPU 纹理**——销毁会使仍指向该 spriteId 的场景实体
   采样悬空描述符（登记号仍在 AtlasRegistry）。改"幽灵页"：纹理保留到重启、DB 墓碑
   红字、浏览器隐藏；ViewportRenderer 提取层对越界 spriteId 跳过建 renderable
   （GetSprite 断言的编辑器侧防线）。
4. **【测试工程】guid 数字位数不定**——实体树二次导出的文本 size 比对误报（十进制
   位数随值漂移）；改数据级断言。场景名是宿主属性，不动点测试需同名场景（既有
   TestSceneArchive 已注明，新测试照办）。
