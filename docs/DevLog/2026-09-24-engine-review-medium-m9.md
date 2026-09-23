# 2026-09-24 Engine-Review Medium M9 补漏：设备丢失回调反注册

[Medium 排查轮](./2026-09-24-engine-review-medium-batch-a.md)的补漏件：M9 在最初
分批规划时被漏排（批A 只列 M4-M8，14 条实修 13 条）——核对清单时发现，本条单独
落账。

- **M9 设备丢失回调裸 this**：`SpriteBatcher::Init` 用 `AddRecreateCallback` 注册
  捕获裸 `this` 的重建 lambda，原 API **只增不删**且 batcher 无析构反注册——
  batcher 先于 Device 销毁后（编辑器 ViewportRenderer 成员 sceneBatcher_/gameBatcher_
  即此序），下次设备丢失重建回调写垂悬 this = UAF。修 = 回调注册 **token 化**
  （`AddRecreateCallback` 返回 `RecreateCallbackId`，新增 `RemoveRecreateCallback`）；
  `SpriteBatcher` 存 token、析构反注册。生命周期契约写进 API 注释：注册者须短于
  Device（编辑器 unique_ptr 成员逆序析构 viewport_ 先于 device_、样例栈序同构，
  天然满足）。

**验证**：bench-mow `--device-loss 300 --validate --frames 900`——recreate: SpriteBatcher
照跑、deviceLoss PASS、验证层零错误（token 化后重建链无回归）；engine-tests 13186
不变（SpriteBatcher 无移动语义使用，加析构零破坏）。

至此 14 条 Medium 全部收口：批A `ff16bd3`（M4-M8）→ 批D `45b4da8`（M16/M17）→
批B `14f95df`（M10-M13）→ 批C `91d5b31`（M14/M15）→ 本条（M9）。
