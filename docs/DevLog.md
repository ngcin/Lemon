# Lemon 开发日志（bench 数字与事件记录，08 §6）

> 纪律：每步验收的实测数字记此处；性能回退 >10% 标红。日期均为 2026 年。
> 测试方法与判读见 `EngineDesign/09-Testing.md`（本文件记数字流水）。

---

## 2026-09-20 · 【P0 发现】overlay 渲染通道缺陷——网格/选框/Gizmo/标签从未画出

M4.7 规划讨论中用户反馈"灰色界面完全没有网格线"，与代码行为（GridSnap 默认开）
矛盾，追查确认为**绘制侧缺陷**而非观感问题。诊断细节与假设排序记
[M4.7-Editor-UI-Polish-Plan.md §2.6](./EngineDesign/M4.7-Editor-UI-Polish-Plan.md)。

**证据链**（探针已还原，工作区零代码改动）：

1. `--smoke --screenshot` 截图：网格/选中选框/Gizmo 手柄/实体名标签全部缺失，
   精灵正常显示。
2. Render() 埋点：每帧 `overlay=68` 包、`batches=3`、`instances=90`
   （6 精灵 + 68 overlay + 16 文本）——**推入与 Bake 侧正常**。
3. 极限对照：网格临时改为不透明纯黄 20px 粗线，截图仍无任何线条——排除
   α70/线宽/配色，缺陷在绘制侧。
4. 交叉观察：仅批 1（instanceOffset=0，精灵）可见；批 2（offset=6，overlay）、
   批 3（offset=74，文本）全部不可见——**指向非零 instanceOffset 的实例寻址断裂**
   （`pc.baseInstance` 与 `DrawQuadInstances` 第二参语义不闭环为最高嫌疑）。

**教训**：此缺陷自某次 M4.2 后变更潜伏至今，`tools/editor-regression.sh` 9/9 PASS
照旧——自动化只断言精灵渲染与数据正确性，**渲染结果可见性（overlay/文本要素）无
任何自动防线**。M4.7-P0 修复后须把"`--screenshot` 四要素可见性"入冒烟断言。

---

## 2026-09-20 · 编辑器使用测试指南 + 一键自动化回归（9/9 PASS）

**`docs/EngineDesign/Editor-Manual-Test-Guide.md`**：按工作流分区（A 启动/项目 →
L 退出状态机）12 区 70+ 项，每项 操作步骤 + 预期 + 自动化覆盖标记（`[自动]`/
`[手测]`）；§3 登记已知观察项（spriteId 漂移自愈/dotnet 阻塞/ALC 泄漏）防止当缺陷
误报；§4 运行记录表。`[手测]` 项 = 真人专属路径（OS 拖拽/IME/Gizmo 手感/模态观感，
决议 R5 无 UI 录制回放）。

**`tools/editor-regression.sh`**（quick|full [build-dir]）：ctest + 基础冒烟 +
`--smoke-close` 双态 + 资产链 + 脚本链（缺 dll 自动 SKIP）+ `--final` + 场景 CLI
roundtrip（`--save-scene` → `--scene` 重开），末尾 PASS/FAIL 汇总（exit code 即判据）。
本轮实跑：full 9/9、quick 4/4 PASS。

**脚本坑**：macOS /bin/bash = 3.2——`$MODE，`（变量名紧跟全角标点）会把高位字节
并进变量名报 unbound（C locale 高位字节可入 name 判定）；全部展开改 `${BRACED}`
规避。参数解析一并修正（单参 quick 曾被赋给 BUILD）。

---

## 2026-09-20 · M4.6b 真用手测第二击：team 下拉未命名档位撞 ID（"4 visible items"）

用户报"Meta 的 layer 报错"（ImGui 冲突弹窗恰盖 layer 字段）——实际根因在上方 team
下拉：`TeamName(4..7)` 未命名档位统一返 `"?"`，下拉一开即 4 个同名 Selectable 同
ID → "4 visible items with conflicting ID"。冒烟悬停扫掠不点开下拉 → 测不到（真人
路径教训第三次）。修：TeamName 只返语义名，序号前缀由调用方拼（"4 ?" 互异）。
同类排查：sprite 资产槽下拉列表项 FileName → relPath（不同子目录同名文件同 ID 的
潜伏同款）。回归：基础/资产链冒烟 errors=0。

---

## 2026-09-20 · M4.6b 真用手测第一击：工具栏按钮点击撕裂 Push/Pop 配对

