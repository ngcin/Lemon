# 09 · 测试方法与实测记录

> 本册回答两个问题：**Lemon 怎么测**（方法/命令/判读）与**测出了什么**（最新结论）。
> 完整的数字历史与事件流水在 `docs/DevLog/`（一条目一文件）；本册记可复现的方法和当前基线。
> 每个里程碑验收、每次新增专项测试后更新本册（§9 约定）。
>
> **编辑器使用级测试**（手测清单 70+ 项 + 一键自动化 `tools/editor-regression.sh`）
> 见 [Editor-Manual-Test-Guide.md](./Editor-Manual-Test-Guide.md)。

## 0. 测试纪律（事故换来的，不可省略）

1. **验证层第一天就开**：任何 Vulkan 改动自测默认带 `--validate`；跑完 grep 确认零错误零警告：
   `./程序 --validate 2>&1 | grep -iE "error|warn|VUID"`（无输出 = 干净）。
2. **探路顺序**：新负载先用小 N + FIFO 确认正确性 → 逐步放大 → 正式数字才用 `--immediate`。
   （M1 曾因跳过此顺序 + 尺寸语义错误造成 5000× 过采样，整机卡死。）
3. **看门狗内置**：所有 bench 帧时间 EMA>250ms 自动中止（exit 1 + `ABORT` 行）——
   出现即说明渲染管线把系统卡死，先查过采样/同步，不要重跑硬扛。
4. **"单测全绿 ≠ 可用"**（Prowl2D 教训）：纯逻辑断言之外，每个结论必须有
   可运行/可量化的验收（本册 §3–§7 的程序就是为此存在）。

## 1. 单元测试（纯逻辑层，无 GPU）

```bash
cmake --build --preset mac
./build/mac/tests/lemon-tests        # 期望末行: "[lemon][info] engine-tests: 11545 checks OK"
```

覆盖：数学（FastSin/FastCos LUT 误差界、Mat3x2）、图集 UV/MakeSpriteInfo、批键合成、
排序稳定性、粒子池（发射计数/颜色插值）、Camera2D（像素完美 snap/阻尼/边界钳制）、
质量分级、5×7 字模；M2 起——PCG32 随机流（同 seed 逐位一致/golden 值防漂移）、
JobSystem（ParallelFor 区间覆盖/单线程诊断档/任务完成）、对象池（槽位复用/延迟归还）、
环形队列（回绕/扩容 FIFO 完整性）、Scene 生命周期（两阶段销毁/句柄回收/重复销毁幂等）、
组件注册表（27 组件/offset 校验/POD 断言）、.scene roundtrip（字段保真/实体引用 remap/
不动点/未知组件前向容错）、Team 关系表、空间哈希（圆/盒/射线/点查询、team/layer 过滤、
命中序确定性）、系统管线（16 系统执行序、端到端模拟、分离力对称性）。
渲染路径（RHI/合批）不在此层，由 §2 程序覆盖。

**结果（2026-09-19）：11545 checks OK（M2 交付 11511 + 复审轮 34；复审轮内容见 DevLog 同日条目）。**

## 2. 测试程序一览

| 程序 | 路径（build/mac 下） | 用途 |
|---|---|---|
| lemon-tests | `tests/` | 纯逻辑断言（§1） |
| lemon-rhi-smoke | `Samples/rhi-smoke/` | RHI 薄层全链路冒烟（§3） |
| lemon-bench-sprites | `Samples/bench-sprites/` | 精灵流水线单项压测（§4） |
| lemon-bench-particles | `Samples/bench-particles/` | 粒子单项压测（§4） |
| lemon-bench-mow | `Samples/bench-mow/` | M1 终验收官场 + 全部专项开关（§5–§6） |
| lemon-bench-sim | `Samples/bench-sim/` | M2 终验收官场：1 万怪全系统模拟 + 确定性回放（§7） |
| lemon-script-tests | `tests/script/` | M3 脚本桥：布局/blit/roundtrip/RNG golden/域管理/批量/事件/结构命令（§6.9） |
| lemon-bench-script | `Samples/bench-script/` | M3 终验收官场：C# 环绕弹幕 + 毒脚本 + GC 零分配 + 回放（§6.9/§7.6） |

