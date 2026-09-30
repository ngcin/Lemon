# editor-regression.sh 残留实例前置守卫

- 日期：2026-09-30（批④-2 同日续；批③c-5 DevLog 登记的低优项清偿）
- 性质：工具加固——把"跑回归前 pgrep 查残留"的人肉纪律固化进脚本。

## 背景

批③c-5 回归三轮排查（15/16 → 14/16 漂移、单步重跑全绿）的真因：前夜泄漏的
headless lemon-editor（PID 39249，1h47m 渲染循环）与回归链争 GPU/窗口资源，
帧锚定注入链（drag/ui/anim）间歇失败。事后纪律是每轮回归前手工 pgrep——本批
固化为脚本前置守卫，泄漏一进场就现形。

## 改动（tools/editor-regression.sh）

- 位置：trap 之后、ctest 之前（任何步骤开跑前）。
- 分类处置：
  - **headless 残留**（cmdline 带 --smoke/--smoke-*、--bench*、--frames、
    --final、--play、--scene、--save-scene、--screenshot、--gen-vs-template
    任一旗标）：上届回归泄漏，TERM 清理后继续（1s 后仍活则 KILL）。
  - **疑似交互会话**（首词 basename = lemon-editor 且无上述旗标）：中止
    exit 2 交人裁决——自动杀交互会话 = 丢用户未存状态。
  - 进程甄别：pgrep -f + ps 逐个验首词可执行名，避免 tail -f xxx.log 之类
    带柠檬字样的误伤。

## 验证

- 伪泄漏实测：后台启动 `--smoke --frames 100000`（与事故同签名）→ 跑 quick，
  守卫打印"清理残留 headless 实例： 1947"并继续，quick 全过、事后 pgrep 空。
- 交互实测：无旗标启动 → `ABORT ... 疑似交互会话` exit 2，不误杀；手工清理。
- bash -n 过（macOS bash 3.2 兼容：全 ${BRACED}、case 模式单行）。
- 守卫上线后的 full 回归 16/16（与 include 瘦身批同轮，见同日姊妹篇）。
