# M8 批⑥ 完工中 —— 压测 A 终测 + soak 2h + #L5 探针（mac 侧机器面）

- 日期:2026-10-12(开工当日 A/C 收口、soak v5 挂机收表)—— **2026-10-13 凌晨对照跑 A 收表判读 = 引擎/C# 逐帧真泄漏,缺陷单已立(08 债表 #R1)**,见 §5
- 批文件:[Plans/M8/2026-10-12-b6-stress-a-and-soak.md](../Plans/M8/2026-10-12-b6-stress-a-and-soak.md)
- 基线:HEAD 3a57452(批⑤ 走查支持期修复收口后),回归 full 26 步全绿 fps=79 / 单测 34,919 / script-tests 1,911 / ctest 4/4 / 金回放三档 mismatches=0

## 1. 压测 A 终测(A1/A2/A3)—— ✅ 三跑全 PASS

- **A1 三跑**(冷机/热机/复跑,`--bench-survivor-lit --frames 900 --no-reopen`,LEMON_NO_ACTIVATE=1):**fps=91/90/91**(门禁 ≥60),alive=10507(≥10000)、lights=64(≥64)、fxPass=5/5、frameAvg≈11.0ms / frameMax≈19ms。
- **A2 bench 三口径重锚 = 全维持原值**:同日读数 bench-survivor **fps=92**(门禁 76.5,余量 +21%)、bench-lights **881**(软门禁 55)、bench-survivor-lit **90–91**(门禁 60)。批⑤ 尾态 74/76 判热态环境噪声成立(今日冷热三态一致 90+;上调门禁反而放大热态 flake 面,76.5/55/60 = "基线回归 <10%"口径自洽)。`editor-regression.sh:263` 与 `ci-daily.sh` 零改动。
- **A3**:#M4(批量纹理上传 Windows 真机验证)与 mac 数字解耦——切换只影响 Windows 运行路径;Windows 轮归 D 组独立三态对比。

## 2. #L5 探针(C1/C2)—— ✅ 不成点,关账

`TestLightStateHashCostProbe`(DayNightCycle 同款 intensity+color 逐帧双字段写,sink 异或防优化;单测 34,919→34,922):ctl-0=0.5µs/帧、**real-10=1.6µs/帧**(增量 1.1µs ≈ 阈值 500µs 的 0.2%)、stress-10000=1119µs/帧(纯线性 ≈0.11µs/灯;压测 A 真实上限 64 灯 ≈7µs)——四数量级余量,组件级预哈希不立项;基准场零 Light2D 实例,金回放零影响(批① 先例)。

## 3. soak 2h(B1)—— 七轮挂机史与 v5 定型

工具/环境坑全记录(防复踩):

1. **present 路径卡死(第 2 轮)**:主线程全堵 `CAMetalLayer nextDrawable` 信号量——显示器睡眠/窗口全遮挡 → WindowServer 停止合成 → drawable 不返还 → present 无限等,主线程连事件轮询都停。环境性伪影非引擎缺陷(sample 取证)。对策:`caffeinate -is` + 窗口保持至少部分可见。
2. **无头局站桩死亡(第 3 轮起)**:玩家被围殴致死 → 世界暂停 → soak 退化空转。对策:项目副本抬玩家 HP 至 1e9(仅改副本,工作项目零触碰)。
3. **误判关窗(第 4/5 轮)**:"合围稳态"画面肉眼近似静止,两次被当作卡死手动关窗;栈采样实证世界健康步进(World::Step/Batch 主导、Gem/Mob 持续生成)。对策:窗口标题改「M8-soak挂机压测·勿关」+ 活性判定改日志生成计数器。
4. **runner 随会话被收(第 7 轮 v4)**:macOS 无 setsid → nohup 双层 + zsh `&!` disown,脚本/游戏双脱离。
5. **真根因 = `--vsync` 缺失(v5 落地)**:`paced = frames==0`(GameEntry.cpp)→ `--frames N` 自动化走 Immediate 不限速(实测 422fps)→ macOS 合成器对不限速灌帧节流 → `nextDrawable` 长阻塞 = 与第 2 轮同族症状。**`--vsync` 旗标落地**(`(paced || vsync) ? Fifo : Immediate`,实测 61.4fps = 60Hz 真实节奏;随 C1 探针同批待提交)。

**v5 终形**:`lemon-game --project <god-HP 副本> --scene Scenes/Grass.scene --vsync --frames 600000`(600k/61.4fps ≈ 2.7h,双判据 ≥7200s 且 ≥432k 帧)+ caffeinate + LEMON_NO_ACTIVATE=1 + RSS 60s 外部采样 + 自然退出记 rc。

## 4. soak v5 结果—— 硬判据全过,B2 内存曲线红旗

