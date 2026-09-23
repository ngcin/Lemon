# 2026-09-20 · 【P0 发现】overlay 渲染通道缺陷——网格/选框/Gizmo/标签从未画出

M4.7 规划讨论中用户反馈"灰色界面完全没有网格线"，与代码行为（GridSnap 默认开）
矛盾，追查确认为**绘制侧缺陷**而非观感问题。诊断细节与假设排序记
[M4.7 计划 §2.6](../Plans/M4/2026-09-21-m4.7-ui-polish.md)。

**证据链**（探针已还原，工作区零代码改动）：

1. `--smoke --screenshot` 截图：网格/选中选框/Gizmo 手柄/实体名标签全部缺失，
   精灵正常显示。
2. Render() 埋点：每帧 `overlay=68` 包、`batches=3`、`instances=90`
   （6 精灵 + 68 overlay + 16 文本）——**推入与 Bake 侧正常**。
3. 极限对照：网格临时改为不透明纯黄 20px 粗线，截图仍无任何线条——排除
   α70/线宽/配色，缺陷在绘制侧。
4. 交叉观察：仅批 1（instanceOffset=0，精灵）可见；批 2（offset=6，overlay）、
   批 3（offset=74，文本）全部不可见——**指向非零 instanceOffset 的实例寻址断裂**
   （`pc.baseInstance` 与 `DrawQuadInstances` 第二参语义不闭环为最高嫌疑）。

**教训**：此缺陷自某次 M4.2 后变更潜伏至今，`tools/editor-regression.sh` 9/9 PASS
照旧——自动化只断言精灵渲染与数据正确性，**渲染结果可见性（overlay/文本要素）无
任何自动防线**。M4.7-P0 修复后须把"`--screenshot` 四要素可见性"入冒烟断言。

---
