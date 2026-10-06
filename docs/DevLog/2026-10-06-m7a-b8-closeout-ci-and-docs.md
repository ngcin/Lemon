# M7a 批⑧ 收官：CI 每日回归 + 性能基线门禁 + grep_step 假绿实抓 + 文档五区落账

**日期**：2026-10-06 · **批**：[M7a 批⑧](../Plans/M7a/2026-10-06-b8-closeout.md) · **状态**：代码面 done（svr-test 真人总成 V1–V3 待用户）

## 拍板（D8-1–D8-4，开工日）

- **D8-1 每日回归形态**：本机 launchd（`Tools/ci-daily.sh` 每日 03:30）+ GitHub win job 每日 scheduled（cron UTC 19:00）。依据：性能门禁只在固定硬件数字可比；GitHub macOS runner 10× 计费（免费 2000 分钟/月 ≈ 实际 200）撑不起每日 full。
- **D8-2 bench-survivor 门禁**：fps ≥ 76.5（基线 85（批⑥ 2026-10-04 实测 85/87）× 0.9 = 09 §10/08"回退 >10% 标红"机器化；45fps 硬线在 `BenchVerdict` 退出码内不重复）。
- **D8-3 smoke-drag 抖动收口**：两次取优（脚本层 retry；与 09 §9 flake 治理口径同源）。
- **D8-4 W5**：真机 GPU 不阻塞收官，移交物理机窗口。

## 门禁基建（09 §9 门禁分流三条 + 登记项，全清）

| 项 | 改动 | 验证 |
|---|---|---|
| ① grep_step 退出码 | `Tools/editor-regression.sh`：输出匹配 **AND** rc=0 双判 | 首跑即实抓假绿（下节）；修后 full 20/20 |
| ② anim-smoke 采样 | `Samples/anim-smoke/main.cpp` 采样间隔 60→30 帧（120 帧 2 采样 → 4 采样，`Distinct>=3` 判据语义不变） | 120 帧默认跑 `cpp=[5,11,16,...] => ADVANCING` + exit OK |
| ③ bench-sim 门禁分流 | `--no-gate` 纯诊断档（录制模式退出码不做 avg≤8ms 判）；默认门禁与回放 hash 门禁不动 | `--no-gate` rc=0 / 默认 rc=0（600 帧） |
| smoke-drag 抖动 | `retry_step` 两次取优（一次红自动重跑，绿 = PASS 标 `retry 1`；两次红 = FAIL） | full 门格首跑实战生效：`OK smoke-drag ... (retry 1——首跑抖动)` |
| 性能门禁步 | 回归第 20 步：`--bench-survivor --frames 900` 判 `=> PASS` + fps≥76.5 | 两轮均 `fps=87 >= 76.5` |

## grep_step 假绿实抓（本批最重要发现）

退出码双判上线首跑（后台）18/20；前台定位排除环境项后，**smoke-template 前台复现退出码 1**——主断言 `smoke-template: hud(...) => OK` 全 YES，但公共裁决段 `editor-smoke FAIL`。

- **根因 1**：overlay-visible 像素断言（M4.7-P0）的 sel/handle/label 三要素依赖"有选中实体"；smoke-template 末态回主菜单（flow tomenu）无选中 = 流程合理末态，断言语义不适用。
- **根因 2**：sceneOk 的 `viewportVisible > 0` 同款——MainMenu 纯 RmlUi 场景无可见 sprite 包。
- **掩盖机制**：回归期望子串 `smoke-template: .* => OK` 被 hud 行/saves 行满足，旧 grep_step 只查输出不查退出码 → **自 M5 批④（2026-09-23）smoke-template 加入回归起，退出码一直非 0 而回归常年绿**——09 §9 ①"输出匹配但退出非 0 = FAIL"预言的口子真实存在了一年批次。
- **修**（`Editor/App/EditorApp.cpp` 两处，按语义适配非放宽阈值）：①无选中时 sel-trio 跳过（打印 `sel-trio=enforced/skipped(no selection)`），grid 防线不动；②smoke-template 模式豁免 `visible>0`，实体守恒防线不动。
- **双向验证**：基础 smoke `sel-trio=enforced` 且 sel=169/handle=42/label=83 照常断言 PASS（enforced 路径工作）；smoke-template `sel-trio=skipped(no selection)` 证实"无选中"推断（排除"overlay 渲染真死"另一种可能）+ `editor-smoke PASS` 退出码 0。

