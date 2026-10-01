# M6c 批④ 验收反馈热修 —— 编辑器暂停联动音频挂起 + Esc/P 键位交底 + pausedAll 跨会话残留

- 日期：2026-10-01
- 前情：[批④ done](../Plans/M6c/2026-10-01-b4-template-audible.md)（commit bcb20c0）；真人验收
  清单走查反馈："暂停挂起续响没法测试，点击编辑器上的暂停还是有声，ESC 一按就退出
  游戏模式了"。

## 定位（三件事，两迷惑一真 bug）

1. **工具栏暂停 ≠ 游戏暂停**：`paused_`（M4.7 三段式工具栏中段）只把 `TickPlay`
   冻结（dt=0）——音频设备回调独立线程照混，BGM 继续（"点暂停还有声"即此）。编辑器
   检视冻结与游戏内暂停（`Audio.Paused`，ADR-015 M4 挂起语义）是两套机制，此前零交底。
2. **Esc 是编辑器惯例**：`EditorApp.cpp` 主循环 Esc 边沿在 Play 态直接 StopPlay
   （M4 惯例，注释在案）；游戏侧 bit6 虽同收 Esc（`ImGuiKey_Escape || ImGuiKey_P`
   ——P 别名保底，批③d-2 注释"Esc 与 ImGui 弹窗争键面"），但同帧被 Stop 杀掉 →
   编辑器内游戏暂停实际只能按 **P**。模板 main.rml 提示只写"暂停 Esc"误导。
3. **pausedAll 跨会话残留（真 bug，随本反馈挖出）**：引擎 `AudioEngine::pausedAll`
   无任何复位路径（仅 SetPaused 写）；游戏暂停中（staged=true 已提交）StopPlay 再
   Play → 新 World 的 AudioChannel 无新命令 → 引擎仍 true → **新 BGM 起播即挂起
   变哑**（新声部起播时 `v.paused = pausedAll && …`）。自动化从未覆盖"游戏暂停态
   StopPlay"路径故未暴露。

## 修

1. `EditorAppScripts.cpp MountPlayAudio`：`audio_.SetPaused(false)` 会话起点归位
   （新 World 意图恒 false——游戏要起始暂停会显式再 SetPaused，不损语义）。
2. `EditorAppChrome.cpp` 工具栏暂停钮：置位 → `audio_.SetPaused(true)`（挂起同
   游戏语义：循环/BGM 挂起、Ui 免疫）；恢复 → `SetPaused(pausedStaged())` 按
   **游戏最后 staged 意图**回设（游戏自身暂停屏在场则保持挂起，不越权解挂）。
   tooltip 补"音频同步挂起"；单步钮 tooltip 补"音频保持挂起"。
3. 模板提示 + README（生成器 + 重生成入库）：main.rml "暂停 Esc/P"；README 音频
   段补编辑器键位交底（Esc=编辑器退出 Play 惯例 / P=游戏暂停 / 工具栏暂停=检视
   冻结两回事）。

## 实测

- ctest 3/3；smoke-template `aud(mount=7 pause=2 resume=3=OK)`；**回归 full
  17/17 首跑全绿**（批④ 提交时 16/17+复跑，本轮 smoke-ui 亦首跑过——负载相关）。
- 真人复测路径（交用户）：进 Play → 开局跑动 → **按 P**（非 Esc）→ 暂停屏 +
  BGM 停拍挂起 → 暂停屏点按钮（Ui 组音仍响）→ 再按 P → BGM 续响不回跳；工具栏
  ⏸ 现在同语义挂起、▶️/继续恢复；"游戏暂停中 Esc 退出再进 Play，BGM 正常出声"
  = 残留修复的验点。
