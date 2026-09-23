# M1～M5 模拟人工测试报告（2026-09-23）

> 定位：对 **M1 渲染内核 ～ M5 玩法（批⓪+①）** 当前完成功能做一轮"模拟人工"验证——
> 以 09-Testing.md 判据与 Editor-Manual-Test-Guide.md 清单为大纲，用引擎自带的
> 无头注入会话（--smoke/--smoke-ui/--smoke-drag/--final/--bench-survivor）+ bench
> 判据复核 + 截图人工目检代替真人操作。**未修改任何代码**；测试期间仅删除过一个
> 损坏的生成物文件（见 BUG-1）。工作区 git status 保持干净。
>
> - 版本：`7e9214f`（M5 批① 收官提交），Release 构建，构建时无待编译项
> - 环境：macOS（darwin 24.6.0 x64）/ AMD Radeon RX 590 / MoltenVK 1.3.357 / .NET 10.0.12
> - 测试人：ZCode 代理（模拟人工），纯真人路径项见 §4 未覆盖清单

## 0. 结论总览

| 里程碑 | 判据复核 | 结果 | 当轮新发现 |
|---|---|---|---|
| 单测（M2/M3/M5 逻辑层） | ctest 3/3；engine-tests **13098** checks / script-tests **1340** checks | ✅ 全绿，与 09 §7.6 基线一致 | — |
| M0 spike | 01-triangle / 02-sprites / 03-csharp 全部 exit OK | ✅ | — |
| M1 渲染 | bench-mow **132.3fps**、批数恒 4、设备丢失/resize/质量降档全过、验证层零错误 | ✅ 全过（优于 107fps 基线） | **BUG-1（P1）管线缓存损坏 → 启动断言崩溃** |
| M2 ECS | bench-sim 1 万怪 **avg 4.97ms**（≤8）、击杀-补充循环（destroyed 1521）、金回放双档 mismatches=0 | ✅ | — |
| M3 脚本 | 5k 整步 **0.207ms**（≤8）、托管分配硬 0、毒脚本恰 60 帧禁用不崩、金回放 PASS、100k 净时比 **1.40×**（≤1.5 硬判档） | ✅ | **BUG-4（P3）5k 档 --cpp-compare 工具判分与 ADR-010 D5 分档不符** |
| M4 编辑器 | `editor-regression.sh full` **11/11**（首轮即绿，smoke-ui 未飘忽）；smoke-drag 连跑 **10/10**；冷启 **340ms**（<2s）；overlay 四要素像素断言全过 | ✅ | **BUG-2（P2）Profiler 系统表 Play 中恒空**；**BUG-3（P3）首启默认布局 Profiler 浮窗遮挡** |
| M5 玩法（批⓪+①） | bench-survivor ×3 全 **PASS**（alive 10467 ≥1 万、frameAvg 15.49~15.54ms ≤22.2、fps 64~65）；批⓪金回放三档零重录 mismatches=0 | ✅ | **OBS-1 bench 模式不切 Game 标签（口径与 09 §6.10 描述不一致）** |

**本轮共发现 2 个代码缺陷（BUG-1/BUG-2）、3 个低级缺陷/口径问题（BUG-3/4/5）、
3 条观察项（§3 OBS）**，详见 §2/§3。

## 1. 各里程碑实测明细

### M1 渲染内核（全过）

| 项 | 命令 | 实测 | 判读 |
|---|---|---|---|
| 基础冒烟 | spike-01 `--frames 120 --validate` | 59.2fps FIFO，exit OK（首帧 max 333ms 为 shader 预热，正常） | ✅ |
| RHI 全链 | rhi-smoke `--frames 300 --validate` | 63.9fps，GPU 0.082ms，验证层零错误；二次启动命中 `pipeline cache loaded (19257 B)` | ✅ |
| 精灵单项 | bench-sprites 10万 `--immediate` | 166.8fps（基线 152.3） | ✅ |
| 粒子单项 | bench-particles 10万 `--immediate` | **392.7fps**（基线 222.5，反而更好） | ✅（口径注意见 OBS-4） |
| 终验收官 | bench-mow `--immediate --frames 900` | **132.3fps**、batches 恒 [4..4]、instances min 84457、recreates=0 skipped=0、quality High、验证层零错误 | ✅ |
| 设备丢失 | `--device-loss 300 --validate --frames 900` | deviceLoss PASS，零验证错误 | ✅ |
| resize 压测 | `--resize-test --validate --frames 1500` | requested=5 seen=6（macOS 双事件，既有口径）、recreates=5 skipped=0、批数恒 4 | ✅ |
| 质量降档 | `--sprites 400000 --frames 900 --immediate` | EMA 23.0ms 降 Med → 23.3ms 降 Low，终态 quality=Low（4× 过载下 fps FAIL 属本场景设计目的） | ✅ |