M4.6b 提交后真人首次连用即暴露（Console 连报 `PopStyleColor() too many times` ×
点击次数）：工具按钮循环里 `if (t.tool == tool_) Push … Button（点击改 tool_）…
if (t.tool == tool_) Pop`——**点非当前工具按钮的那帧，Push 判定在点击前（不推）、
Pop 判定在点击后（弹出）**，无配对 Pop。M4.0 起潜伏，冒烟悬停扫掠不点击 → 永测
不到（M4.6 教训"只有真人点得到"再现）。修：高亮判定先落局部 `active`，与点击
变异解耦。全 Editor 扫描同类"条件式 Push + 中间变异状态 + 条件式 Pop"仅此一处
（Profiler/Inspector 的条件配对中间无变异）。回归：基础冒烟 120 帧 errors=0。

附带观察：同屏 `spriteId 记账漂移：记 105 实得 104` 红字 = M4.6 §6 已登记观察项
（manifest 记账 × 追加式登记号漂移 → 红字自愈重指，M5 评估 guid 间接化），非本轮
回归，行为符合设计（自愈后引用已按新号重指）。

---

## 2026-09-20 · M4.6b 日常编辑效率七件套（复制粘贴/拖拽导入/新建脚本/编译反馈）

按 M4.6 §5 实施（M4.6a 次日收尾轮）。全部 ImGui/SDL/STL 既有能力，零新第三方（R4）。

**七件落地**：

- **实体复制粘贴（Ctrl+C/V）**：选中子树的根（祖先也在选中集内的跳过）→
  `SceneArchive::SaveEntityTree` 多根入内部剪贴板 + 各根原位置；粘贴
  `LoadEntityTree`（guid 全换新）+ 根 +24/+24 相对偏移（连续粘贴不叠死）、进结构
  Undo；Edit 态专属（Play 中禁用，与 Undo 轨一致）。Ctrl+D 原样保留。
- **资产右键重命名/删除**：M4.4 时 UI 已接（本册 §3 清单误记"未接"）——本轮验收
  确认引用不断/墓碑红字行为不变，未动代码。
- **外部拖拽导入**：`Window::TakeDroppedFiles()`（SDL_EVENT_DROP_FILE 即拷即存——
  `drop.data` 仅事件期内有效）语义层接口 → 落 AssetBrowser 当前浏览目录（面板不可
  见 = Assets/ 根）→ `ImportFile`；重名自动加序号**不覆盖**（拖同名文件静默覆盖旧
  资产太危险）；无项目可操作红字。平台差异按 §7 登记 07 §3.5（Windows 盘符路径
  验证点）。
- **新建脚本菜单**（Assets 菜单）：类名弹窗 → `ProjectWizard::AddBehaviourScript`
  模板 .cs 落 Game/ + GameMain.cs 注册锚点前插 `Register<>` 行（与终验
  EditProbeBehaviour 同款锚点手法，不动 Configure 签名）→ 编译队列热重载 → 类型
  可挂。模板真机 dotnet 编译 0 错误；非法类名/重名拒绝（单测覆盖）。
- **编译状态提示**：排队制——触发只置 `compileQueued_`，当帧状态栏画"编译中…
  （dotnet build）"并 present，**下帧**才真正阻塞构建（阻塞 1–2s 期间屏幕留提示
  帧），完成后状态栏回显"上次编译 Nms"。主线程阻塞现状不动（§6 观察项）。
- **Console 编译错误解析**：`BuildGameProject` 改 popen 捕获合并输出（64KB 上限）；
  `ExtractCompileErrors` 提取 `file(l,c): error CSxxxx` 行（去 `[csproj]` 尾巴、
  50 行上限，纯函数单测）→ 编辑器侧裁项目根前缀后红字进 Console。真机 dotnet
  输出格式对照一致（`/abs/Game/A.cs(6,17): error CS1525: ... [..csproj]`）。
- **FilePicker 手输 + 快捷钮**：绝对路径回车直达（目录=进入 / 文件=进父目录并填
  文件名，不存在=告警回显）；Home/项目根快捷钮每帧随动（切项目后随动）。

**防重复编译小纪律**：新建脚本路径写完源即 `ScriptSourceChanged()` 吸收基线再排队
（否则 watcher 500ms 后必二跑 dotnet）——所有非 watcher 文件写路径同理。

### 验收数据（Release / AMD RX 590 / MoltenVK）

| 项 | 结果 |
|---|---|
| engine-tests | **12986 checks 全绿**（+51：ExtractCompileErrors 格式/边界 + AddBehaviourScript 模板/锚点/非法名） |
| ctest | 3/3（engine-tests / imgui-isolation / script-tests） |
| 编辑器基础冒烟 | 120 帧 errors=0 PASS（冷启 535ms） |
| 资产链冒烟 | `--project --smoke --frames 240`：spriteId=104、页 96×48 热替换、errors=0 PASS |
| 脚本链冒烟 | `--project --script --smoke --play --frames 240 --validate`：playAlive=42、Play 往返 3.8/0.6ms 逐字节一致、errors=0 PASS |
| 终验 `--final` | 620 帧全 OK：热重载 play=1283ms/edit=1249ms、stateBag 66/66、fps 59、冷启 345ms、autosave 链 OK |
| 关闭状态机回归 | `--smoke-close clean/dirty` 双 OK（主循环改动零回退） |
| 新脚本链 | 模板+注册行真机 dotnet 编译 0 错误；换装链同终验机制（1249ms ≪ 10s 判据） |

