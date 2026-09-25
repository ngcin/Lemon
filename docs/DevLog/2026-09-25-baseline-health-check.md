# 2026-09-25 · 基线体检——M6a 批② 开工前全量验证（全绿）

批②分解落账后、动工前的基线健康检查（构建/单测/回归/金回放/性能/Debug 六面）。
结论：**基线健康，批②可开工**。本条目兼作 T6 全量验收的对照基线。

## 结果

| 检查项 | 结果 | 对照 |
|---|---|---|
| Release 构建（mac） | 无需重编（与 00d55c9 一致） | — |
| ctest（mac） | **3/3** | — |
| engine-tests | **23267 checks OK** | = 批二修复批账面（2026-09-25） |
| script-tests | **1554 checks OK** | = 同上 |
| editor-regression full | **14/14**（无干扰环境） | = 同上 |
| 金回放 m5b2 三档 | sim-st / sim-mt `replay=PASS mismatches=0`、bench-script `PASS`；终态 alive 9682/created 11203/destroyed 1521 逐项一致 | 零重录持续（自 2026-09-23 录档，历批修复含批一/批二） |
| bench-survivor | **900 帧 PASS**：fps=83、waves=3、anim 10000/10000、**playerHp=275793**（与 2026-09-24 hazard 化批落账逐位一致 = 行为零漂移）、fx 饱和 | 判据 ≥45fps |
| Debug（mac-debug） | ctest **3/3** + engine-tests **23267 checks**（EnTT 断言全开） | = 账面 |

## 发现两项（操作性，非引擎缺陷）

1. **bench-survivor 判据窗口**：hazard 子判据要求停跑时玩家存活（playerHp∈[0,1e6)），
   但玩家 1e6 HP 按确定性掉血曲线在 ~1000+ 帧耗尽死亡 → **--frames 存在隐式区间
   （约 660~1000）**：600 帧 waves=2<3 假 FAIL（第 3 波 t=11s 才开）、1200 帧玩家已死
   假 FAIL。**口径帧数 = 900**（历史验证即此）。建议：回归脚本若纳入 bench-survivor
   时固定 `--frames 900`，或后续把判据改为"掉血实证或死亡皆可"（harness 小改，随
   批② T6 或单独微批）。
2. **smoke-drag 对并发负载敏感**：回归后台跑的同时前台跑单测二进制 → 拖拽注入窗口
   内帧更新数被压（25 < 正常 28）→ 累计量偏 → 假 FAIL；单独重跑即绿。**回归运行
   期间不要并行其他 CPU 重活**（回归含 fps 判据步骤，同理）。另：金回放正确帧数 =
   sim 3600 / script 1800（多传帧数会在档尾 truncated 中止，且管道尾 `tail` 会掩盖
   bench 退出码——判读必须看 RESULT 行，不看管道退出码）。