### M2 ECS 运行时（全过）

- bench-sim `--n 10000 --frames 3600 --stats`：**avg 4.970ms**（判据 ≤8），
  alive 9682 / created 11203 / destroyed 1521 —— M5 批⓪ iFrames 修复后的
  击杀-补充循环持续生效（不再是"alive 恒 10002"旧症）。
- 系统分解：Separation 4.145ms 主导，其余 16 系统 <0.5ms；Pickup/Hitbox 在该
  基准场景空转（无宝石/弹幕源），符合设计。
- 确定性回放：`--replay build/goldens/m5b0-sim-mt.txt` 与 `--threads 1 --replay
  m5b0-sim-st.txt` 均 **replay=PASS mismatches=0** —— M5 批①"零重录证明"在当前
  头指针上依然成立。单线程档 avg 20.7ms 与既有诊断档口径（~19.9ms）一致。

### M3 C# 脚本层（全过，含一条工具判分问题）

- bench-script `--n 5000 --frames 3000 --stats`：整步 **avg 0.207ms / max 0.648ms**
  （判据 ≤8）；**托管分配总 0 B**（非零窗 0）；毒脚本 stderr **恰 60 条**
  `Poison ... block 0` 后 `disabled after 60 consecutive failing frames`，引擎不崩。
- 100k 硬判档 `--n 100000 --frames 3000 --cpp-compare`：ratio **1.40×** ≤1.5（基线
  1.43），步长 avg 3.811ms，RESULT PASS。
- 金回放 `--frames 1800 --replay build/goldens/m5b0-script.txt`：PASS。
- spike-03（CoreCLR 最小闭环）：exit OK，60Hz 步进 tick avg 2.534ms。
- ⚠ 5k 档 `--cpp-compare` 被**工具判 FAIL**（1.57×>1.5×）——见 BUG-4。

### M4 编辑器 v1（全过）

- `tools/editor-regression.sh full build/mac`：**11/11 首轮全绿**
  （ctest 3/3 + basic/close×2/drag/ui/assets/script/final/save-scene×2）。
  其中 smoke-ui（真人会话模拟：快捷键/Undo/scrub/保存/Play/重命名/挂父子/导航/布局）
  本轮未复现 09 §8 登记的 2/6 飘忽。
- smoke-drag `--frames 90 --no-reopen` 连跑 **10/10 OK**（M4.8-c 出口判据复核）。
- `--final --smoke`：cold-start **340ms**（<2s）、overlay 像素断言
  `grid=18562(≥8000) sel=82 handle=49 label=833 全过`、热重载/StateBag/autosave 链 PASS。
- `--smoke --frames 120`：cold-start 531ms、cjkFont=OK、entities=5/5、errors=0。
- 截图目检（`--screenshot`，无项目首启卡 + 常规场景）：菜单栏/工具栏三段式/
  布局下拉/Hierarchy 搜索+创建/Scene 网格/选中选框/移动手柄/实体标签/Inspector
  组件页（含 M5 批① Collectible 字段齐全）/Assets 面板/状态栏脏点与红字指引
  全部正常可见。发现 BUG-2/BUG-3（§2）。

### M5 玩法批⓪+①（全过）

- bench-survivor `--frames 900` 三跑：**全 PASS**——alive 10467、stepAvg
  13.22/13.25/13.30ms、frameAvg 15.49~15.54ms（判据 ≤22.2）、fps 64/65/65、
  present=IMMEDIATE。与 09 §6.10 成长化基线（61~64fps）一致偏好。
