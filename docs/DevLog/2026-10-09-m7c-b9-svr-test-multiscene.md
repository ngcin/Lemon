# M7c 批⑨：消费者迁移——svr-test 拆多场景 + RunSweeper 退役 + 加载屏接入 + 模板随迁

- 日期：2026-10-09
- 关联：[批文件](../Plans/M7c/2026-10-09-b9-svr-test-multiscene.md) · [ADR-017](../ADR/ADR-017-Scene-Management-And-LoadScene.md) 批⑨ 行 · [b6b](./2026-10-08-m7c-b7-sdk-scene-facade.md)（换场编排）· [b7](./2026-10-08-m7c-b7-sdk-scene-facade.md)（SDK 门面 + 两敞口登记）· [b8](./2026-10-08-m7c-b8-loadscene-async.md)（LoadSceneAsync + 加载屏样例）
- 性质：批⑥–⑧ 换场引擎的**首次真实项目落地**。**D1–D5 用户未应答按推荐推进待追认**：D1=A code-mount 壳 + DDOL 种子 + 重装自毁守卫 / D2=A 战斗场自含实体 / D3=A 键盘位（UI 指针入流登记候选池）/ D4=A 模板两场景（Volcano 不随迁）/ D5=A async 进场+同步回菜单。

## 实测数字（机器面）

| 项 | 值 | 判据 |
|---|---|---|
| lemon-tests | **34,716 checks 全绿**（引擎内核零改动，逐位不变） | 基线只增不减 |
| script-tests | **1,818 checks**（逐位不变） | — |
| ctest | 4/4 | — |
| game-smoke | **OK**（uidoc=7 精确命中 code-mount 口径：四屏预装+HUD+卡片+加载屏；contractErr=0；hud/cards 链过换场） | 宿主端到端 |
| scene-smoke | **OK**（四同步跳 + async 第五跳；`uiMainIdle=1` = CSharp 幸存者仍挂+已隐双断言新口径） | 编排回归 |
| template-chain smoke | **OK**（flow 全链：menu→start（换场）→cards→死亡复活→二死结算→restart（换场）→pause/settings/resume→tomenu（换场）→菜单回归；uidoc=7；saves 双档；第二项目 ids 一致） | **批⑦ 敞口① 收口**：编辑器内换场端到端机器驱动 |
| smoke-guid | OK（entities=7 drift=7/7 零悬空） | — |
| 回归 full | **21/21**（首跑四红 = 机器级既有敞口，见下节修复；二跑三红 = stash 基线对照后忘重编的旧 lemon-game 二进制连坐；终跑全绿 bench fps=88） | — |
| 金回放 | **跳过实测**（依据：引擎内核零改动——vtable 59/组件 id/系统序逐位不动，单测 34,716 逐位不变 = 哈希流零漂移旁证；改动面 = 宿主 smoke 工具 + 生成器 + 两项目内容，均不入回放流） | — |
| 构建 | 零编译警告（ld duplicate libraries 观察项见下） | — |

## 架构落账（批文件 D1–D5 兑现）

