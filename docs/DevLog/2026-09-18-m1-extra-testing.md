# 2026-09-18 · M1 补测轮（覆盖缺口审查，闪烁修复之后）

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