### 遗留与观察

- UI 观感项（拖拽入列动画、"编译中"帧的视觉时长感）待真人手测清单（M4.6 §5 末）。
- dotnet build 主线程阻塞维持现状（M4.6 §6 观察项，>2s 频繁再异步化）。

---

## 2026-09-20 · 会话内二次装配闪退闭环（宿主进程单例化）

用户二次实测崩溃（SIGSEGV @0x19，栈 = ProfilerPanel → GcAllocated →
CoreCLRHost::GetExport → 垃圾函数指针）。根因三层（commit `3cb6b6f`）：

- **事实**：CoreCLR 进程单例——同进程二次 `ScriptHost::Initialize` 必失败
  （script-tests 永久探针钉板 `second-host init=0`，进程存活）。
- **缺陷**：`InitScriptHostFrom` 二次执行先 `make_unique` 毁旧宿主 →
  `ctx_.scripts_` 悬空 → 二次 Initialize 失败 → `host_.reset()` → 悬空永久化
  → Profiler 每帧 `ctx.Scripts()->GcAllocated()` 踩已释放内存（0x19 = 小整数
  垃圾，典型 use-after-free 形貌）。
- **修复**：宿主复用（已有 host → `HotReloadAssembly` A 线换装至新程序集；失败
  转无脚本态）+ 不变量"ctx 指针先清再动宿主"（管线无 Game 分支同步）。副产：
  `--script` 与项目 Game/ 并存原本二次 init 必失败（M4.5 起静默无脚本），现
  复用换装正确生效。

验证：env 临时探针端到端驱动二次装配（换装 ok + GcAllocated 返回 158056 +
exit=0）后移除；ctest 3/3；M4.4 冒烟 / `--final` / smoke-close 全绿。

**当日两连修教训（编辑器会话生命周期纪律）**：CoreCLR 宿主 = 进程单例，编辑器
会话内任何"再装配"都必须走换装而非重建；凡经 native 导出持有/回调的托管指针
（ctx 指针 / 缓存导出函数），销毁侧必须先摘引用。

---

## 2026-09-20 · 向导创建后闪退闭环（相对路径 × 误触发 × 异常逃逸 三连缺陷）

用户实测报错（`Unhandled exception. System.ArgumentException: Path "./demo/test/.lemon/bin/test.dll" is not an absolute path` → abort）无头复现 exit=134 后逐层闭环（commit `35c4c3a`）：

1. **触发层**：FileWatcher"首拍不算"条件写反（`!snap.empty()` 分支——有文件的目录
   首拍必置脏）+ `lastHandledCsWrite_` 从 0 起步把"项目自带 .cs"当"变更"——两个
   误触发源叠加 = **每次打开项目必做一次无谓换装**（泄漏一个旧域；冷启 3151→1874ms）。
   修：primed_ 首拍基线 + 开项目时 ScriptSourceChanged() 基线化。
2. **路径层**：向导父目录手敲 `./demo` → 项目根相对 → 初始装载（InitScriptHostFrom）
   有绝对化、热重载（HotReloadAssembly）没有 → `LoadFromAssemblyPath` 抛参数异常。
   修：DB 入库即绝对化 root_ + 向导父目录绝对化 + HotReloadAssembly 兜底绝对化。
3. **拦截层（致命一环）**：`UnmanagedCallersOnly` 导出未 try/catch——托管异常逃逸到
   native = coreclr 直接 abort 整个编辑器。修：dm_load/unload/reload 导出 +
   DomainManager 全域生命周期方法 try/catch 转 false；`weak.Target!` 空解引用一并
   防护。**纪律沉淀：经 native 导出直通的托管代码，异常就地转返回值，永不逃逸。**
4. **回归层**：script-tests 永久用例（相对路径 dm_reload 返回 0 且进程存活 → 真路径
   恢复换装 → tick 复常）。

排查过程记档：崩溃栈 unresolved managed 帧 → 先无头复现（相对路径 + 带帧限跑）拿到
同栈 → Post:81 `throw cmd.Error` 定位逃逸口。recent.json 曾写入相对条目（崩溃前
管线已成功）——改记 DB 侧绝对 root_，用户文件已修正。

