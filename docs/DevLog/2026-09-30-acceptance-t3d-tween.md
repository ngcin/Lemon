# 2026-09-30 · T3d 真人验收回执 + Tween 手感项收口（验收载体：ani.scene 动效双件）

- 日期：2026-09-30
- 性质：手测轮回执——T3d 两件（ani.scene 观战 + Inspector AnimGraph 绑定槽）与 Tween A 档手感项通过。

## 验收载体（用户项目侧搭建，零引擎/编辑器改动）

ani.scene 原本没有可见的 tween 消费面，本轮补齐：

- **DuelBehaviour 受击闪色**：打点命中处 `Tween.Color` 闪浅红 0.06s + TweenFinished 回程补白 0.15s。"闪白"落地为闪红——精灵底色纯白，乘法混色下提亮不可见，同打击感语义（要真白闪需引擎加叠加混色，非项目侧能力）。
- **TweenDemo 脚本 + 宝石实体**：gem 周期性 Scale 1×→4× OutBack 弹出（模拟拾取弹跳）；GameMain 显式注册（教训见上午 ③c 条目）。
- ani.scene 无金回放/冒烟绑定，加实体零风险。

## 结果与两个非缺陷认知

1. **T3d 两件 ✅**：观战一轮无问题；Inspector AnimGraph 双 GUID 槽正常。
2. **Tween 手感 ✅**：B 侧闪红每 ~0.9s 一次、宝石 4s 一弹，过冲回落自然。
3. **"DuelA 只受击一次 / DuelB 受击多次" = 设计内**：Duelist.controller 有 `Attack→Hit` 打断转移，A（M01）故意缺 Hit 绑定 = 受击不打断攻击，B（M02）全绑 = 每次受击被 Hurt 掐断攻击段、打点事件发不出。T3d 的"缺绑宽容 vs 全绑打断"活样对比，维持不动。
4. **宝石"不动"首报 = 16×16 素材观感问题**：初始 0.2 缩放 = 3px 点、满弹 16px，肉眼不可辨（补间机制无恙）。改待机 4×/弹回 4× 后清晰可见。教训：低像素素材做动效验收，先按显示尺寸放大。

## 落账

[T3d 批文件](../Plans/M6a/2026-09-27-b2-t3d-anim-controller.md)（Status）、[Tween A 档批文件](../Plans/M6a/2026-09-28-b2-tween-a-runtime.md)（遗留段）、[Lemon/AGENTS.md](../../AGENTS.md)（T3d 尾注）。

## 关联

- 上午条目 [③c 验收](./2026-09-30-acceptance-b3c-input-events.md)（UiEcho 注册教训同源）
- [M5/③d 验收条目](./2026-09-30-acceptance-m5-uidoc-d1.md)（余项清单：T3d/Tween 两项随本轮勾销）
