# 2026-09-22 · M5 性能批①：bench-survivor 28fps → 58fps（判据转 PASS）

**分段计时落地**（`EditorApp.cpp`）：`--bench-survivor` 帧内打点（pump/sim/glue/
ui/acquire/scene/uidraw/present）+ 帧末累计，segSum 与 frameAvg 对账一致（35.43
= 35.43）——13ms 缺口一次拆净：

| 段 | 修前 | 修后 | 归因 |
|---|---|---|---|
| scene（视口层） | **19.31ms** | **1.25ms** | 两处根因见下 |
| sim（世界步进） | 11.22ms | 11.14ms | 未动（留性能批②） |
| ui（ImGui 面板） | 4.13ms | 4.11ms | 未动（万级 Hierarchy，观察项） |
| 其余五段 | <1ms | <1ms | — |
| **全帧** | **35.43ms / fps=28 FAIL** | **17.16~17.48ms / fps 57~58 PASS**（两跑稳定） | 判据 ≥45fps |

**scene 段两处根因**（都在编辑器视口层，非引擎内核；1 万实体负载下现形）：
1. `ViewportRenderer::ExtractScene` 差集销毁 **O(N²)**——`seen` vector +
   `std::find` 每帧 ~5000 万次比较。改纪元戳：map 值 `EntityRenderable{rid,
   lastSeen}`，`++extractEpoch_` 比对 O(N)。
2. Scene 视口**实体名标签全量画**——1 万次 ComputeWorldTransform + snprintf +
   TextWidth + 字形四边形（万级标签本身即不可用 UI）。改：视口 AABB 裁剪（+128px
   屏幕边距）+ 预算封顶 256 + 谓词对齐提取（禁用/悬空精灵无标签——原先禁用精灵
   也画标签，顺手修掉的漏网 bug）。

**回归**：ctest 3/3（engine-tests 含进程内编辑器核心 prefab/spawn 桥）+
bench-survivor 全跑（alive=10003 无回归）+ `Scenes/ui.scene` 90 帧截图标签墨色
像素命中（标签路径仍渲染）。**未跑**：`--smoke` 像素四要素冒烟（依赖手测工程，
本机卡顿从简；下轮补）。

**留给性能批②**：sim 11.14ms 系统级分解（vs bench-sim 5.1ms 差在哪几个系统）、
frameMax≈31ms 尖刺归因、ui 4.11ms。观察：scene 修后 bench-survivor 已 PASS，
sim 优化属余量挖掘非达标必需。

---