**回归**：ctest 3/3（含新用例）；M4.4 冒烟 600 帧 errors=0（中点 PNG 热替换不受
基线化影响）；--final 全 OK（stateBag 66/66 fps 59）；smoke-close 两模式 OK；
相对路径开项目零误触发零崩溃、中途 touch 真实换装 1 次正常。

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

## 2026-09-21 M4.7 编辑器 UI 精美化（P0 + a + b + c 全批次）

触发与方案：`docs/EngineDesign/M4.7-Editor-UI-Polish-Plan.md`（定稿 v1）。

### P0 overlay 渲染通道修复（§2.6）——两个叠加缺陷，均实证定位

1. **overlay scale 语义错位**：`PushOverlayQuad` 误将 size 除以 64px 单元格
   （全引擎约定 = 世界像素边长，见 Extract/DrawText），overlay 整体缩小 64 倍
   （1.5px 线宽 → 0.023px 亚像素）。
2. **跨合批器 SSBO 描述符竞态（主因）**：`boundStorageBufferId` 设备级去重 +
   UPDATE_AFTER_BIND"执行期取最新值"——同帧 scene.Record → game.Record 改写
   binding 2 后提交，**scene 视口全部绘制读的是 game 环**。批 1（offset 0）
   恰与 game 环前 6 个精灵同位故可见；批 2/3（offset 6/74）读到从未写的零数据
   → 退化零尺寸四边形全灭——与计划实验 #4"只有首个批可见"完全吻合。
   **对照实验**：交换两视口渲染顺序 → 四要素全部显现，假设坐实。
   **修复**：binding 2 升级为 4 槽 SSBO 数组（每合批器固定一槽，push constant
   `ringIndex` 索引，帧内零改写）；shader/RHI/SpriteBatcher 三侧同步。
3. **冒烟像素防线**（防再穿透）：`--smoke` 末帧回读场景 RT（新增
   `DebugRecordTextureCapture`，线性空间无 UI/sRGB 干扰），扫描四要素特征色
   （网格灰带/主选青/手柄黄/标签墨），计数入 exit code。复现竞态时四项全 0 →
   FAIL，防线有效。
4. **顺手修真 bug**：`PruneSelection` 恒按编辑场景校验 + Inspector 每帧调用
   → Play 中任何选中下一帧被误清（Play 无法选中/检视实体）。改按 ActiveScene 校验。

### M4.7a 主题 token + 工具栏三段式

`Tooling/Theme.h/.cpp` 单点（决议 D2：中性 Unity 深色 + 蓝强调；旧柠檬黄全撤）；
工具栏三段式（左 W/E/R+GridSnap｜中 Play/Pause/单步按实宽精算居中｜右预留）；
Play 态色 D3（编辑灰蓝/Stop 红）；Console 分级计数徽标；全库 ~15 处散落
ImVec4 字面量收编 token。

### M4.7b 图标体系（决议 D1：自绘 16 枚，零新依赖）

程序化形状页（图集槽 2，512×32：16×32px 白形状，4× 超采样软件光栅化）→
`Tooling/Icons.h`（IconButton/DrawIcon）。工具栏 7 键、Hierarchy 行前类型图标
（prefab 蓝/带脚本/精灵/空实体）、AssetBrowser 类型图标（替代旧调色板色块）。
资产导入槽位从 2 → 3 起（图标页占用，撞"槽位重用"断言后修正）。

### M4.7c 视口交互 v2 + 网格 v2 + GameView Aspect

- **一段式拖拽（D5）**：mousedown 命中即选中并 arm，位移 ≥4px 才真正改
  Transform（点 vs 拖二分）；纯点击不入 Undo。
- **Move 轴约束**：X 红/Y 绿箭头（Unity 语义）独立命中（优先于实体本体），
  中心块自由拖；hover/拖拽轴提亮 + 光标变化（EW/NS/手型）。
- **Esc 取消拖拽**：恢复起点快照，不入 Undo。
- **hover 轮廓**：未选中实体悬停亮边（框选 M5 铺底）。
- **网格 v2**：zoom 自适应密度（屏幕 14–72px 区间内翻倍/减半）+ 两级网格
  （minor 常显/major 每 4 格提亮）+ 世界主轴高亮 + 原点标记。
- **GameView Aspect 下拉**：Free/16:9/4:3/1:1 letterbox（原固定 16:9）。

### 回归红线（全过）

ctest 3/3；`tools/editor-regression.sh full` **9/9**（含 overlay 像素断言全路径）；
验证层零报错；基线图 `docs/Baselines/m4.7-editor-ui.png`。

## 2026-09-21 M4.7 手测首击：两个真用缺陷（崩溃 + 拖拽全灭）修复

用户手测报告两题，其一竟是自 ImGui 1.92 升级起就存在的静默全灭。