- 分段归因复核：sim Σ=13.2~13.7ms（Pickup+万怪密团，既有观察项）、scene ~1.5ms、
  ui ~0.26ms（Hierarchy clipper 生效——截图中万级 BenchGem/BenchMob 平铺滚动正常）。
- frameMax 43.6/56.2/58.9ms，尖刺帧(>25ms) 33/36/33 个/跑，尖刺帧 sim 段均值
  26.3~27.0ms —— 与文档"Census 每 30 tick 全量扫"归因一致，**既有观察项维持**。
- 批⓪金回放三档（sim-mt/sim-st/script）零重录 replay 全 PASS（见 M2/M3 节）。
- Inspector 截图确认 Collectible 24B 新字段（magnetSpeed/value 等）已进组件页。

## 2. 缺陷清单（本轮新发现）

### BUG-1（P1·健壮性）管线缓存文件一旦损坏，所有 Vulkan 程序启动即断言崩溃

- **现象**：`.lemon/pipeline-cache.bin` 截断损坏（16384 B、头部声明长度超出实际）
  后，任何走 RHI 的程序启动 100% 复现：
  ```
  [lemon][info] pipeline cache loaded: .lemon/pipeline-cache.bin (16384 bytes)
  [lemon][error] VALIDATION-ERROR: VK_ERROR_INITIALIZATION_FAILED: Error reading
                 pipeline cache data: Failed to read 4 bytes from input stream! Read 0
  [lemon][error] ASSERT Engine/Renderer/RHI.cpp:510  false    → exit 134 (SIGABRT)
  ```
  bench-mow、rhi-smoke、spike-01 等全部无法启动；**编辑器走同一段代码**
  （其缓存路径 `.lemon/editor/pipeline-cache.bin`），损坏后同样会开局崩。
- **复现/触发链（本轮实测）**：后台运行的 bench-mow 被强制终止（测试编排工具
  kill）→ 缓存文件截断 → 此后每次启动必崩。日常等价触发面：写入缓存过程中
  断电/崩溃/被杀。**删除该生成物文件即恢复**（测试中已执行，gitignored）。
- **根因（两处叠加）**：
  1. `RHI.cpp:510`（LoadPipelineCache）：`vkCreatePipelineCache` 对非法
     initialData 返回 `VK_ERROR_INITIALIZATION_FAILED` 被 `VK_CHECK` 判死刑。
     Vulkan 语义里这是**可恢复的缓存未命中**，应回退空缓存继续启动；
  2. `RHI.cpp:1254`（SavePipelineCache）：`std::ofstream` 直写目标路径，
     **无"临时文件 + rename"原子替换**，写入中断即留半截文件；
  3. 诱因放大：所有 sample 共用 cwd 下同一 `.lemon/pipeline-cache.bin`，写入
     频繁且互相覆写（9621→19257→16384 B 反复易主）。
- **建议**：Load 侧捕获该返回码 → 销毁、告警、删除/改名坏档、空缓存重建；
  Save 侧改 tmp+rename 原子写；可选：缓存路径按程序名区分。

### BUG-2（P2·编辑器观测性）Profiler 的系统耗时表在 Play 中恒为空

- **现象**（截图实证）：bench-survivor 运行中 Profiler 面板只有
  `system | last | max | runs` 表头，**无任何行**；帧率曲线/GPU 列/GC 行正常。
- **根因**：`ProfilerPanel.cpp:52` 读 `ctx.World().Pipeline().Profiles()`，而
  `EditorContext.h:29` 的 `World()` **恒返回编辑世界**（从不 Step，各系统
  runs==0 被跳过）；Play 中真正跑的是 `ActiveWorld()`（`EditorContext.h:120`）
  的 playWorld。→ 真人 Play 时 F3 系统表同样恒空，**M2 判据"F3 面板数据齐全"
  在编辑器侧实际不可用**，M5 玩法调参（观察每系统成本）直接受影响。
  该表此前未被手测验收覆盖（指南 J2 只要求曲线/GPU/GC 三样）。
- **建议**：ProfilerPanel 改读 `ctx.ActiveWorld()`（编辑态与 Play 态同一段代码
  自然分流）；或提供 编辑/Play 双档切换。

### BUG-3（P3·首启观感）默认布局不停靠 Profiler，首次启动以浮窗遮挡 Hierarchy

