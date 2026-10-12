# 调试与常见坑

## lemon-game 旗标

| 旗标 | 语义 |
|---|---|
| `--project <dir>` | 项目根（缺省 = exe 旁 data/，包形态零参启动） |
| `--scene <rel>` | 覆盖入口场景 |
| `--frames N` | 跑 N 帧退出（自动化；smoke 族要求 ≥240） |
| `--vsync` | FIFO 垂直同步（自动化/挂机必开，见下文坑） |
| `--smoke` / `--smoke-scene` / `--smoke-light` / `--smoke-fx` | 冒烟模式族（互斥） |
| `--validate` | Vulkan 验证层 |
| `--screenshot out.png` | 截图（调试口） |

## lemon-editor 旗标（常用子集）

`--project <dir> --scene f.scene --save-scene f.scene --play --script Game.dll
--frames N --validate --screenshot out.png --demo --no-reopen`；bench/smoke 族
（`--bench-survivor`、`--smoke-ui`、`--smoke-template`、`--gen-vs-template <dir>` 等）
主要服务回归，开发时也拿来快速起固定场景。

## 环境变量

- `LEMON_NO_ACTIVATE=1`：不抢前台焦点（挂机/连跑多进程时防打断）。
- macOS 验证层加载坑：brew 版验证层清单的 `library_path` 是裸文件名，lemon-game
  直跑 `--validate` 可能报 `VK_ERROR_LAYER_NOT_PRESENT`。绕行：

```sh
DYLD_FALLBACK_LIBRARY_PATH=/usr/local/lib \
  ./build/mac/Engine/Entry/lemon-game --project demo/svr-test --validate
```

（lemon-editor 同机不受影响；另注：设备创建先于脚本宿主装配。）

## 确定性回放（用户视角）

模拟是确定性的：同输入流必逐帧同结果。配套工具 = `Samples/bench-sim` 的
`--record/--replay`（录输入流 + 逐帧状态哈希比对）——「我这改了引擎/脚本，行为
变没变」用它一跑便知，模板与回归都在用。推论：**脚本别用系统随机/墙钟时间**
（用 `Scripting.Rng`/`Time` 家族），否则回放必炸。

## 常见坑清单

- **不限速自动化冻结（macOS）**：`--frames N` 默认不限速（Immediate 呈现），
  macOS 合成器对无限速灌帧节流 → 画面冻结且事件停轮询。自动化与挂机一律加
  `--vsync`（60Hz 真实节奏）。
- **挂机窗口全遮挡/显示器睡眠**：同族症状（WindowServer 停合成）。挂机保持窗口
  至少部分可见 + `caffeinate -is`。
- **结构命令两拍**：Add/Remove/Destroy 与动态挂灯当帧入队、帧首应用——同帧查询
  查不到是语义（见[脚本页](05-scripting.md)）。
- **补间在途禁写同字段**：Tween 存活期间拥有字段，同帧脚本写会被覆写。
- **无头站桩死亡**：无头跑战斗场景，玩家会被围殴致死进结算——自动化要抬 HP
  （soak 用项目副本，别改工作项目）。
- **存档位置**：编辑器侧 `.lemon/saves/`（slot_0/settings/meta 三档）；奇怪的行为
  先看是不是旧档残留在生效。
- **日志**：编辑器 Console 面板分级过滤；脚本异常红字定位行号。

> 口径与坑的权威登记：[09 测试分册](../EngineDesign/09-Testing.md)（回归/回放/
> 专项测试）与 `AGENTS.md` 本机坑一节（引擎仓库内）。