### 缺陷 1：空实体 + 加 SpriteRenderer → 选中即断言崩溃

`ASSERT Atlas.cpp:70 bad spriteId`。根因：`ViewportRenderer::WorldBoundsOf` 的
守卫写成 `spriteId < SpriteCount()`——把 `spriteId == 0`（槽位未设，合法态）放
过去直撞 `GetSprite(0)` 断言。引擎侧 `RenderableManager::Extract` 同样未守卫
（运行时路径同炸）。对齐 Unity 语义（SpriteRenderer 挂任意 GameObject、
Sprite=None 即不渲染）：两处守卫改 `spriteId != 0 && spriteId <= SpriteCount()`
→ 无 sprite 实体不渲染、选框落 24px 占位框。验证：手工构造 `spriteId=0` 首位
实体的场景 `--scene` + `--smoke` 强选路径（曾经的崩溃路径）现在 PASS（sel=307
来自占位框轮廓）。

### 缺陷 2：视口 Gizmo 拖拽/缩放/旋转全部无效（连滚轮缩放也死）

静态审查状态机无果 → 新建 `--smoke-drag` 注入模式（ImGui 事件注入模拟真实
按下/拖动/释放 + 链路分段计数 + 位移/角度断言），一次跑出真凶：

- **根因**：`canInteract = hovered && !WantTextInput && !WantCaptureKeyboard` ——
  ImGui **1.92 改了 WantCaptureKeyboard 语义**：有任意窗口持有焦点即真（编辑器
  UI 内恒真，实测 activeId=0、无文本输入仍 kb=1）。视口点击/滚轮缩放自 1.92
  升级起从未活过；M4.7c 手柄变显眼后终于被手测抓到。修复：视口两处（拾取门 +
  滚轮门）撤下该条件，输入态由 `WantTextInput` 表达（与 GameView 输入路由同
  口径）。
- **次因**：无项目时"尚未打开项目"中央卡（400px）悬在视口中心 = 实体聚集区，
  截走 hover/点击。加 `×` 关闭按钮（会话内不再弹）。
- 顺带修 `Renderable.cpp` 守卫（见缺陷 1）、确认子实体本地坐标陷阱在驱动侧。

### --smoke-drag（新回归线，防再犯）

注入链：帧 3 选根实体+关吸附+居中相机 → 9 按下（体内点，避手柄带）→ 10-20
拖 44pt → 21 释放 → 断言位移 = 44pt×世界/点；24 切 Rotate → 28 按下于 45° 弧
点 → 29-38 沿圆弧 −90° → 39 释放 → 断言 Δrot=−π/2。诊断计数（press/armed/
activeEver/updates）打进裁决行，失效可分段定位。接入 editor-regression.sh。

### 回归红线（全过）

ctest 3/3；`tools/editor-regression.sh full` **10/10**（9 项 + smoke-drag）；
smoke-drag 与 empty-sr 场景 `--validate` 零报错。

## 2026-09-21 M4.7 手测第二轮：滚动条/网格 v3/缩放手感/Select 模式

### 1. 滚动条清理（外层 + Hierarchy 空表常驻条）

- Hierarchy 树区域曾挂 `AlwaysVerticalScrollbar` → 空列表也常驻滚动条，改按需出现；
  面板窗口本体挂 `NoScrollbar`（滚动只发生在 tree 子区）。
- 外层宿主 `##LemonEditor` 与 Scene/Game 窗口挂 `NoScrollbar | NoScrollWithMouse`：
  内容溢出的 1–2px 曾触发最外层滚动条；更重要的是**滚轮会从 Scene 子区冒泡到
  可滚动的祖先窗口**（ImGui 行为）——把整个布局顶走 = "缩放后无法操作"的真凶。

### 2. 网格 v3（Godot 样式）

全部网格线统一 1px 宽（v2：minor 1.5px + 原点标记 2px 混杂观感乱）；major 仅
颜色区分不加重；原点主轴 Godot 语义 **X 红 / Y 绿**（1.5px 略粗），专用原点十字
/方块标记删除。像素验证：y=0 红轴 RGB(238,148,139) 落屏正确（x=0 默认视野外，
平移即现，代码路径对称）。

### 3. 缩放手感

- 方向反转：滚轮**前推 = 放大**（ImGui MouseWheel 正值 = 前推，原式符号反了）；
- 锚点：有主选中 = **对象中心**（缩放前后对象屏幕位置纹丝不动，像素验证 Δ=0.0px；
  初版误把世界坐标当屏幕坐标传 ScreenToWorld——selΔ=107px 抓出）；无选中 = 鼠标点。

### 4. Select 模式（Q；Godot 式 8 向手柄）