- **svr-test 拆场**：`MainMenu.scene`（入口 = Flow 种子单实体；四屏 UIDocument 实体退役）+ `Grass.scene`/`Volcano.scene`（战斗场自含 Player + Director(+VolcanoTableLoader)，装载即开局）。**RunSweeper 全删**——清场归引擎换场（批⑦ D2 除 DDOL 系外全清）；重开 = 重装载，SweepArmed/SweepObserved 握手与 Spawning 态机整体退役。
- **跨场壳（D1）**：GameFlow 种子 Awake 自标 DDOL（批⑦ D1 根位式）+ MainMenu 重装新种子 Booted 守卫自毁（守卫随 StateBag 过热重载）；四屏文档 code-mount（`UI.Show` 通道 B 现载 origin=CSharp 跨场幸存——换场 sweep 只卸 origin=Scene，`UiSubsystem.cpp:832` 不感知 DDOL，场景声明式不跨场）。隐藏屏 Start 一次性 Show+Hide 预装载（通道 B 只在 Show 现载，此后隐藏态可写——③d-2 先例语义保持）。
- **换场门面（D5）**：进战斗 = `LoadingScreen.Begin`（LoadSceneAsync + 加载屏，批⑧ 样例消费；svr-test 直通形态、模板保留 holdGate 全样例）；回菜单 = 同步 `LoadScene("MainMenu")`；`sceneLoaded` 订阅归 GameMain.Configure（与 UI.Events 同生命周期，热重载随域重建重订）→ GameFlow.OnSceneLoaded（MainMenu→菜单；Grass/Volcano→Run 态+Run 共享态归零（模板）；未知场→隐菜单不认领）。
- **键盘位（D3，批⑦ 敞口② 修法）**：菜单 R=草地 / 空格=火山；结算 R=重开 / Esc=回菜单——语义位入 InputState = 确定性回放；鼠标点击为人用路径（非回放口径，README 注明）。UI 指针入流（候选 B）登记 M7c 候选池（playtest 回放采集需要时再上——InputState schema→回放格式→金档重录风险）。
- **火山（困难）**：`waves_volcano.tab`（guid `7e57100000100011`，占位数值用户改表即调）+ `VolcanoTableLoader`（`WaveTableLoader` const→`protected virtual TableGuid`）。
- **模板随迁（D4）**：生成器 `VsTemplateGen.cpp`——内嵌 GameFlow 源串同款重写 + GameMain（删 RunSweeper 注册/订 sceneLoaded/HudDoc 常量）+ PlayerHud（Start 补通道 B 装载）+ **LoadingScreen.cs/loading.rml 补进生成器**（批⑧ 手加产物目录的分歧补齐 + 修 Doc 裸文件名笔误→全 relPath）+ 场景拆二（MainMenu 壳/Grass 自含——Player/Director 构造提取为具名 lambda 与 prefab 导出双发）+ entryScene/README 串 + **Flow 实体 guid 钉死**（`kFlowEntityGuid`，GenerateGuid 为进程随机——不钉死则重生成漂移、GameEntry kSceneSmokeDdolGuid 夹具断链；两处字面量互为镜像注释）。
- **smoke 联动**（宿主/工具面）：smoke-template——entryScene 断言×2 改 MainMenu.scene、种子开 MainMenu+选 Flow、`uiLoads==6` → `uiLoadsFinal==7`（终态 code-mount 累计恰 7）、overlay 三要素对 Transform-only 实体零像素 → 走「无选中跳过」语义（M7a 批⑧ 先例；像素防线由 basic smoke/smoke-drag 链继续覆盖）；game-smoke——`mountedUi>0`（boot 通道 A）→ `DocumentLoadCount()>=4`、`BatchSystemCount>0` 删（RunSweeper 是模板唯一脚本批量系统，退役后恒 0——脚本链断言由行为面 hud/cards 承接）；scene-smoke——`!HasDocument(main.rml)`（origin=Scene 卸载前提）→ `HasDocument && !IsDocumentShown`（CSharp 幸存者仍挂 + 流程已隐——**更强**：D1 架构的端到端机器证）；smoke-guid——开 Grass.scene、prefab 实例 7→6（+内联 Player = 7 计数不变）。

## 机器级既有敞口修复：smoke 帧率下限（回归首跑四红的根因）

首跑回归 17/21：asset-chain / uirml×2 / script-chain 四步确定性红。溯源两日线索全链：

1. **根因**：FileWatcher = 500ms 快照轮询；各 smoke 的等待窗全部按**帧数**标定（如 uirml「≈50 帧 ≥ 轮询周期」按 60Hz）= 833ms。显示器**睡眠/合成器不节流**时编辑器主循环实测 500fps+（240 帧会话 <0.5s），帧窗墙钟塌缩 → watcher 竞速全面失效（热替换 64×64 停留原值、终局复种墓碑不复活）。**屏醒（caffeinate）即绿** = 环境归因实锤；HEAD 基线（stash 对照）同败 = 非本批引入（本批引擎面零改动的旁证）。
2. **修法**：编辑器 smoke 会话帧率下限 18ms（55.6fps——比 60Hz 标定略慢取宽裕；`EditorApp.cpp` 帧尾）。交互会话（frames==0）与 bench（Immediate 压测口径）不动。四步复跑全绿。
3. **流程乌龙登记（对照实验纪律）**：stash 基线对照后**忘了重新编译**导致一段「基线也败」的无效对照；且 uirml 手工复现误用 `--script Lemon.SDK.dll`（回归实为 TestScript.dll——SDK 零行为类型，C# 回执链天然缺席）。两条都吃掉了一轮排查时间——教训：基线对照必须 `ninja` 确认重编 + 复现命令先从回归脚本原文抄。
4. **smoke-ui 注入抖动登记**：下限生效后的全量回归中 smoke-ui 单红（hier 行点击偶失 gsel/rename）——独立复跑两连绿，smoke-drag/anim-chain 同族注入抖动；按 09 §9 既有口径升级 `retry_step` 两次取优（editor-regression.sh）。

