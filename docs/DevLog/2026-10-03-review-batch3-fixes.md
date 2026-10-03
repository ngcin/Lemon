# 批③ review 2fce9f0：low 真缺陷 10 + 冒烟卫生 5 + #29/#61/#63 收口

2026-10-03 · 修复批③（报告 `docs/Reports/2026-10-02-code-review-2fce9f0.md` 3.3
low 段——按纪律逐项证据自查后修；冒烟卫生段；#29/#61/#63 并入）

## 引擎真缺陷（low 10 项）

- **#46** `RmlUiBackend::Init` 签名改 `bool(rhi::Device&, rhi::Format)`：
  幂等（先 `if (impl_) Shutdown()`——重复 Init 原为泄漏重建）；格式不支持返
  false。`UiSubsystem` 消费：Init 失败短路不再装 system/render interface。
- **#48** `SceneArchive::ReadEntity` void→bool（非对象/缺 components 键返
  false——Save 恒写该键）；`Load`/`LoadEntityTree` 坏条目丢弃（Destroy +
  remap 置 Null + `CommitDestroys` + LEMON_WARN），guid 改写环逐项判空。
  坏档从"半装配场景"变"干净丢弃 + 响亮警告"。
- **#49** 同文件契约注释收敛（#48 顺带）：Save/Read 的 components 键与容错
  语义写明。
- **#50** `Scene.h` View/Pool/FromEntt-ToEntt 注释改写为实况（编辑器抽取/
  冒烟夹具；#43 pimpl 化已知越界债；新代码 prefer Each/TryGet）。
- **#53** `CoreCLRHost`：重试路径 `fxr_` 复用（原 `new Fxr` 泄漏）；
  getDelegate 失败补 `close(ctx)`。
- **#64** Chase 并行段弃回源 registry 直读：`TargetBoard/Grid::Nearest` 加
  `outPos` 回填（三处胜者点），Chase 拿快照 pos 算向量——并行契约（03 §4
  条款 2）自洽。Flee/Shooter 串行读合法不动。
- **#68** `BakedClip` 帧换算 `secToFrameClamped`：float 域先钳再转 uint32
  （NaN/越界不再走 UB cast；正常值与原算术位同）。
- **#70** `AudioEngine::StreamFeed` 死字段 `consumed` atomic 删除。
- **#71** `Audio.Paused` getter 补桥：vtable **表尾追加** `audioPausedGet`
  （基准场零调用——金录像零重录约定）；`AudioEngine::IsPaused()` 锁内读；
  C# `AudioPausedGet` 表尾镜像 + null 容错；`TestScript` fc4 探针 mark
  1552/1553 + `tests/script` 断言"Paused getter 读引擎态为 true"。
- **#75** `SdlImeHandler::Reset()`：OnDeactivate/OnDestroy 清 `ctx_`——
  退活后 IME 回调不再引用已析构上下文。

## #61 timeScale=0 冻结泄漏（golden 前置检查后修）

- 前置检查（金录像零重录纪律）：模板玩法只有 WaveDirector 无 Spawner；
  repo 内 SetTimeScale(0) 测试自 t=0 冻结（波未起）→ **记录路径零行为变化**。
- `DirectorSystem::Tick`：`wd.time += dt` 后 `dt<=0 continue`（冻结期不再
  推波起始/持币 stale census）；`SpawnSystem::Tick`：deferred.reserve 后
  `dt<=0 return`（快 spawner `interval<=dt` 冷却钳 0 = 每 frozen tick 泄
  burst + RNG）。
- 新增 `TestSpawnFreezeNoLeak`（interval=0.001/burst=2，冻结 60 tick 零
  新生——WaveSpawnCounter 只收 prefabId=1，夹具补 `prefabId=1`）。

## 冒烟卫生 + #29 去重

- **#85** `EditorAppSmokeUirml.cpp` 重复 include 删除。报告子项"两文件同串
  异名 static 常量"**不可复现**（多轮 grep 未证实——low 段未复核标注的活
  例，如实登记）。
- **#80** `SubtreeSizeOf` static 化 + 动态栈 + **活数守卫**（子树不可能大于
  AliveCount，超即环 + LEMON_WARN；首版"栈深≥64"误报宽树，已纠正）。
- **#81** 夹具 GUID 常量单源：`EditorAppSmoke.h` `kSmokeSpriteGuid` 常量块
  （注明与 TestScript.SpawnerBehaviour.kSpriteGuid 镜像关系），21 处 JSON
  hex + 1 处数值插值。