- **现象**（截图 100% 复现两次）：无 imgui.ini 的会话（全新安装 / 删除
  `.lemon/editor/` / 全部 `--smoke` 系注入模式）下，一个标题缩略为 "Pr…" 的
  窄条浮窗压在 Hierarchy 标签、搜索框与实体行之上。
- **根因**：`EditorApp.cpp:255-260` BuildDefaultLayout 只 DockBuilder 了
  Hierarchy/Inspector/Scene/Game/Console/Assets 六个窗口；Profiler 从未停靠，
  而 `Panel.h:23` `OpenByDefault()` 默认 true 且无人覆写 → Profiler 开着、
  悬浮在 ImGui 默认位置。现有用户的 ini 已把它停靠过（本轮 bench 截图中它
  正常待在底部），故日常不可见——**只有首启/重置布局的人会撞上**。
  代码注释（EditorApp.cpp:2231）自证知道此事："Profiler 非默认布局成员（浮动
  位置随 ini 漂移，会随机遮挡……）"，但只对冒烟会话显式关闭，未修默认态。
  另注意：这与手测指南 J4a "'默认布局'回内置七面板"的表述不符（实为六停靠 +
  一浮动）。
- **建议**：默认布局把 Profiler `DockBuilderDockWindow` 进 bottomId（与
  Console/Assets 同区作隐藏标签），或 `OpenByDefault()` 返回 false。

### BUG-4（P3·测试工具）bench-script `--cpp-compare` 判分未实现 ADR-010 D5 分档

- **现象**：`--n 5000 --frames 3000 --cpp-compare` 输出 `ratio 1.57x → （>1.5x
  FAIL）→ RESULT bench-script FAIL`。但 09 §6.9 明文"比值 ≤1.5× **仅对 100k 档
  硬判**（小规模被 ~66µs 固定域线程往返支配）"，且文档载明的 5k 历史值本身就是
  1.92×。100k 档实测 1.40× PASS（本轮复核）。工具的 1.5× 无差别硬判与验收
  口径冲突——**按 09 §6.9 首行示例命令跑，结果恒 FAIL**。
- **根因**：`Samples/bench-script/main.cpp:247` 对任意 n 无条件 `ratio <= 1.5`
  判定（文件头注释 :5 同样是未分档的旧口径）。
- **建议**：按 ADR-010 D5 分档表实现（<100k 只打印不 FAIL），并同步头注释。

### BUG-5（P2·文档/易用性）09 §5 的 bench-mow 验收命令无人值守时永不退出

- **现象**：按 09 §5 所载 `./lemon-bench-mow --immediate`（不带 `--frames`）运行，
  程序帧数上限为 0 = 无限跑，RESULT 行只在窗口关闭/Esc/看门狗后输出——无人值守
  下一直阻塞（本轮实测挂 4 分钟无输出）。
- **根因**：`Samples/bench-mow/main.cpp:33`（frames 默认 0）+ `:149`（仅 frames>0
  才判退出）。
- **建议**：frames==0 时启动日志明确提示"将运行至窗口关闭"；或给默认帧数；
  或文档示例补 `--frames 900`。

## 3. 观察项（口径/设计讨论，不计缺陷）

- **OBS-1 bench-survivor 停在 Scene 标签**：bench 路径 `EditorApp.cpp:1699` 直调
  `ctx_.EnterPlay()`，未经交互路径的 `TryEnterPlay() → tabFocusPending_=1`
  （:924/:716，F1 的"进 Play 自动切 Game"）→ Play 测量期间活动标签停在 Scene，
  实际渲染的是 Scene 视口（网格+万级标签 overlay）而非 09 §6.10 口径所述
  "GameView 渲染"。历次基线同口径横向可比（且 Scene 视口是更重的路径，数字
  偏保守），但文档表述应修正，或 bench 路径补齐 tab 切换与交互侧对齐。
  连带影响：RT UI（XP 条）叠加只在 GameView 绘制，bench 口径下从未被看到。
- **OBS-2 Inspector 暴露 Hierarchy 组件内部字段**：parent/firstChild/next/prev
  （"(internal)"）直接出现在组件页。Unity 不暴露该层级内部态；若为调试用途
  建议折叠/隐藏，属设计取舍，请拍板。