| 判据 | 结果 |
|---|---|
| 墙钟 ≥7200s | ✅ **10022s**(2h47m,17:04→19:51) |
| 帧数 ≥432k | ✅ 600,000 帧 |
| fps | **60.4**(vsync FIFO 真实 60Hz 节奏) |
| 干净退出 | ✅ rc=0 自然退出 |
| B3 崩溃转储 | ✅ DiagnosticReports 零新增 lemon-game(最新条目停在 10-11) |
| **B2 RSS 曲线** | ❌ **不平**:稳态段(剔前 30min 预热)181→282MB,**完美线性 +44.5MB/h(R²=1.0000,每 60s 恒定 +760KB ≈ 12.7KB/s ≈ 210B/帧@60.4fps)**,最后 1 小时仍 +43.8MB 无平台化 |

日志侧线索:整场 166 分钟仅记录 288 次 FlySword、31 次 Gem、2 次 Blade 实例化(≈每 35s 一发弹)——击杀稀少 ⇒ **活怪净累积嫌疑最大**(导演持续刷怪、capAlive=1500,击杀速率低);但实例化日志可能被池化遮蔽,外部无法确证。涨幅与"恒定刷怪速率 − 低击杀速率 = 线性实体增长"形态吻合;确定性逐帧小分配(~210B/帧)泄漏假设不能排除。**(2026-10-13 §5 对照跑收口:刷怪堆积假设排除,真泄漏坐实。)**

**⚠️ 证据保全事件(2026-10-12 ~21:00)**:机器重启致 `/tmp/m8-b6-soak/` 清空——v5 原始 rss.csv/日志/取证 sample 全部丢失,上表数字为本 DevLog 从收表当场的判读记录转移保存(斜率/回归/首末读数完整,逐点原始 CSV 不可复得)。对照跑 A(§5)全部产物重建于重启后。

## 5. 对照跑 A —— 归因实验(进行中)

**设计**(与 v5 唯一变量 = 刷怪通道):`cp -R demo/svr-test → /tmp/m8-b6-soak/svr-ctl`,摘除 Grass.scene 的 **Director 实体**(4→3 实体,零刷怪;god-HP 不需要——无怪即无伤害),昼夜/灯池/blob/FX/Player/Blade 环绕刃全保留;600 帧冒烟过(60.8fps 干净退出)。窗口标题「M8-soak对照A·勿关」。

**判别窗口修正**:首排 30min 版(108k 帧)启动 8 分钟后发现设计缺陷——v5 的涨幅判读窗口是预热 30min 之后,30min 对照全程处于预热段判不出稳态斜率(前 8min 轨迹 162→169MB 与 v5 同期基本重合,仅说明预热段增长与刷怪无关);杀掉重启 **60min 版(216k 帧,21:43 起跑)**。

- 判据:t∈[20min,60min] 斜率 vs v5 稳态 0.74MB/min —— **≈0 → 刷怪侧堆积(场景性,B2 判场景豁免)**;**≈0.74MB/min → 引擎/C# 逐帧真泄漏(立缺陷单)**。
- **结果(2026-10-13 凌晨收表):真泄漏成立,缺陷单已立(08 债表 #R1)。** 02:24–03:25 跑完 60min 版(216,000 帧 @60.4fps,**rc=0 自然退出,与 v5 逐位同节奏**;墙钟 61min)。判别窗回归:**t∈[20,60]min 斜率 0.742MB/min(44.49MB/h)R²=1.0000,与 v5 稳态 0.74MB/min 比值 1.00**——摘除 Director(零刷怪)后斜率分毫不差,刷怪堆积假设排除;[30,60] 与 [10,60] 窗同斜率(44.49/44.41MB/h),线性自启动即成立。折算 **≈215B/帧**@60.4fps(与 v5 估的 210B/帧同量级同源)。首末读数 168.5→204.5MB;对照窗内 DiagnosticReports 零新增 lemon-game 转储;对照日志整场仅 2 次 Blade 实例化(开局环绕刃),生成通道确认死透 ⇒ **泄漏位于每帧固定路径(场景步进 + DayNight 逐帧灯写 + 灯池 + Player 环绕刃 + FX + UI),与实体数/战斗无关**。
- **实验纪律补记(二进制钉源)**:备制后 HEAD 前进(b4eb203 动画族复名碰 Engine 运行时文件,当前 build/mac 二进制 ≠ v5 当日),为守住"与 v5 唯一变量 = Director"的归因设计,worktree 钉 `7453595`(= v5 当日源码态:3a57452 + vsync 旗标 + C1 探针)重编 lemon-game + dotnet runtime(ccache 缓存命中,600 帧冒烟 60.8fps 干净退出,与首排冒烟同值);对照跑全程用钉版二进制 `/tmp/m8-b6-soak/lemon-v5-tree/`。副本与 12min 部分数据(`rss-ctl-partial-12min.csv`)经重启幸存,直接复用;完整曲线 `rss-ctl.csv` 61 点留档 /tmp(/tmp 易失,关键数字已全文本段落本 DevLog)。
