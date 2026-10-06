# CI 每日门禁改手动档（D8-1 修订拍板）

**日期**：2026-10-07 · **批**：[M7a 批⑧](../Plans/M7a/2026-10-06-b8-closeout.md) D8-1 修订 · **性质**：用户改拍板，配置回退

## 变更

批⑧ 初版（2026-10-06）每日门禁形态 = 本机 launchd 定时（03:30）+ GitHub win job 每日 scheduled（cron UTC 19:00）。**用户 2026-10-07 改拍板：全部手动，不要定时任务**：

| 件 | 处置 |
|---|---|
| 本机 LaunchAgent | `launchctl unload` + 删 `~/Library/LaunchAgents/com.lemon.ci-daily.plist`（已验证零残留） |
| workflow schedule | `.github/workflows/ci.yml` schedule 段删除 → dispatch-only（push/PR 注释维持） |
| `Tools/ci-daily.sh` | **保留** = 手动一键门禁（增量构建 → full 回归 20 步含 bench-survivor fps≥76.5 → 报告 `build/ci-reports/`；2026-10-06 门格 rc=0 已验） |
| `Tools/com.lemon.ci-daily.plist` | 模板留档（头注 = 恢复定时的安装配方），未装载 |

## 现行口径（09 §9/07/08/AGENTS/M7/M7a/批文件 已同步）

- 门禁 = 手动：要跑回归/性能门禁时 `Tools/ci-daily.sh`；win 逻辑面 = GitHub Actions 手动 dispatch。
- 恢复定时 = workflow 还原 schedule 段 + 重装 LaunchAgent（配方两处头注都在）。
- 门禁判据不受影响：回归 20 步、bench-survivor fps≥76.5（基线 85×0.9）、grep_step 退出码双判、smoke-drag 两次取优——全部保留。

## 附注

昨日（2026-10-06）批⑧ 落账文档中"每日 scheduled/launchd"表述已按本修订更新（各文档以"2026-10-07 用户改拍板手动档"注记保留修订轨迹）。`build/ci-reports/2026-10-06.log` 为首日（也是定时形态下唯一一日）报告，留档不动。
