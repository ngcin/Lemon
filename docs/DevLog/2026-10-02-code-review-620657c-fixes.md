# 代码审核修复批：37 条确认问题全落码（基线 620657c，报告全勾销）

- **日期**：2026-10-02
- **输入**：[代码审核报告 2026-10-02-code-review-620657c](../Reports/2026-10-02-code-review-620657c.md)（M6c 音频 + M7a 前置 18 提交、10 组评审 + 独立复核；37 条确认 + 1 条未复现）。
- **结果**：**37/37 确认条目全部处理**（35 条代码修复 + 2 条注释/文档勘误类按报告建议落注）；#38（MSVC `-w`）复核认定无害（cl 的 `-`/`/` 等价 + `/w` 文档化），不动。门控全绿：engine-tests **34040 → 34056**、script-tests **1771 → 1775**、ctest 3/3、回归 full **17/17 首跑全绿**（两轮）、smoke-audio 单跑 `entries=1 baked=1 meta=1 preview=1/1 voices=1/0 => OK`。

## 修复分批（编号 = 报告 §4 全文唯一号）

### A · 音频引擎核心（Engine/Audio）

- **#14 pausedAll 引擎层收口**：`Shutdown()` 复位 pausedAll/streamUnderruns_/underrunWarned_；**`StopAll()` 一并复位**（全停 = 会话清场语义，AudioChannel 的 StopAll 命令同步清 `paused_` 记账）——一处修掉两条高危的引擎侧根因，M7a 独立运行时不再依赖宿主打补丁。
- **#3 音高微扰除数**：`8388607.0f`（2^23−1）→ `16777215.0f`（2^24−1），u 回到 [0,1]，扰动区间复原 ±range（原实为 −range..+3range 整体偏尖）。
- **#4 回调阻塞链剪断**：流式声部挂填充队列从 PlayLocked（持 mtx）内移到 `Play()` 的 **mtx 外**——原链条「设备回调等 mtx ← PlayLocked 等 fillMtx_ ← 填充线程 fread 256KiB」使音频回调被磁盘 IO 间接卡（ADR-015 M4 短临界区前提失效）；预填整环 ≈1.4s 余量下 job 迟到微秒级无害。
- **#15 偷声部包络覆盖**：per-clip 超限 victim 选择**跳过被偷槽位**（池满偷槽在先，victim 恰为该槽时刚设的 5ms 释放包络随即被新声部字段整体覆盖）；偷声部硬切残余以注释交底（固定池无槽容纳释放尾巴，常态饱和由 per-clip 路径治理）。首版误改成「跳过释放中声部」被 `oldest oneshot stolen` 既有断言打回——ADR「偷最旧」语义不变，只修覆盖位。
- **#16 StageBgm staging 替换/撤销**：同 tick 未提交的上一条 Bgm 直接撤命令 + 销记账（引擎从未起声部）；StageBgmStop 同款撤销（净效果零，已提交曲仍走淡停）。
- **#18/#19 契约注释**：`Clear()` 不再归 1 nextId_（逻辑句柄单调不回收契约）；Stop 命令注释勘误（实际同次 Submit 末尾回收即清，不跨 tick）。

### B · 高危暂停语义（游戏侧 + 编辑器挂点）

- **#1/#2 ReturnToMenu 复位 `Audio.Paused`**：模板与 svr-test 双处补「先解挂再停曲」（残留 pausedAll 使下局 PlayBgm 新声部生而挂起 = 整局静音）；引擎层兜底见 #14。
- **#9 重编译重试 Play 补双挂点**：模态内直调 `ctx_.EnterPlay()` 改走 `TryEnterPlay()`（统一 MountPlayAudio/WirePlayAudioBackend——原路径新 World 的 audioSink_ 为 null、C# 音频命令纯记账整段无声）。
- **#27 StopPlay 清 preview 记账**：残留 previewVoice_ 让下一次双击同资产命中「同曲再点 = 停」静默空操作（第一次点击无声）。
- **#28 paused_ 会话复位**：TryEnterPlay 成功即归零——「画面冻结、BGM 照响、暂停钮亮着」的 sim/音频错位态根除。

### C · 编辑器资产与冒烟