通用 flags：`--frames N`（跑 N 帧退出）、`--validate`（验证层）、`--immediate`（不限帧率）。
bench-sim 专属：`--n N`（怪数）、`--threads N`（1=单线程诊断档）、`--stats`（每模拟分钟
打印每系统耗时）、`--record F`/`--replay F`（录制/回放输入流+逐帧状态哈希）、`--seed S`。
bench-script 专属：`--n N`（弹数）、`--cpp-compare`（C# 净时 vs 同负载 C++ 等效系统比值）、
`--stats`/`--threads`/`--record`/`--replay` 同 bench-sim（回放档无输入行，场景确定性驱动）。

## 3. rhi-smoke —— RHI 薄层冒烟

```bash
./build/mac/Samples/rhi-smoke/lemon-rhi-smoke --frames 300 --validate
```

覆盖点：动态渲染、bindless 纹理/采样器槽（含槽 3 非零下标）、实例 SSBO、push constant、
管线磁盘缓存落盘与二次启动加载、GPU 时间戳、resize、`--device-loss K` 设备丢失恢复、
**mips 生成链**（256×256 棋盘 9 层；窗口顶部 256→8px 递减一排，缩小后棋盘应收敛为
均匀红灰混合而不是闪烁摩尔纹——肉眼判读 mip 是否生效）。

判读：`exit OK`；无 error/warn 输出；GPU 时间应为 ~0.1ms 量级。
**结果（2026-09-18，验证层开）**：FIFO 67.7fps，GPU 0.093ms，验证层零错误零警告；
管线缓存二次启动命中 `pipeline cache loaded: 9621 bytes`；mip 棋盘收敛正确。

## 4. 单项压测：bench-sprites / bench-particles

```bash
./build/mac/Samples/bench-sprites/lemon-bench-sprites --n 100000 --frames 300 --immediate
./build/mac/Samples/bench-particles/lemon-bench-particles --n 100000 --frames 300
```

| 程序 | 负载 | 实测（2026-09-18） |
|---|---|---|
| bench-sprites | 10 万精灵全流水线 | 152.3fps（IMMEDIATE），渲染 CPU 3.76ms，1 批 |
| bench-particles | 10 万预算粒子 | 222.5fps，GPU 0.741ms（存活 8.7 万时），2 批 |

## 5. bench-mow —— 终验收官场

15 万实例（默认 10 万精灵 + 5 万粒子）+ HUD 位图文本 + 双图集 + 相机剔除窗口运动。

```bash
./build/mac/Samples/bench-mow/lemon-bench-mow --immediate --frames 900   # 正式数字（无帧率上限；--frames 缺省 = 跑到窗口关闭，无人值守必加）
./build/mac/Samples/bench-mow/lemon-bench-mow --validate --frames 900    # 正确性（vsync 锁 60）
```

flags：`--sprites/--particles N`、`--zoom F`（相机拉近，F=2.58 ≈ 压测 A 的 15% 可见语境）、
`--device-loss K`、`--resize-test`（§6.2）、`--frames N`。

**RESULT 行判读**：

| 字段 | 健康值 | 异常含义 |
|---|---|---|
| `fps≥60` | PASS（IMMEDIATE 场景） | 不足即性能回退，对照 DevLog 上一基线 |
| `batches [min..max]` | 恒定 4（精灵1+粒子2+文本1），波动≤2 | 波动大 = 批键分组不稳（曾因哈希摘要碰撞出现 672 批） |
| `instances min` | ≈ 满额（随剔除窗口小幅波动） | 骤降 = 实例数据丢失（闪烁事故的诊断证据之一） |
| `swapchainRecreates` | 稳态 0（resize/丢失场景除外；启动时 SDL 双建不计入） | 稳态>0 = 交换链异常重建 |
| `skippedFrames` | 0 | >0 = acquire/present 连续失败 |
| `quality` | 稳态 High；超载时应见降档（§6.3） | 高负载仍 High = 降档逻辑没触发 |

## 6. 专项测试方法

### 6.1 设备丢失自动恢复

```bash
./build/mac/Samples/bench-mow/lemon-bench-mow --device-loss 300 --validate --frames 900
```

判读：RESULT 行 `deviceLoss PASS`；验证层零错误；帧计数连续（恢复后画面立即正常）。
**结果（2026-09-18）**：全规模注入恢复，验证层零错误。

### 6.2 resize 满负载压测（--resize-test）

```bash
./build/mac/Samples/bench-mow/lemon-bench-mow --resize-test --validate --frames 1500
```

