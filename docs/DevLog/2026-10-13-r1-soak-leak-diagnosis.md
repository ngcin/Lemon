# M8 批⑥ #R1 —— soak RSS 线性泄漏定位报告(修复前置归因)

- 日期:2026-10-13(对照跑 A 判读收口后,用户指令"修复"当日)
- 输入:[批⑥ DevLog](./2026-10-12-m8-b6-stress-a-and-soak.md) §4/§5(+44.5MB/h R²=1.0000,零刷怪斜率同值 ⇒ 引擎/C# 侧逐帧泄漏假设成立)
- 结论先行:**引擎全部自有内存面干净(malloc 堆/GC 托管堆/GPU 驱动堆零增长);增长实体 = 64KB VM_ALLOCATE 区连续累积,分配现场在 macOS SkyLight 私有框架内(main-thread dispatch 块),`leaks`=0(有引用活数据);分配绕过用户态 hook(私有绑定/直 trap)——本机工具链(CLT,无 Xcode/sudo)取不到完整栈。修复前置的最终归因在本机不可达,未做盲修。**
- 状态:#R1 维持挂账;下一步三选项见文末,待用户拍板。

## 1. 定位方法链与判读

| 步骤 | 工具 | 结果 |
|---|---|---|
| malloc 堆差分 | `MallocStackLogging=1` + `malloc_history --callTree` 两拍(90s 间隔) | **零增长**(两拍全树合计 15.1MB 持平,增长仅数百字节 JIT 噪声)——引擎/SDL/RmlUi/FreeType/miniaudio 的堆全排除 |
| 托管堆 | `dotnet-gcdump` 两拍 | GC 对象 116KB→365KB(+2.8KB/s,全部为运行时内部:JIT 元数据/EventSource 表);与 RSS +26KB/s 差一个数量级——**C# 业务对象泄漏排除** |
| VM 区差分 | `vmmap --summary` 两拍 | **VM_ALLOCATE +2.85MB/120s = 唯一增长区**;MALLOC_*/IOAccelerator/IOSurface 全平。区数不变、地址连续向上爬 |
| 区级 diff | `vmmap` 全量两拍 | 100s 内新增 44 区,41 个恰 64K、rw-/rwx SM=PRV、全 dirty,连成带状(~每 2.5s 一块)|
| 无引用泄漏 | `leaks`(MSL 进程)| **0 leaks**——增长内存全部有引用(活数据结构,非孤儿块)|
| 分配现场 | lldb 断点 `mach_vm_allocate` 条件 `$rdx==65536` | 栈:`mach_vm_allocate ← SkyLight'SLPerformWithVMAllocation+84 ← SkyLight'SLSSetKeyTranslationTableFromPtr+165`——**3 帧即熄**(main-thread dispatch 块,rbp 链断,上层不可走;+165 为最近邻符号,归属有不确定性但确系 SkyLight 内部)|
| 用户态拦截 | DYLD interpose(重签名解锁 inject,spike 上验证有效)| 仅拦到启动期 malloc-zone 初始化的 3 次 64K;**泄漏期 0 次** ⇒ 分配不经公开符号动态绑定(私有绑定/直 syscall),用户态栈捕获路线穷尽 |
| 其他 | `sample` 60s(0 命中,调用 duty <0.1%)、`xctrace`(需完整 Xcode,本机仅 CLT)、lldb Python 绑定(CLT python 版本不配)、sudo kdebug(无密码 sudo)| 全部不可用/未命中 |

## 2. 触发矩阵(场景级二分,激活启动系——可信)

复现环境:`/tmp/m8-b6-soak/svr-min`(svr-ctl 的净拷贝)+ HEAD 二进制,`--vsync --frames 0`,默认激活启动,vmmap 计数 64K 全 dirty 区,60s 窗:

| 变体 | 场景内容 | 结果 |
|---|---|---|
| full(=Grass.scene) | PlayerBehaviour + DayNightCycle 双脚本全功能 | **漏 +22 块/60s** |
| Empty.scene | 零实体(GameEntry 装配链 + CLR + RmlUi + 字体全在位)| 净 |
| PlayerOnly(T2r)| 仅 PlayerBehaviour(含 Ally 生成/环绕刃/HUD Ui.Set×5/帧/事件订阅)| 净 |
| DayNightOnly(T3r)| 仅 DayNightCycle(Tween.Alive 每帧 + 20s 补间循环 + Save.SetString)| 净 |
| DayNight + UiEcho@Ground(E)| 中性第二脚本(仅 UI 事件订阅)| 净 |
| DayNight + UiEcho@Player(G)| 同上,挂 Player 实体 | 净 |
| spike-01(SDL+Vulkan 基线)| 无引擎/CLR | 净 |
| bench-mow(引擎渲染,无 CLR)| 全渲染栈 + 窗口 | 净 |

**要点:泄漏需要 PlayerBehaviour(全功能)× DayNightCycle(全功能)同时在场;任一单独、或以其他脚本替换、或空场景,均不漏。** 每帧折算 ≈430B(64K 块为分配粒度,底层数据逐帧增长)。另有无焦/全遮挡会停漏的观察(t6 零块 vs t4 无焦仍漏——窗口可见性疑似第二门控,自动测量下不可完全控制,置信中等)。

**引擎侧嫌疑收窄为"同帧双写源"**:**native tween ApplyValue(TweenTable::Advance 写 Light2D)与 C# SetComponent(Transform/SpriteRenderer)同帧并存是 full 独有组合**——但引擎 C++ 堆零增长,该组合诱发的分配发生在系统框架侧,机制未证(无栈)。

## 3. 过程教训(防复踩)

1. **C# 场景脚本预编译缓存**:`Game/bin/Game.dll` 一次编译、改 `Game/*.cs` 不触发重编(log 实锤:`脚本类型未注册(跳略)'ZzDummy'`)。本轮前段所有 .cs 级二分(空壳/摘 HUD/摘 Ally 等"仍漏"结果)全部作废——实跑原版=full。**场景级摘挂(scripts[] 字段)才可信。** 诊断 svr-test 派生项目时,改脚本必须先删 bin 或验证重编。
2. **lldb attach 的输出是块缓冲**:进程不退出就看不到输出,"挂起"假象;lldb Python 绑定在 CLT 上与系统 python3 不配;`timeout` 命令 macOS 无(GNU coreutils)。
3. **interpose 在 malloc 锁内 backtrace 会死锁崩溃**(malloc_zone_register_while_locked 路径),需启动时间窗回避;ad-hoc 签名二进制默认拒 DYLD_INSERT,`codesign --force --sign - --entitlements`(allow-dyld-environment-variables + disable-library-validation)可解锁(仅诊断副本,build 产物未入库)。
4. vmmap 全量输出 VM_ALLOCATE 行的 dirty 判据:`[ 64K 64K 64K` 三列(virt/resident/dirty)。

## 4. 产物与复现包(`/tmp/m8-b6-soak/`,/tmp 易失——关键数字已全部文本化于本 DevLog 与 §1 表)

- `svr-min/`(净拷贝 + 场景变体族:Empty/Noscript/PlayerOnly/DayNightOnly/DnEcho/DnPlayerEcho/HZz/H16 + Grass 原版)
- `interpose.c/.dylib` + `lemon-game-diag`(重签名诊断副本)+ `ent.plist`
- 两拍证据:`r1-snap1/2.txt`(malloc call tree)、`r1-vm1/2.txt`、`r1-full1/2.txt`(vmmap 全量)、`gc1/2.gcdump`、`r1-leaks1.txt`、`diag-*.log` 全程日志
- 复现一条龙:`cd /tmp/m8-b6-soak && <lemon-game> --project svr-min --scene Scenes/Grass.scene --vsync --frames 0 &` 后每 60s `vmmap <pid> | grep -cE '^VM_ALLOCATE.*\[ +64K +64K'`,激活启动约 +22 块/min。

## 5. 下一步(三选项,待用户拍板)

- **A 装 Xcode → Instruments/xctrace Allocations 一发归因**:VM 分配带符号栈,直接指向调用 API;归因后决定引擎侧规避(如替换/节流该调用)或报 Apple。工具到位后半天内出结果(本 DevLog 复现包已备)。
- **B 按"系统框架侧缺陷"收口**:引擎自有内存零增长的证据链完整(§1);#R1 改判环境豁免(同第 2 轮 present 卡死先例的环境性口径),M8 出口判据"soak 内存曲线平"按"引擎侧无泄漏 + 系统侧挂 Apple 案"对表——需用户认可口径变更。
- **C Windows 轮顺带反证**:批⑥ D 组(#M4 Windows 真机轮)跑同款 60min 对照,win 上不漏 = mac 特有环境性的旁证(与 B 互援)。

建议:**A**(根治,成本 = 装个 Xcode);不想装则 B+C 组合。
**（2026-10-14 追记:选项 C 已执行完毕——Windows 同构 61 分钟私有内存零斜率(+1.5~2.7MB/h 噪声 vs mac +44.5MB/h R²=1.0),mac 特有性坐实,收口建议见 [Windows 反证条目](./2026-10-14-r1-windows-counterproof.md)。⚠️ 本条目 §4 复现包位于 /tmp,已随 10-13 19:30 mac 重启清空(关键数字已全部文本化;VM 侧 C:\lemon-work 完整保留)。)**
