# 2026-10-08 M7c 批④：飘字池提额 256→512（+ 定位批③ i18n 英文态回归）

用户拍板（同日 auto-slice 取消讨论后）：候选池「飘字池提额（若一局满屏跳字触顶）」
不待触顶直接做 512 收尾。改动 = `FxChannel.h` 常量一行 + `GameplayTests.cpp` 池
语义测试两处 256 硬编码参数化（引用 `kMaxTexts`，提额自动跟随）——全消费点
（定长池数组/环形取模/bench 灌满）核实自动跟随，零配套修改。

## 实测数字

- 构建 `--preset mac` 60 目标零错误；单测 **34,435**（逐位同批③ 基线）；ctest 4/4。
- bench-survivor：门禁 PASS 两跑 **fps=92 / 90**（≥76.5），`fx(texts=512 bars=128
  饱和)`（压测自动按新上限灌满）、alive=10436、frameMax=19.18ms、尖刺帧 0、
  uidraw avg 0.03ms——容量翻倍性能无感，与预判一致（成本在存活 sprite 绘制，
  不在池本身）。
- 回归 full：**两跑各 19/20、失败项漂移、复跑皆绿**（smoke-ui 头部动作抖动 /
  anim `clear=NO`，smoke-drag 同类注入抖动，09 §9 两次取优）→ 20 步全有绿记录。
  首跑 17/20 归因用户交互编辑器会话中途打开的 GPU 争用（present 单帧尖刺 1s，
  回归守卫只查启动时刻拦不到；同会话空闲后单独 bench=91 PASS）。

## 附带定位：批③ i18n 英文态回归（登记 ⑤+ 候选池，非本批引入）

`--smoke-anim` 在 en 稳定失败 `row=NO`（3/3）、zh 稳定绿（3/3）；HEAD~1 构建同败。
根因 = 选帧对话框底行（meta_hint 提示 + 3×150px 三钮右对齐让位公式，
`AnimationPanel.cpp:929`）按中文宽度定型，en meta_hint 远宽 →「替换为」按钮越出
模态窗（正是 2026-09-28 越窗锁防的形态）。批③ 门禁只跑双语言 `--smoke`（不含
smoke-anim）故漏网；且 smoke 未固定语言 → 回归结果依赖
`~/.lemon/editor-settings.json`（本机当前 en），门禁不可复现。修法两件（底行任意
语言宽度自适应 + smoke 固定语言）登记候选池，见
[批④ 批文件](../Plans/M7c/2026-10-08-b4-fx-text-pool-raise.md) §4。