**附带实证**：回归须前台会话跑——后台/非 GUI 会话注入链（合成点击/拖拽）上下文缺失 = smoke-anim flow/pick 位确定性假红（首跑实抓，前台单跑即绿；历史"注入链抖动"归因收窄多一条）。launchd LaunchAgent 属用户 Aqua 会话不受影响（次日日报可证）。

## CI 每日形态落地

- `Tools/ci-daily.sh`：增量构建（**不 pull 不动工作树**；BUILD-FAIL = 环境态标注）→ full 回归 20 步 → 报告 `build/ci-reports/YYYY-MM-DD.log`（HEAD/日期/逐步/汇总；连续翻看 = flake 观察面）。守卫 ABORT（交互会话在场）rc=2 透传供报告区分。
- `Tools/com.lemon.ci-daily.plist`（入库模板，`__LEMON_ROOT__` 占位）→ 已安装 `~/Library/LaunchAgents/com.lemon.ci-daily.plist` 并 `launchctl load`（03:30；睡眠错过唤醒补跑；关机跳过）。
- `.github/workflows/ci.yml`：win job 加 `schedule: cron '0 19 * * *'`（push/PR 自动触发维持用户拍板暂停，恢复归 M7b；mac job 维持注释）。scheduled 首跑留每日观察（本地 yaml 语法校验过）。
- **首日报告**：`build/ci-reports/2026-10-06.log` 两段（首跑 18/20 暴露假绿 → 修复后复跑 `PASS=20 FAIL=0`，rc=0）。

## 文档五区落账

| 文档 | 落账 |
|---|---|
| 06 §4 | 双格式策略现状注记（二进制半边 LBA1/LAT1 已落；结构化 JSON 直拷 D3；全类型二进制+校验归 M7b） |
| 06 §6.1 | 出包流程对表（已实现段 vs M7b 段；mac/win 双验、W5 移交） |
| 07 §3.5.1 | **交付物矩阵**新小表（lemon-editor/lemon-game/lemon-packager/dotnet 分发/CI × mac/win） |
| 08 | §2 M7 行 + §3 M7a 段收官注记（判据机器面落账）；Gate C 注记两尾巴补齐（真机编译 W1/W2 ✅、每日门禁 ✅）；§3 bench 表下 CI 落地口径 |
| 05 §1 | Play 降级编辑器特权注记（lemon-game 兑现同源双入口） |
| 01 §5 | **目录树按实况重绘**（Engine/Assets/Entry/Ui/Platform/Scripting/dotnet/Tools/packager/demo 等；删 2026-10-02 修订注记——01 文内明确到期任务兑现） |
| 09 §9 | 形态落账 + 门禁分流三条 ✅ + flake 治理机器化 + "回归须前台会话"实证注记；§7 回归 19→20 步 |
| M7.md / AGENTS.md | M7a 代码面收官 + M7b 移交清单收口 + 批⑧ 状态行 |

## 门格

- 回归 **full 20/20 首跑全绿**（2:38；smoke-drag retry 1 实战生效）；ctest 4/4；bench-survivor 门禁 fps=87 两轮；ci-daily 修复后 rc=0；`--no-gate`/anim-smoke 单验绿；plist `plutil -lint` OK；ci.yml yaml parse OK。
- 金回放零影响（改动面 = 回归脚本/Samples 采样断言/编辑器冒烟裁决段，不触模拟与 vtable）。

## 真人验收总成（待用户，[批文件 §3](../Plans/M7a/2026-10-06-b8-closeout.md)）

V1 svr-test 主项目 `lemon-game --project demo/svr-test` 四屏全流程零 C++（08 判据原文对象）· V2 设置与存档持久化 · V3 暂停挂起续响 + 重开不叠曲（svr-test 侧）。（V4 mac 干净包再移交 = 批⑤ 已过，可选。）

## 遗留与移交

- W5 Win 真机 GPU（物理机窗口，D8-4）；scheduled workflow 首跑观察；连续 `retry 1` 频次观察（≥20 连续转硬阻断，09 §9）。
- M7b 移交清单六条（[批文件 §2](../Plans/M7a/2026-10-06-b8-closeout.md)）：压缩/加密/增量、安装器+zip、Steam depot、`.app` 签名公证、干净 Win 物理机安装回归、C# 程序集保护评估（+publish 确定性/ICD 探测随迁）。