- **#5 换片失败不再静默**：坏 guid 一次性告警（复用 warnedClipMiss_ 旗）；池满/节流可恢复拒绝**回滚 guid 记账制造失配、下 tick 重试**（与起播路径对称）。
- **#7 removed 事件可达**：`Rescan()` 保留 Remove() 预入队的 removed（「保留至被消费」语义 + `ConsumeChange()` 消费即清）——GPU 幽灵页回收「即时触达」承诺成立；首版纯保留会造成跨重扫重复 Evict，消费者取走清零堵死。
- **#8 meta 热改生效**：音频 importer 段变更（loop/preload）→ Rescan 判 modified（触发后台烤制）+ `BakeStale` 计入 .meta mtime（loop 冻结在 .baked 头，不重烤永不生效）。
- **#25 tooltip 磁盘 IO 缓存**：音频烤制状态按 guid+源hash+.baked mtime 三键缓存（悬停每帧 fopen/fread + 头坏每帧重复红字 → 仅变更时重读一次）。
- **#24 引用语料增量缓存**：单一大串每次 Rescan 全项目重建 → 逐文件 {mtime,size,text} 缓存、只重读变更/新增、消失出缓存、查找逐文件免拼接大分配；切项目全失效。
- **#37/#11/#10/#29 冒烟面**：播种写盘查错（wav 失败不再误报为用法错误，fail-fast）；metaOk 纳入裁决（>=1——importer 段写入这条验收点入回归面，全量 ==entries 不采用：老 meta 缺段是既有容错）；smokeAudio 补进 injectionSession 守卫（不再污染 recent.json）；liveAfterEnter==1 的隐含前提（编辑场景无其他音频活动）注释交底。

### D · 模板/工具/SDK

- **#26** 模板音频拷贝查 ec（缺件即断纪律补齐——拷失败原会产出「成功」的哑模板）；**#30** 回归残留实例守卫 `cmd%% *` → `ps -o ucomm=`（build 路径含空格时守卫静默失效）；**#31/#32** 音量滑条落盘去抖（拖动只应用+回显，`volDirty` 收口到 CloseSettings 统一 Flush——每次满拖 ~20 次全档同步 IO → 1 次；开关翻转仍即时落盘，settings.sav 断言面不变）；**#17** AudioSource.Group 镜像默认值坑双侧重注（Flags 同款家族第二例）；**#21** SDK Play 注释勘误（池满/节流 = 非零句柄即刻失效，非返 0）。

### E · 测试增强（断言真空/挂死/撞号收口）

- **#12** fade 终结断言去 mask（v1 改循环声部——一次性版自然终点先亡，FadeVoice 失效也绿）；**#13** 双流声部叠加对拍帧 0→1（原期望 2×sin(0)=0 恒真）；**#36** 收尾暂停计数 `>=2` → `==3`（确定性恰 3）；**#35** 联动限幅区分性用例（立体声 L 满格/R 低幅：L 过膝单增益拉低 R——独立限幅 R 原样，两实现首次可区分）；**#33** SPSC 锤序错继续排空（首错即 return 会让生产者环满忙转、join 挂到 ctest TIMEOUT 而非红字）。
- **#22/#23/#34** AudioProbe 重排：号段迁 1500..1559（原 1300..1307 与 AnimFx 1300/1400 撞段、mark3 无上界）+ 帧4 拆出；`SetGroupVolume` 首覆盖（引擎侧断言 GroupVolume(Bgm)==0.25）+ D5 Paused 的 C# staging→落地链真断言（pausedStaged 读写 + 全局暂停下 Bgm 循环挂起态出生占槽恰 2）。

### F · 文档

- **#6** 04 分册 native 函数表台账补 M6c 条目（36→46 槽：竖切 4 项 + 批② 6 项实名、AudioHooks 退役、Lemon.Audio/AudioSource 镜像与默认值坑）；「Audio 随 M4/M5 落地」过期句更新。

## 发现

1. **测试是语义契约的守门人**：#15 首版「偷声部跳过释放中」自认更优，被 `oldest oneshot stolen`（ADR-015 M4 白纸黑字）当场打回——修复必须贴报告指控的最小面，顺手「优化」选择语义会破既有契约。
2. **「保留至消费」需要消费侧闭环**：#7 首版只做 Rescan 侧保留，自查发现磁盘删除路径会跨重扫无限重报——事件传递语义 = 生产侧不丢 + 消费侧取走清零，两半缺一不可。
3. 报告建议的「引擎层收口 pausedAll」一次修掉 #1/#2/#14/#28 四条的公共根因，比逐宿主打补丁便宜一个量级——审核建议排序（§1 按影响）经验证成立。

## 落账

报告 [2026-10-02-code-review-620657c](../Reports/2026-10-02-code-review-620657c.md)（快照存档不改）；04 分册台账（#6）；本条目。