- `EditTool::Select`（Q 键 + 工具栏首位 Cursor 图标，形状页 16→17 枚）；
- 选中实体显示 8 手柄（4 角 + 4 边中点，白圈红点仿 Godot，hover 放大）；
- **拖手柄 = 调整大小**：对侧手柄锚定，新半尺寸 = 鼠标在实体本地轴投影 ÷2（角
  手柄双轴随动/边手柄单轴；旋转感知——本地轴分解，锚点像素验证 Δ=0.0）；拖本体
  = 移动（复用 Move 数学）；
- 8 向 hover 光标（NS/EW/NWSE/NESW）；复用 4px 阈值/Esc 取消/Undo 属性轨/网格吸附；
- 冒烟像素断言不变（网格变细后 35364 ≥ 8000 仍达标）。

### --smoke-drag 扩为四段（防再犯）

移动（3–23）+ 旋转（24–41）+ **Select resize（43–56：右边中点外拖 → scale
1.00→1.53 且左缘锚定 Δ=0.0）** + **缩放（58–63：滚轮 +3 → zoom 1.00→1.43 且
对象屏幕位置 Δ=0.0px）**；裁决行带分段数值。教训入册：注入拖动方向必须沿手柄
本地轴（旋转后沿屏幕轴拖会投影成负值把 scale 压没）；测试驱动量"当前"位置而非
原始位置（相机锚点断言 selΔ 19.8→0.0 的修正）；λ 捕获帧内局部 `cam` 引用跨帧
悬垂（UB，改为 λ 内自取 `viewport_->SceneCam()`）。

### 回归红线（全过）

ctest 3/3；`tools/editor-regression.sh full` **10/10**；smoke-drag `--validate`
零报错。

## 2026-09-21 M4.7 手测第三轮：格子不均 + Transform 天文数字（4.3e7）根治

### 症状同源：resize 爆炸 → 极限缩放 → 网格/断言全露设计缺口

用户手测：格子忽大忽小；8 向拖没几下，Transform2D.pos 到 43555688/1363321。

### 1. Transform 爆炸——Resize 三重缺陷叠加

- **÷2 模型跳变**："新半宽 = 鼠标投影÷2"在 arm 时鼠标≠精确手柄位会瞬间跳 2×，
  且手感是半速拖拽。改**比例跟随**：arm 时记录鼠标相对锚点的本地投影 d0，此后
  hx' = hx0·(d/d0)——arm 零跳变，手柄 1:1 跟随鼠标（Godot 手感）。
- **无上限**：极缩放下 1pt = 数十世界 px（zoom 0.05 时 ≈20），旧模型一帧 +300
  半宽、连拖数次数乘到 4e7。硬钳 scale ∈ [~0.01, 200]（半宽域 [2, 200·单位半宽]）。
- **子实体世界/本地混写**：锚点/半宽按世界算，结果直接写进本地 pos——父链当
  放大器逐次累乘。补父链逆变换：世界意图（锚点 + 本地轴新半宽）→ R(−pw.rot)
  与 pw.scale 逆映射回本地（TRS 复合之逆，ComputeWorldTransform 同约定）。
  8 向手柄仅在**单选**时激活（多选是合体框语义）。

### 2. 格子不均——半像素 AA 吞线

网格线宽 1/zoom 世界 px 但不吸附像素：线骑在两物理像素之间时各得 ~50% 覆盖，
α70 的网格混完后几乎隐形 → 一段段"空带"（缩得越小越密越明显）。网格 v4：
**所有线吸附到 RT 像素中心、宽度恒 = 1 物理像素**——任何缩放下等宽等距等亮度；
网格 α70→110 / major 110→150（仍在冒烟灰带内）；原点主轴 X 红/Y 绿改同宽不透明
（顺带修正 v4 初版把红绿画反）。选框同吃一套吸附（PushOverlayRectSnapped，
旋转框走 AA 不吸附）——final 套件的 sel=0 偶发（1.8px 线 AA 后无像素达 ±10
纯色容差）就此消除。

### 3. 测试稳健化（smoke-drag 连挂引发的工程）

- 注入坐标改**世界锚定、注入帧重投影**（旧版帧 4 定屏幕点，布局 settle 后漂移
  → 点落空，press=0 偶发）；λ 内自取 SceneCam（捕获帧内局部 cam 引用 = UB）。
- smoke-drag 帧 2 标记 + BuildUI 强制默认布局（DockBuilder 必须在 NewFrame 内
  且窗口 ID 栈上——直接在主循环调 = 崩溃，已踩）。回归不再吃 ini 布局漂移账。
- full 套件曾在机器高负载下 final fps 判据抖（minFps 30 vs 45 阈值）——判定为
  负载抖动非回归，静置后 58–59 稳过。