## GameFlow 瘦身账（出口判据的价值量化）

| 面 | 批⑧ 前 | 批⑨ 后 |
|---|---|---|
| 清场系统（RunSweeper 类 + 12 tag 清单） | 36 行 × 2 项目 | **0（引擎换场接管）** |
| 清场握手（SweepArmed/Observed + Spawning 态机） | ~25 行 × 2 | **0（sceneLoaded 事件路由）** |
| 重开/开局实体管理（Instantiate 双 prefab + Run 归零时序） | Spawning 段内联 ~20 行 | OnSceneLoaded 段（场景自含，无 spawn） |
| 换场调用面 | — | 3 处 SDK 调用（Begin/LoadScene ×2 门面） |
| GameFlow.cs 总行数 | 362 / 367（svr-test/模板） | 367 / 372（正文逻辑净减 ~60 行；注释净增——含 D1–D5 裁决与契约注记） |
| GameMain.cs | RunSweeper 注册 | sceneLoaded 订阅（净 +1 行） |

语义账为主：**游戏侧「清场」职责归零**、重开零握手（原两帧握手窗口 + tag 集维护是单场景架构的全部税）；行数账持平因新架构注释密度（按仓库注释纪律如实记录）。

## Review 轮（收口后全量自查，当日修）

- **F1（当日修）隐藏屏契约红**：通道 B 只在 Show 现载，`RefreshSettingsUi` 首帧即写未装载的 settings DOM = 契约红 ×N（svr-test boot 冒烟即抓）→ Start 四屏一次性 Show+Hide 预装载。
- **F2（当日修）批⑧ 遗留双缺陷**：模板 LoadingScreen.cs/loading.rml 未进生成器（fresh 生成缺件）+ Doc 裸文件名 `"loading.rml"`（通道 B 按 relPath 解析必败——批⑧ 注册不挂载故未现形）→ 补生成器 + 全 relPath。
- **F3（当日修）HideCardsDoc 未装载 Hide = 契约红**（首局 EnterRun 即触发）→ `CardsShown` 守卫 no-op。
- **F4（当日修）场景名匹配面**：生成器 NewScene 默认名 "untitled"——C# `scene.name` 路由落空 → MainMenu/Grass 构建时 `SetName`（svr-test 手写场景本就有名字）。
- **F5（当日修）mountedUi 编译警告**：断言退役后变量 unused → 删记账（装载链断言移 RESULT 侧）。
- 确认面（机器证过，无缺陷）：MainMenu 重装副本种子守卫自毁（template-chain tomenu 往返 = 流程壳独占）；DDOL Flow 种子跨五跳幸存（scene-smoke `ddolLineage=1`）；HUD/卡片 code-mount 跨场幸存与显式收屏（ReturnToMenu）；音频换场强制清暂停与 BGM 开局起播含装载期。

## 观察项登记（不动）

- `ld: warning: ignoring duplicate libraries 'liblemon-engine.a'/'libSDL3.a'`——lemon-game 重链时出现，链接行重复档案为既有 CMake 形态（非本批引入；CI 报告从不捕获构建明细故历史不可考）。编译警告面恒零。
- Volcano 波次数值 = 占位（用户改 `waves_volcano.tab` 即调，README 已注）。
- 编辑器直开战斗场 Play = 裸战斗（无流程壳）——与直开 Main.scene/ani.scene 既有口径一致（README 注明完整流程从 MainMenu 进）。

## 真人走查清单（待用户，出口判据）

菜单 R/点击 → Grass 加载屏（1–2 帧）→ 战斗 → 死亡复活对话 → 二死结算 → R 重开（加载屏）→ Esc 回菜单 → 火山开局（困难变体）；编辑器内同流程（Open MainMenu.scene → Play）；暂停/设置/音量链回归。

## 下一步

批⑩（可选，编辑器打磨：Play 态 Hierarchy 场景组 + DDOL 徽标 + i18n 词条）或 M7c 收尾转 M8；D1–D5 追认 + 本批真人走查待用户。