方法：程序在第 300/500/700/900/1100 帧程序化改窗口尺寸
1600×900 → 640×400 → 320×200（极小）→ 1680×380（极端宽高比）→ 1280×720（复原），
全程 15 万实例渲染。
判读：`resize-test: requested=5 seen≥5`、`skippedFrames=0`、批数恒定；
极小窗口下 `instances min` 应明显下降（剔除联动），最大单帧 ~1s 属 WaitIdle 重建的合理代价。
**结果（2026-09-18）**：requested=5 seen=6（macOS 对一次变化可发两条事件）、
recreates=5 skipped=0、批数恒 4、320×200 时可见 8.5 万、验证层零错误。

### 6.3 质量分级实时降档

```bash
./build/mac/Samples/bench-mow/lemon-bench-mow --sprites 400000 --frames 900 --immediate
```

方法：40 万精灵（4× 验收负载）把帧时间压过 20ms 阈值，观察 EMA 降档链。
判读：日志出现 `quality downgrade -> Med` → （持续超阈 2s）`-> Low`；
RESULT `quality=Low`。
**结果（2026-09-18）**：EMA 24.7ms 降 Med、23.2ms 再降 Low；全程 stddev 0.98ms 稳定。
（反向验证：FIFO 60fps 时 16.6ms < 20ms，浸泡 2 分钟无误降档。）

### 6.4 长时浸泡（查慢泄漏/退化）

```bash
./build/mac/Samples/bench-mow/lemon-bench-mow --frames 7200 --validate   # ≈2 分钟
```

判读：fps 维持 60、recreates=0、skipped=0、quality=High 无抖动。
**结果（2026-09-18）**：7200 帧 60.2fps，全指标无退化。

### 6.5 管线缓存命中

方法：同一程序连跑两次，第二次启动日志应出现 `pipeline cache loaded`（首次为落盘 saved）。
**结果（2026-09-18）**：rhi-smoke 二次启动命中 9621 bytes。

### 6.6 mips 生成链

方法：见 §3（rhi-smoke 棋盘）。此链曾在补测轮发现真 bug：屏障缺 baseLevel 参数全部
打在 level 0 上，blit 源/目的布局双不符——零调用方导致从未被跑过。修复后验证层零错误。

### 6.7 bench-sim —— M2 模拟验收场（无渲染，纯 CPU）

```bash
./build/mac/Samples/bench-sim/lemon-bench-sim --n 10000 --frames 3600 --stats   # 性能
```

负载：1 万怪（Chase 玩家 + 同队分离 + 击退）+ 玩家弹幕 + 命中/死亡/补怪（maxAlive 配额）+
投射物寿命回收，16 系统全链路（4 个里程碑占位空跑）。看门狗：步 EMA>250ms 中止（§0.3）。
RESULT 行判读：`sim avg ≤ 8ms`（08 §3 判据）；`alive` 稳定在 n 附近（配额生效的旁证）；
`--stats` 输出每系统 last/max 耗时（F3 数据源，尖峰看 max 列）。

### 6.8 确定性回放（录输入流 + 逐帧状态哈希比对）

```bash
./build/mac/Samples/bench-sim/lemon-bench-sim --n 10000 --frames 18000 --threads 1 --record r.rpl
./build/mac/Samples/bench-sim/lemon-bench-sim --n 10000 --frames 18000 --threads 1 --replay r.rpl
# 多线程档：去 --threads 1 重录重放（两档独立验收）
```

机制：录制侧每帧写程序化输入（独立 seed 子流）+ 全场景状态哈希（FNV-1a over
组件字段、按注册表序/池序——确定性遍历）；回放侧纯读盘驱动输入，逐帧重算哈希比对。
判读：`replay=PASS mismatches=0`（exit 0）。**双档验收**：单线程档（逻辑基准）与
多线程档（并行确定性：cell 内 id 稳定序 + 分离力截断序 + 逐实体写自身无共享写）
都必须逐帧一致；单线程档同时是"逻辑 bug vs 并行 bug"的诊断隔离基准。
文件格式 `LREPLAY1`（文本可 diff）；改系统注册序/RNG 实现/哈希字段 = 破坏回放兼容。

