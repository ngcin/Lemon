# 2026-09-22 M5 清障③：bench-survivor 建场 + 编辑器内基线（缺口在案）

新增 `lemon-editor --bench-survivor`（08 §3 压测 B 的可执行形态）：

- **播种**：临时项目（tempdir，不污染用户工程——MakePrefabFrom 会写 Prefabs/）+
  程序化怪 prefab → **清障② SpawnFn 桥**消费：Spawner interval 0 / burst 64 /
  capAlive 10000 拉满 1 万（"导演拉满"）+ 玩家 Chase 目标 → EnterPlay 全 16 系统。
- **vsync 测量口径定死**：Immediate 呈现（FIFO 下 fps 被 60Hz 钉住，45fps 档测
  不出；frameAvg 恒 ~16.7ms = 回退信号）；帧时全帧 steady clock，预热 240 帧剔除
  （怪海 ~156 帧涨满）；判据 alive≥10000 且 frameAvg≤22.2ms。09 §6.10 落章。
- **基线（三跑稳定）**：alive=10003，stepAvg 9.7~11.4ms，frameAvg 33.6~35.3ms，
  **fps 28~30 => FAIL**（判据 45）。
- **缺口 13ms/帧登记**：stepAvg vs bench-sim（11 vs 5.1ms，疑 renderable 提取进
  step）；渲染+ImGui ≈24ms（bench-mow 同规模仅 9.3ms/帧）。M5 性能批主战场，
  逐项 profile 后优化，本命令回归对标。

**回归**：editor-regression full 11/11；engine-tests 13026；script-tests 1281
（清障③只动 EditorApp/EditorEntry，引擎零改）。