- 极缩小诊断口：smoke-drag 帧 70 跳 zoom 0.05 取景（配 --screenshot 扫描网格
  均匀性；用后保留，间距/亮度扫描全部均匀）。

### 回归红线（全过，两轮连跑）

ctest 3/3；`tools/editor-regression.sh full` **10/10 × 2**；smoke-drag
`--validate` 零报错；empty-sr 场景（上轮崩溃路径）PASS。

## 2026-09-21 M4.7 手测第四轮：滚轮几次后相机甩飞（center=(12333,139304)）+ F 找不回

### 1. 根因：视野外选中对象参与缩放锚定 = 每格滚轮甩 11% 距离

缩放锚点数学本身量纲正确（before/after 均 ScreenToWorld），但锚点选择无条件取
**选中对象中心**：对象在视野外（或父链携带上轮爆炸损伤——Inspector 显示的 pos 是
**局部值**，世界坐标可在 1e5+ 量级）时，每格滚轮把相机向它拖 (1−1/k)·距离——
对象在 1e6 世界距离处一格就是 ~1.1e5 单位，"放大缩小几次"即可把 center 甩到
十万量级（截图 center=(12333,139304)、visible 0、原点红线出视野全是同一件事）。
修复：**选中对象屏幕锚点在视野 ±25% 余量内才锚其中心，否则退回鼠标锚**（视野外
对象从此无法拽动相机；Godot 手感 = 鼠标锚）。

### 2. F 聚焦被 hover 门挡死 = 迷路后没有回家键

F 原实现要求鼠标悬停 Scene 窗口——在 Hierarchy 选中实体再按 F 完全无效。修复：
- F 去 hover 门（仅 WantTextInput 拦截），层级面板选中后直接按 F 即聚焦；
- 空选中 = 聚焦**全部**可绘制实体（View<Transform2D,SpriteRenderer> 包围盒）；
- 角标 visible=0 且有选中时提示"选中对象在视野外 —— 按 F 聚焦"。

### 3. 顺带修掉：SDL3 鼠标点坐标 vs RT 像素量纲（DPI≠1 隐患）

SDL3 后端透传窗口"点"坐标，ScreenToWorld/缩放锚点/平移按 RT"像素"归一——
Retina（DPI=2）下拾取/拖拽/鼠标锚点会整体偏 2×。统一加 ptToPx 换算
（mousePx / pan delta）；DPI=1 行为不变（本机测试环境即 1，回归判定不受影响）。

### 4. 回归扩线：smoke-drag 第五段（甩飞防护 + F 聚焦）

帧 72 抛远实体（2e5,3.5e5）并选中；73-75 视口中心滚轮 ×3；77 断言相机未被拽走
（|Δcenter|<16——注入点 ±1pt 抖动在极缩小下折 ~2 单位/格，真甩飞是每格 2 万+）；
79 经 ImGuiBackend::SetKeyTapOverride 注 F（down 帧注入、次帧补 up，同帧合并会
吃掉按下沿）；81 断言相机聚焦实体（focusΔ<32）。看门狗 70→84 帧，套件
--frames 80→90。verdict 追加 `slingΔ=(x,y) focusΔ=` 字段。

### 回归红线

`tools/editor-regression.sh` **10/10 × 2 连跑**；smoke-drag 全段 OK
（move/rot/scale/zoom/sling/focus）。

## 2026-09-21 M4.7 手测第五轮：8 向拖动"不丝滑"——吸附与网格显示解耦

### 根因：gridSnap_ 一 flag 两用

`gridSnap_` 默认 true，同时 gate **网格显示**（DrawGrid）与**全部拖拽吸附**——
用户看得见网格（默认开）就必然吃着全套台阶：平移 8 世界单位一档、旋转 15° 一档、
8 向 resize **半宽** 8 单位一档（总尺寸 16 单位，zoom≥1 时每跳 8-16px）、Scale
0.25 档。这不是帧率问题（GPU 0.3ms、60fps），是吸附档位台阶被感知为"卡顿"。
Godot（磁铁开关）/Unity（按 Ctrl 才吸）默认都是**不吸附**。

### 修复：Godot/Unity 语义对齐

- `gridSnap_` 拆成 `gridVisible_`（默认 true，纯视觉）+ `snapEnabled_`
  （默认 false）——拖拽全模式连续丝滑；
- **按住 Ctrl 拖拽 = 临时取反吸附**（`snap != io.KeyCtrl`），吸附党不用去点开关；
- 工具栏拆两钮：# 网格显示 / 新 **Magnet**（马蹄磁铁，形状页 17→18 图标）吸附
  开关，tooltip 注明档位与 Ctrl 临时取反；
- smoke-drag 帧 3 `gridSnap_=false` → `snapEnabled_=false`（现默认已关，显式防
  默认变更），帧 70 诊断 → `gridVisible_=true`。