> **零重录先例（M5 批①，2026-09-22）**：管线**中插**新系统（PickupSystem #9）而
> 既有基准场景无其驱动组件（Collectible/XpProgress 皆空）→ 系统空转 → 状态哈希流
> 不变 → **批⓪金档原样 `--replay` 三档 mismatches=0**（sim mt/st + script）。即：
> 行为零漂移的系统插入可以且应当用旧档回放作机械证明，而非默认重录。
>
> **重录先例（M5 批②，2026-09-23）**：`ComputeStateHash` 对注册表**每个组件名
> 无条件入哈希**（schema 漂移绊线，含零实体组件）——故**新增组件（WaveDirector）
> 即全帧哈希漂移**，与布局变更同类，属重录口径（行为零漂移的旁证：bench-sim 终态
> alive/created/destroyed 与旧档逐项一致）。金档换代 m5b0 → **m5b2**（sim mt/st +
> script 三档，重录后 replay mismatches=0）。推论：**加字段可零重录（空组件名已在
> 哈希流），加组件必重录**——批次规划时按此预判。
>
> **零重录先例二（M5 批④，2026-09-23）**：World 级新通道（SaveChannel 存档 KV /
> RtUiCards 三选一卡片 / RtUi 槽着色）+ vtable 尾追 8 项——零新组件、零布局改动，
> 且新通道**不入 StateHash**（呈现与用户数据段）→ m5b2 三档原样 replay
> mismatches=0。即：**凡"World 持有 + 非 ECS"机制一律零重录**，与批②推论合并：
> schema 一字不动 = 旧档即证。

### 6.9 bench-script —— M3 脚本验收场（无渲染，CoreCLR 域线程）

```bash
./build/mac/Samples/bench-script/lemon-bench-script --n 5000 --frames 3000 --stats --cpp-compare
./build/mac/Samples/bench-script/lemon-bench-script --n 5000 --frames 18000 --threads 1 --record r.rp
./build/mac/Samples/bench-script/lemon-bench-script --n 5000 --frames 18000 --threads 1 --replay r.rp
./build/mac/Samples/bench-script/lemon-bench-script --n 5000 --frames 18000 --threads 4 --replay r.rp  # 双档
```

负载：档② `BoomerangSystem`（Projectile+Transform2D 双组件查询，绕玩家轨道原地写回）
驱动 N 弹 + 档① `PlayerBehaviour`（Lissajous 走位，经 NativeApi 写组件）+ 毒脚本
`PoisonSystem`（每帧抛异常）。判读（ADR-010 修订版判据，D5/D6）：
- `avg ≤8ms`（5k 档）；`--cpp-compare` 比值 ≤1.5× 仅对 100k 档硬判（小规模被 ~66µs
  固定域线程往返支配，见 ADR-010 D5 分档表）；
- `托管分配: 总 0 B`（硬 0，16 帧采样窗逐窗核对；前提 = bench 自设
  `DOTNET_TieredCompilation=0` + 引擎侧导出指针缓存，ADR-010 D6）；
- 毒脚本 stderr 恰 60 条 `Poison ... block 0` 红字后 `disabled after 60 ...`，
  引擎不崩（异常隔离 + 自动禁用活体验收）；
- 回放 `LREPLAY1` 无输入行（场景确定性驱动，仅逐帧哈希行）；金档绑定随引擎分发的
  .NET 运行时版本（ADR-010 D3，跨版本超越函数实现可变）。

**断点通路**（M3 判据"断点可下"的证据链，本机已验证）：
1. 进程内 CoreCLR 诊断 IPC 在线——`lsof -p <pid>` 可见 `dotnet-diagnostic-<pid>-*-socket`
   （Rider/VS/VS Code 托管附加与断点走的就是该通道）；
2. 域线程名 `Lemon.Domain`（`sample <pid> 1` 线程列表可见，附加后线程列表直接可辨）；
3. CoreCLR 自带 `.NET Debugger`/`.NET DebugPipe` 线程在进程内常驻；
4. Debug 配置（`mac-debug` preset）全量测试通过（见 §7.6）。
附加流程（Rider/VS）：Run → Attach to Process → 选 `lemon-bench-script` → 在
`Samples/bench-script/script/*.cs` 下断点 → 命中即停在域线程托管帧。注意：本机
macOS 安全策略拒绝 lldb 原生 attach（Console.app 可见 debugserver 拒绝记录）——
托管调试不受影响（走诊断 IPC 而非原生 ptrace）。

### 6.10 bench-survivor —— M5 编辑器内压测场（2026-09-22 建场；同日性能批转 PASS）

```bash
./build/mac/Editor/lemon-editor --bench-survivor --frames 900   # 预热 240 + 测量 660
```