- **#84** `SeedBenchSurvivorScene` 基号参数化：调用方传
  `SpriteCount()+1`（与 OpenProjectPipeline 同式推导——原硬编码 100 与图集
  页数隐性耦合）。
- **#82** 程序化进 Play 装配单源 `EnterPlayProgrammatic()`：EnterPlay +
  **paused_ 复位** + MountSceneUiDocuments + MountPlayAudio +
  WirePlayAudioBackend。原 TryEnterPlay 封装与四处手动拼装并存且步骤面漂移
  （--play 漏 paused_ 复位；smoke-uirml 漏 paused_/音频两步；bench-* 漏三
  步）。四处调用点归一；`TryEnterPlay` 重构 = 守卫 + 单源；守卫
  （PlayBlockedByScripts）留调用方——--play 红字退出 1、bench 无 Game/
  合法形态不设守卫。
- **#29** 三组近逐字重复收口：催命贴脸/压血停火（SmokeTpl 一死/二死各两
  份）→ `ArmDeathPressure`/`TeleportChasersToPlayer` 单源；bench
  survivor/scene 打印块（各 39 行仅前缀不同）→ `BenchPrintSegAvg`/
  `BenchPrintSimBreakdown`/`BenchPrintSpikeAttribution`（tag 参数化，输出
  逐字节同旧——09 §6.10 台账口径不再有分叉面）。

## #63 Pool<T>：保留 + 纠偏 + 防护（而非删除）

- 删除否决：01 §内存分配行与 03 §10 把 `Pool<T>` 点名为引擎内建纪律（采纳
  面 = 投射物/掉落物/音源实例，均未落地——是"设计在前未采纳"，非死代码）；
  为零用户接线 DestroyCommit 也属投机。两测试（TestPool/
  TestVerifyPoolSlotReuse）保留覆盖。
- 头注释纠偏：删除"FlushReleases()（DestroyCommit 阶段调用）"这一**不存在
  的集成点**表述，改为归还时机归持有方。
- 双重归还防护：逐槽 `live_` 位（O(1)；LEMON_ASSERT 恒生效故不能 O(n)
  find）——Release/DeferredRelease 断言活槽，双重归还/死槽 defer 即响亮
  abort（此前双重归还 = 同 idx 双入 free_ → 后续两次 Acquire 同槽别名）。

## #100 模板 GUID 单源化 + 副产：生成器↔模板漂移同步

- `VsTemplateGen` 新 `FillGuidPlaceholders`（全常量表 `{GUID:name}`→hex），
  4 个 writer 原文包一层；17 处正文占位符（GameMain/GameFlow/PlayerCombat/
  PlayerHud）。C# 侧不再手写 16 位 hex。
- **副产发现**：生成器 GameFlow 原文落后于入库模板（缺 hand-patch"暂停解
  挂 Audio.Paused=false"与"#31 音量去抖 FlushVolIfDirty"）——即入库模板
  已不可再生。生成器同步至入库内容后验证：`--gen-vs-template` 再生 Game/
  与 Assets/ **逐字节一致**（Prefab 实例 GUID 每次随机 = 既有行为）。

## 挂账

- **#69**（AudioEngine tick 内 ~0.1% 微优化）延后：改法涉及 RT 音频回调
  路径，风险/收益不成比例。
- **#85** 第三子项不可复现（见上）。
- CJK 字段名机器覆盖继续遗留（批① 已登记，须改 .rml 夹具）。

## 门格

- 全量构建 0 error 0 warning（顺手清 `EditorAppSmoke.cpp:1802` 预存
  unused-param——M6a 批① 切段链 lambda，非本轮引入）。
- `ctest` 3/3（engine-tests 含 TestSpawnFreezeNoLeak/Pool 防护回归；
  script-tests 含 #71 mark 1553 断言）。
- `smoke-uirml` 脚本版（`--script ... --frames 500 --validate`）
  `ev=c1r4 => OK`（无冷启动抖动）；非脚本版 `=> OK`。
- `--bench-survivor --frames 900` **PASS**：fps=88 alive=10436 waves=3
  teamAlive=10002 anim 10002/10002 hazard fx(256,128)——三组去重打印块
  + EnterPlayProgrammatic bench 路径 + #84 基号参数化活证。
- `--smoke-template --frames 3000` `=> OK`：death(seen/revive/scriptOk) +
  flow 全链 12 段 = #29 催命/压血去重块活证；play-roundtrip byte-exact；
  aud mount=7；second-project ids identical。
- `--smoke-drag --frames 90 --no-reopen` `=> OK`（#98 resize/zoom 注入段
  活证）。

修复未提交（33 文件，+532/−285；待用户指令）。