### 附带确认（拖拽手感其余环节本就正确）

比例跟随模型 arm 时 f=1 无跳变、手柄 1:1 跟随鼠标；4px arm 阈值与 Godot 同级；
拖拽逐帧路径无分配/无 O(n)（dragTfs_ 仅 arm 时快照一次）。上一轮的 ptToPx 量纲
修正保证 Retina 下鼠标→世界 1:1（否则拖拽速度整体 2× 偏差也会被感知为"不丝滑"）。

### 回归红线

`tools/editor-regression.sh` **10/10 × 2 连跑**；工具栏/图标页截图核对正常。

## 2026-09-21 M4.7d 可选件回捞：label-scrub / Console 折叠 / 面包屑 / Layout 下拉 + 属性轨潜伏修复

M4 规划剩余功能面 = M4.7 d 批次四件（原砍单候补，全部回捞）+ M4-Editor-Plan §9 两处
回填。本轮一并闭环。

### 1. Inspector label-scrub（砍单候补首位）

- **手感**：拖字段名横向改值（Unity 拖 label 同款）。Text 无交互 ID（IsItemActive 恒假）
  → 手动跟踪：hover+左键接管，按住期间逐帧 dx；hover/拖拽 = 主题色下划线 +
  ResizeEW 光标（发现性）。浮点 1.0/px（度字段 0.5°/px 与 DragFloat 对齐）、Shift = ×0.1
  微调；整型走**余数累计**（acc += dx → 取整步 → 残差跨帧保留，慢拖不丢步），钳类型域
  （uint64 特判不入 int64 域）。覆盖 Float/Double/整型族；Vec2/Bool/枚举/颜色/资产槽
  /EntityRef/TeamRef 不参与（歧义或非数值）。
- **ID 纪律**：PushID(f.name) 提前到整字段作用域（label "##scrub" 与控件 "##v" 共用
  字段名种子，控件 ID 与旧版逐字一致）。
- **属性轨合流**：进行中 → g_scrubActive（冻结空闲快照刷新）；结束帧 → g_scrubEnded
  （= Deactivated，提交 before/after）——与控件拖拽同一合并语义，读毕即清不跨组件。

### 2. 属性轨潜伏 bug（M4.2 起）：Inspector 控件编辑从不进 Undo

- **根因**：DrawComponent 提交块判序 `if (!anyActive) 刷新空闲快照 else if (anyDeactivated)
  提交`——ImGui 释放帧 IsItemActive 已翻 false 而 IsItemDeactivated 为 true（源码核实：
  ButtonBehavior 释放路径就地 ClearActiveID → SetActiveID(0) 记 DeactivatedItemData），
  单字段交互永远走第一分支把空闲缓存刷成改后值，提交分支不可达。Gizmo 拖拽走直推
  路径（ViewportPanels → PushPropertyUndo）故未暴露；M4.2 验收只测了 Gizmo 链。
- **修复**：提交判定前置（anyDeactivated && key 匹配 → 提交；否则 !anyActive 才刷新），
  加空交互跳过（点了没改值 before==after 不入栈）。

### 3. Console Collapse（M4.7d）

连续同文同级并组一行 + `×N` 徽标（Unity 语义；刷怪/重复告警降噪）；过滤作用整组
（组内同级等效）。默认开，与 Auto-scroll 并列开关。

### 4. AssetBrowser 面包屑（M4.7d）

`Assets / 子目录 / …` 逐级 TextLink 可点直达（末段灰显 = 当前位置），替代目录下拉
——深层目录不用在全量列表翻。连续/尾随斜杠防御；逐段 PushID 防同级重名。

### 5. 工具栏 Layout 下拉（M4.7d，右段兑现"预留"）

命名布局 = imgui.ini 全量快照另存（`SaveIniSettingsToMemory` → `.lemon/editor/
layouts/<名>.ini`）；切换/更新/删除/另存；"默认布局"走既有 forceDefaultLayout_ 通路。
切换延迟一帧到 BuildUI 布局安全点（与 DockBuilder 同点帧内应用）。窄工具栏下让位
（右对齐坐标 < 当前光标则不画，不挤中段 Play）。

### 文档回填（M4-Editor-Plan §9 收尾）

09 §8：多窗口顺延 M5+ 记录 + 编辑器冒烟覆盖边界条目（十步一键 vs 真人手测分工）；
ADR-009 修订记录：逐字段 override 正式移 M5（砍单 #1 生效，M4.4 整体 Revert 顶住）。

### 回归红线

`tools/editor-regression.sh` **10/10 × 2**（改动前后各一轮）；截图目检：Layout 下拉
右对齐不叠中段、面包屑 TextLink 正常渲染、Console Collapse 控件在位。