- **OBS-3 G7（双击资产建实体并选中）**：代码路径确认 `AssetBrowserPanel.cpp:299`
  建实体后即 `ctx.Select(ne, false)`（替换式选中，Hierarchy/Inspector/视口选框
  同源 `selection_`），此前的"不会选中"疑虑大概率是误观察或已被修复；真人
  复验仍待（本环境无法双击）。
- **OBS-4 09 §4 的 bench-particles 示例命令缺 `--immediate`**：照抄示例只跑出
  61.1fps（FIFO 垂直同步锁 60Hz），与同节所载 222.5fps 基线口径不符（本轮补
  `--immediate` 实测 392.7fps）。示例命令应补旗标。
- **既有观察项状态复核**：smoke-ui rename/scrub 飘忽（09 §8）本轮 11/11 首轮
  全绿未复现（按文档口径仍属"重跑至全绿"类）；frameMax Census 尖刺（33~36 个/
  跑）与 Pickup 密核扫描余量维持登记，无恶化。

## 4. 未覆盖清单（需真人/本环境不可达）

- 纯 OS/观感路径：Finder 拖 PNG 导入（G1/G2）、IME 中文上屏与快捷键屏蔽（E7/J5）、
  FilePicker 模态观感（K1-K5）、关闭三键模态（L2/L3 真人观感）、Inspector 控件
  手感（D 系 label-scrub 手感）、G4-G6 右键菜单真人复验、G7 双击真人复验。
- GameView 焦点输入链（F3 聚焦才进游戏）、F8"脚本未就绪"模态、A2 向导 UI 流、
  B7 崩溃恢复模态观感——自动化已有等价覆盖（--final/--play），模态观感未目检。
- RT UI（XP 条/文本）在 GameView 的目视效果（受 OBS-1 影响，bench 口径不可见；
  ctest/script-tests 已覆盖通道语义）。
- 断点通路（诊断 IPC/域名线程）：本轮未重新验证（M3 已留证据链，无相关改动）。
- 2 分钟浸泡（7200 帧）：时间预算内未跑；bench-mow 900 帧稳态指标全绿。

## 5. 附录：原始数字速查

```
ctest                : 3/3 (engine-tests 13098 / imgui-isolation / script-tests 1340)
spike-01/02/03       : exit OK ×3（02: 327.9fps drawCalls=1）
rhi-smoke            : 63.9fps gpu=0.082ms pipeline cache loaded(19257B) 零错误
bench-sprites        : 166.8fps (10万, IMMEDIATE)
bench-particles      : 392.7fps (10万, IMMEDIATE) / 61.1fps (FIFO, 示例命令原样)
bench-mow            : 132.3fps batches[4..4] recreates=0 skipped=0 quality=High
bench-mow device-loss: PASS（--device-loss 300, 零验证错误）
bench-mow resize     : requested=5 seen=6 recreates=5 skipped=0
bench-mow quality    : Med(23.0ms) → Low(23.3ms) 终态 Low
bench-sim            : avg 4.970ms alive=9682 destroyed=1521 (1万, 3600帧)
replay sim mt/st     : PASS mismatches=0 ×2（m5b0 金档零重录）
bench-script 5k      : avg 0.207ms 托管0B 毒脚本60+禁用；cpp-compare 1.57x=工具FAIL(BUG-4)
bench-script 100k    : ratio 1.40x PASS avg 3.811ms 托管0B
replay script        : PASS（m5b0-script.txt）
editor-regression    : 11/11（full，首轮）
smoke-drag ×10       : 10/10 OK
--final              : cold-start 340ms overlay grid=18562/sel=82/handle=49/label=833 全过
bench-survivor ×3    : PASS alive=10467 frameAvg=15.49/15.49/15.54ms fps=65/65/64
                       frameMax=45.1/57.1/58.9ms 尖刺33/36/33个（Census，既有观察项）
```

> 报告生成：ZCode 模拟人工测试轮；建议 BUG-1/BUG-2 优先安排修复批
> （BUG-1 影响所有 Vulkan 程序的崩溃韧性，BUG-2 直接影响 M5 玩法调参观测）。