负载：编辑器全链（模拟 + 视口提取 + GameView 渲染 + ImGui 叠加 + present），临时项目
程序化播种——怪 prefab（Health/Knockback/Velocity/Chase/**Hazard（09-24 方案 A 批）**）
经 SpawnFn 桥（清障②）由
Spawner 拉满 1 万（interval 0 / burst 64 / capAlive 10000 = "导演拉满"），玩家作
Chase 目标；**批⓪（M5.md T4）起兼弹幕源**：Shooter 20 发/s + 弹体 prefab
（dmg 12 / pierce 0），命中/击退/击杀/补怪闭环真实发生并计入 destroyed。

**测量口径**（vsync 问题在此定死）：
- 交换链按 **Immediate** 请求（mac MoltenVK 实测可拿到；拿不到回退 FIFO——此时
  fps 被 60Hz 钉住，frameAvg 恒 ~16.7ms 即为信号，数字作废）；
- 帧时 = 全帧 steady clock（含渲染提交与 present 等待），**预热 240 帧剔除**（怪海
  ~156 帧涨满 + 稳态余量）；
- 判据（08 §3）：`alive ≥ 10000 且 frameAvg ≤ 22.2ms（≙ ≥45fps）`；
- **分段计时**（M5 性能批落地）：帧内打点、帧末累计，输出
  `pump / sim / glue / ui / acquire / scene / uidraw / present` 各段均值 + segSum
  （与 frameAvg 对账；resize/acquire 失败的 continue 帧整帧不参与）。

**基线（2026-09-22，本机 6C / RX 590 / MoltenVK）→ 性能批① → 性能批②**：

| 时点 | 分段 avg ms | 全帧 |
|---|---|---|
| 建场基线 | sim 11.22 / scene 19.31 / ui 4.13（其余 <1） | frameAvg 35.43ms fps=28 **FAIL** |
| 性能批①（同日，两跑） | sim 11.14~11.47 / **scene 1.22~1.25** / ui 4.11~4.12 | frameAvg 17.16~17.48ms fps 57~58 **PASS** |
| 性能批②（同日，三跑） | sim 11.04~11.18 / scene 1.21~1.31 / **ui 0.25~0.26** | frameAvg 13.09~13.25ms fps 75~76 **PASS** |
| 批⓪ 战斗化（同日，三跑） | **sim 9.37~9.49** / scene ~1.3 / ui 0.25 | frameAvg 11.48~11.64ms fps 86~87 **PASS** |
| 批① 成长化（同日，三跑） | sim 12.84~13.92（Pickup 1.87）/ scene ~1.5 / ui 0.26 | frameAvg 15.53~16.29ms fps 61~64 **PASS** |
| 批② 导演化（09-23，三跑） | sim 12.55~12.60（Director <0.1）/ scene ~1.3 / ui 0.26 | frameAvg 15.11~15.20ms fps 66 **PASS** |
| 批③ 动画化（09-23，三跑） | sim 12.91~14.25（Animator 0.128）/ scene ~1.3 / ui 0.26 | frameAvg 14.89~17.27ms fps 58~67 **PASS** |
| Hazard 化播种（09-24，修复前红字） | **sim 49.41（Hitbox 36.43）** / scene ~1.4 / ui 0.68 | frameAvg 52.05ms fps=19 **FAIL** |
| 方案 A 查询快路径（09-24，三跑） | sim 9.90~10.05（**Hitbox 1.08** / Rebuild 0.57）/ scene ~1.4 / ui 0.26 | frameAvg 12.17~12.59ms fps 79~82 **PASS** |

批② 导演化口径变更：Spawner 闸 10000→8000 让 2000 头寸给 BenchDirector（3 波 ×
4 条目 180/s/波，t=1/6/11s）——判据在原两条（alive≥10000、frameAvg≤22.2ms）外加
**导演证据项** `waves≥3 且 teamAlive>8000`（实测 waves=3、teamAlive 10002 顶满
capAlive=10000；alive 10435、三跑逐位一致）。Director 系统成本 <0.1ms（普查与
Spawner 同款 30-tick O(n)）。

批③ 动画化口径变更：BenchMob prefab 增 `Animator2D`（程序化 4 帧表 `anim.clip`
自播种——tempdir 项目 hermetic，不依赖仓库路径；切片/clip 通道全走真实导入），
万怪帧映射进压测口径——判据再加**动画证据项** `anim(切片命中 = Animator2D 总数)`
（实测 10003/10003、三跑逐位一致；含玩家宝石等非动画实体外的全数怪群）。Animator
系统成本 0.128ms（纯函数帧号 + 逐实体 TryGet 写 spriteId；优化路径预留：spriteId
条件写/并行）。

**Hazard 化口径变更（2026-09-24 方案 A 批）**：BenchMob prefab 增 `Hazard`（dps 8 /
radius 24 / tick 0.8——参数对齐 vs-survivor 模板 Mob.prefab）+ 玩家 HP 500→1e6
（防玩家死亡扰计量，Perf10k 同款）——"万怪密团 Hazard 查询"（模板怪真实工作形状）
进回归口径，判据再加 **hazard 证据项** `playerHp<1e6`（掉血实证；红绿两跑与三跑
绿字 playerHp=275793 逐位一致 = 行为零漂移旁证）。来源：Perf10k.scene 压测
（DevLog 2026-09-23——万怪聚堆带 Hazard 51.1ms/帧 vs 去 Hazard 17.2ms，~66% 帧时
在 HazardSystem 拒绝路径）。修复 = SpatialHash 查询侧两级加速（Item 内联 team/layer
位 + cell 级 team 位图整格早退 + `TeamTable::HostileMask` 预过滤，03 §5 修订注）：
Hitbox 36.43→1.08ms（~34×）、Rebuild 仅 +0.05ms（Meta 快照）；命中集合与回调序
零漂移（差分等价单测 + 金回放 m5b2 三档原样 replay mismatches=0 零重录）。判据
阈值不动（22.2ms），历史行不删——本行起"带 Hazard 模板怪海"即判据形状。

批⓪ 战斗化后 fps 反升（76→86）非笔误：iFrames 递减修复使怪进入击杀-补充循环，
蜂群密度被持续疏散，Separation 随之回落（sim 11.1→9.4ms）。frameMax 43~52ms
仍为 Census 30-tick 尖刺（4 个/跑，见下），观察项维持不扩 scope。

批① 成长化增量归因（系统分解实测）：**PickupSystem avg 1.87ms**（玩家 96px 磁力
查询 + ~450 颗地面宝石自程查询，均扫过万怪密团 cell——候选数是主成本）+ Separation
10.78ms（alive 10467 = 万怪 + ~460 宝石存量，随存量缓涨）。宝石 Spawner capAlive
2000 封顶存量；判据余量充足（16.3 vs 22.2ms），**Pickup 密核扫描列 M5 观察项**
（优化方向备档：宝石侧查询降频/惰性自检属行为变更需单独批；哈希项内嵌 pos 同
Separation 备档路径）。frameMax 29~60ms 仍为 Census 尖刺口径。

scene 段 19.31→1.25ms 的两处根因（都在编辑器侧视口层，非引擎内核）：
1. **ExtractScene 差集销毁 O(N²)**：`seen` vector + `std::find`，1 万实体 ≈ 每帧
   5000 万次比较——改纪元戳（map 值带 lastSeen，`ViewportRenderer.h`）O(N)；
2. **Scene 视口实体名标签全量画**：1 万次 ComputeWorldTransform + snprintf +
   TextWidth + 字形四边形——改视口 AABB 裁剪（+128px 屏幕边距）+ 预算封顶 256 +
   谓词对齐提取（禁用/悬空精灵不画标签，原先是漏网 bug）。

**后续观察项**（留给 M5 性能批续章）：sim 11.14ms 系统级分解（vs bench-sim 5.1ms
的差距在哪几个系统）、frameMax≈31ms 尖刺归因（GC/生成突发？）、ui 4.11ms（万级
Hierarchy 面板）。回归口径 = 本命令三跑稳定。

**性能批②（同日，本批）**：上三项观察项全部闭案，另补跑 `--smoke` 四要素像素
冒烟（批①欠账）。新增裁决输出：`sim系统分解`（每系统 avg/max，测量窗口 =
预热后 ZeroProfiles 起，Stop 前捕获——Play 世界随 ExitPlay 析构）、frameMax 帧
八段快照、每段 max@帧号、尖刺帧(>25ms)分段均值；`LEMON_BENCH_UI_PROBE=1` 额外
打印 Hierarchy 面板占 ui 段百分比。结论：

1. **sim 11.1ms = Separation 10.18ms（91%）**，其余 15 系统合计 <1ms（SpatialHash
   0.50 / AI 0.28 / Movement 0.10）。vs bench-sim 的差距**不是编辑器回归而是负载
   密度**：bench-survivor 玩家静止（无头无输入）+ Spawner range 600 → 万怪压成
   最高密度团，邻居扫描候选数 ~7×；bench-sim 玩家持续走位拖拽蜂群 + range 1500
   始终较散，**稳态** Separation 仅 ~1.4ms（其 5.1ms 判据均值大半来自
   未收敛帧；两侧怪均不死——iFrames 无递减缺陷，见 DevLog 2026-09-22 P0 条目，
   destroyed 计数实为投射物寿命回收）。sim 优化属余量挖掘，路径备档：哈希项内嵌
   pos 省 try_get、cell 内 id 排序提局部性（均行为保持、不动金档）；邻居选择
   策略类优化会破回放金档。
2. **尖刺归因 = Census 缓存污染**：20~22 个尖刺/660 帧 ≈ 660/30 = SpawnSystem
   每 30 tick 全量扫 1 万 Meta（~480KB 驱逐 L2），紧随的 Separation 当帧 10→22ms。
   另 present 929ms@~287 一次性停顿为系统侧（autosave Play 中跳过且 5 分钟节拍，
   已排除；驱动/合成器，非引擎代码）。ui 段修复后尖刺帧降至 2~3 个/跑，
   frameMax 25.2~25.6ms。
3. **ui 4.14→0.25ms（Hierarchy 3.60→0.06ms，16×）**，frameAvg 13.1ms / fps 76。
   两处修（都在编辑器侧）：`testhooks::Stash` 门控（每行 string 拼接 + map 落位
   曾占 ui 段 87%，仅 --smoke-ui 会话开）；万级平铺（>256 根且全叶）走
   `ImGuiListClipper` 只画可见行（DrawNode 拆行体 DrawNodeRow + 子遍历；带父子
   结构或过滤态仍走原递归——混合结构万级平铺的通用扁平化留 M5 观察）。
   uidraw 随之 0.13→0.03ms（顶点量骤减）。

## 7. M2 验收结果汇总（2026-09-19，本机 6C）

| 判据（08 §3） | 结果 | 实测 |
|---|---|---|
| bench-sim 1 万怪全系统 ≤8ms/步 | ✅ | avg **5.10ms**（多线程 5 worker，alive 10053；系统时间 ~2.1ms，余为并行调度） |
| 确定性回放同输入 5 分钟逐帧一致 | ✅ | 18000 帧 × **双档 PASS**（--threads 1 与多线程，mismatches=0） |
| F3 面板数据齐全 | ✅ | 每系统 μs + 实体/事件/池统计（--stats 文本；ImGui M4 消费同一数据源） |

## 7.5 M1 验收结果汇总（2026-09-18，本机 RX 590 / MoltenVK 1.3.357）

| 判据（08 §3） | 结果 | 实测 |
|---|---|---|
| bench-mow ≥60fps（10 万精灵+5 万粒子） | ✅ | 107.0fps（IMMEDIATE），GPU 1.52ms |
| CPU 渲染线程 ≤4ms（压测 A 语境） | ✅ | 3.05ms（1 万精灵+10 万粒子全可见） |
| 粒子 10 万 ≤4ms GPU | ✅ | 0.741ms |
| 多图集不闪帧 | ✅ | 批数恒 4，帧间零波动 |
| 设备丢失自动恢复 | ✅ | 验证层零错误 |

完整分场景数字与优化过程见 `docs/DevLog/` 对应日期条目。

## 7.6 M3 验收结果汇总（2026-09-19，本机 6C，.NET 10.0.12）

| 判据（08 §3，ADR-010 修订版） | 结果 | 实测 |
|---|---|---|
| 5k 弹整步 ≤8ms | ✅ | avg **0.248ms**（max 10.1ms 为首帧分配尖峰；18000 帧全程） |
| 100k 弹净时间比 ≤1.5×（D5 分档） | ✅ | **1.43×**（5k/10k/30k/100k = 1.92/1.67/1.52/1.43×，边际比 1.41×） |
| 确定性回放双档 | ✅ | 5k × 18000 帧 ×（--threads 1 + 4）mismatches=0 |
| 毒脚本不崩引擎 + 60 帧自动禁用 | ✅ | stderr 恰 60 条红字后 disabled，进程不崩；引擎侧回归全绿 |
| 示例脚本托管分配 0（D6 前提下） | ✅ | 0 B / 18000 帧（15/15 进程硬 0，16 帧窗逐窗核对） |
| 卸载自检 | ⚠️ 降级 | unload=pin（已知 runtime 限制，ADR-010 D1/D2 修订；断言固化 + 诊断探针） |
| 断点可下 | ✅ | 诊断 IPC socket + Lemon.Domain 线程名 + mac-debug 全量测试通过（§6.9 证据链） |
| 布局护栏 27/27 | ✅ | lemon-script-tests：27 组件 / 91 字段 / 3 数组段 + blit roundtrip + PCG32 golden |
| 脚本桥回归 | ✅ | lemon-script-tests **1264 checks**；lemon-tests **12830 checks**；M2 bench-sim 重录金档双档 PASS（threads=1/4 各 18000 帧 mismatches=0；threads=4 avg 3.21ms ≤8ms） |

规模阶梯（--cpp-compare，--frames 3000）：10k avg 0.445ms / 30k 1.210ms / 100k 3.829ms
（全部 ≤8ms 判距内；100k max ~47ms 为 reserve 首帧大分配尖峰，avg 判据不受影响）。

## 8. 已知未覆盖（记录在案）

- **editor-regression smoke-ui 飘忽（既有，M5 批① 期间定位）**：`--smoke-ui` 的
  rename/scrub 子断言（帧序号注入的像素坐标点击/拖擦）约 2/6 概率 miss——基线
  （批⓪ 代码 stash 复测）同样复现，非批① 引入；批⓪ 的 11/11 为侥幸通过。门禁口径
  = 重跑至全绿（飘忽在子断言级，非产品缺陷）；根治属编辑器测试基建（坐标注入改
  控件命中断言/重试语义），登记 M5 待办。
- Dock 最小化时 acquire 的 0 尺寸分支（resize 压测已覆盖退化尺寸的相邻路径）。
- 多窗口（M1 范围外；M4 决议 #7 定单窗口 docking 交付，multi-viewport 补课顺延 **M5+**）。
- Windows 平台（CMake 预设 `win` 待加，见 README）。
- 编辑器交互覆盖边界（M4.md §6 提案已落地）：无头冒烟 `--smoke`（含 overlay
  可见性像素断言）/ `--smoke-close`（关闭状态机）/ `--smoke-drag`（视口注入五段）/
  `--smoke-anim`（切片 + clip + Animator 帧映射链，M5 批③起）/ `--smoke-template`
  （向导复制模板 → build → Play → HUD/存档/波次/击杀/升级卡片断言，M5 批④起）/
  `--final`（终验链）经 `tools/editor-regression.sh` 一键 13 步；**未脚本化**的纯观感路径
  （Inspector 控件手感、面板排版）仍靠 [Editor-Manual-Test-Guide.md](./Editor-Manual-Test-Guide.md) 真人清单。
- M2：bench-sim 早期怪群聚拢阶段存在 max ~145ms 单步尖峰（avg 判据不受影响；哈希重建
  并行化是已预留路径，触发 M5 压测 B 再做）；投射物穿透去重用全量 iFrames 策略（M5 细化）；
  Patrol 端点暂停未实现（M5 随 clip 资产补）；~~Animator 帧映射待 clip 资产表（M5）~~ 已落（批③，03 §8.1）。
  Director/Navigation/CSharpBatch 三系统为占位空跑（M5/M6/M3 激活）。
- M3：ALC 卸载 pin（runtime 限制，M4 走整域重建，ADR-010）；30k+ 首帧 max 尖峰
  ~45–47ms（块缓冲 reserve 一次性 malloc，avg 判据不受影响；惰性分帧预留是可选优化）；
  脚本调试 lldb 原生 attach 被本机 macOS 策略拒绝（托管附加不受影响，§6.9）；
  金档跨 .NET 版本不保证（D3，随引擎分发收紧 rollForward 后消除）。

## 9. 更新约定

- 新测试跑完：在 `docs/DevLog/` 新增条目 `YYYY-MM-DD-<slug>.md`；方法若可复用，写入本册对应节。
- 新里程碑：验收判据在 08；本册 §7 换新表，§2 程序表补新 bench（M2 将加 bench-sim）。
- 性能回退 >10%（对 DevLog 上一基线）视为阻断，先定位再合入。
