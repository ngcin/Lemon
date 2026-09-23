# 2026-09-24 Engine-Review Medium 批C：C# 域生命周期与事件（M14/M15）

[Medium 排查轮](./2026-09-24-engine-review-medium-batch-a.md)收尾批。两条都在 C# SDK/Entry
侧，引擎 native 零改动。

- **M14 卸载/换装的清根对称**：`UnloadScript` 原只置空 `s_tickFn/s_asm/s_alc` 三引用，
  `ReloadScript` 步骤 1 只 `Behaviours.Reset()`——`Scripting.s_systems`/`Events.
  s_handlers`/`SceneOps` 静态表在 Unload+GC 窗口期仍根住旧 ALC 类型 → `weak.IsAlive`
  恒真、泄漏记账永不归零（LoadScript 末尾的 Reset 救不了已发生的 GC 轮询，时机错位
  是根因）。修 = 两处对齐 LoadScript 清理口径：**先清根（Scripting/Events/Behaviours/
  SceneOps/Time 五连 Reset）→ 再 Unload+GC**。当前 .NET 10 域线程模型下 ALC 本就必
  pin（ADR-010 修订），本修复不改变当前可观察行为——它是"runtime 修复 pin 后卸载
  仍失败"的根因消除，B 线探针届时自然归零。
- **M15 事件退订**：`Events.Unsubscribe` 补 API（原 Subscribe 只增不删，`Reset` 仅换域
  跑）；系统性收口 = `LemonBehaviour.Subscribe` 助手（实例订阅记入私有表，惰性分配
  不入 GC 热路径纪律账）+ `Behaviours.Detach` 收尾自动退订。**两处按实例订阅迁移**：
  TestScript `WaveBannerBehaviour`（构造器订阅波次横幅——评审点名的无界增长样例）与
  vs-survivor 模板 `PlayerBehaviour`（死亡→复活重挂逐局累积订阅）。`GameMain.Configure`
  静态订阅（域级）不迁——换域 Reset 覆盖，非泄漏面。

**验证**：script-tests **1494 checks OK**（+`TestSubscribeAutoUnsubscribe`：挂载→销毁→
再挂载后推一次 Custom 900 恰一份 901 回执——裸订阅旧行为下旧实例 handler 残留 = 双份；
探针 SubProbeBehaviour typeId 8 表尾注册，behaviours 计数断言 8→9 对齐。**时序坑**：
派发期由 handler 回推的事件走托管 pending，**下帧**派发头部才拉取——断言前需两帧
Step，与 Update 期回推的当帧送达（leak 测试 700 样例）不同）；金回放 m5b2 三档 PASS
mismatches=0（SDK 重建后零漂移）；`editor-regression.sh full` **13/13**（含模板链
`--smoke-template`——PlayerBehaviour 迁移后模板完整一局链路照跑 + 热重载终验）。

## 排查轮收尾

14 条 Medium 处置汇总：批A（M4/M5/M6/M7/M8，`ff16bd3`）→ 批D（M16/M17，`45b4da8`）→
批B（M10/M11/M12/M13，`14f95df`）→ 批C（M14/M15，本批）。M8 粒子半边此前已随 H2 修复
覆盖。评审 Low 段（约 20 项）未动，留观察项台账。
