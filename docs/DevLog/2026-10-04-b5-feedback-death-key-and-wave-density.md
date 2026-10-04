# M7a 批⑤ 验收反馈热修：模板测试自杀键 R + 波次密度上调（2026-10-04）

[批⑤ DevLog](./2026-10-04-m7a-b5-packager-mac.md)（真人验收进行中，首轮反馈）

## 事件

真人验收包体（`build/mac/dist/vs-survivor-mac`）反馈两条：

1. **"怪物少了点"**——波次密度观感不足；
2. **"死亡复活测试不到"**——死亡→复活→二死结算链不可达。

## 根因

- 数值面：玩家 HP 100 / Mob Hazard 8 DPS、速度 60（玩家移速远超，随便风筝）；
  站桩单怪磨死需 ~12s，正常游玩死亡几乎不可达。冒烟链当年就撞过同一问题
  （EditorAppSmokeTpl "站桩下自动炮火清怪快于刷怪（磨不死）"——用引擎侧压血
  钩子 ArmDeathPressure 造死亡）；真人无该调试通道 = 模板可测性缺口。
- 密度面：16 波波距 35s、首波 61 只，前期压力平缓。

## 修复（模板侧，两件）

- **测试自杀键 R**（`PlayerCombat.Update`，bit5 = GameEntry/编辑器 Play 同接线，
  边沿触发 + `St == Run` 守卫 + 死亡对话框期 Update 早退天然屏蔽）：Run 态按
  R = `Die()` 直调（幂等守卫既有）——首死出复活卡、复活后再按 = 二死进结算。
  README 同步登记。
- **波次密度**（`Director.prefab` WaveDirector）：前 15 波 e0/e1 数量 ×1.3、
  波距 35s→25s、Boss 波 565s→380s。

## 门格

- 模板 Game.csproj 编译零错；重出包（直接以 `Templates/vs-survivor` 为打包源——
  packager 对项目只读，模板无 `.lemon` 走回退扫描）自检 + 零参 `--smoke` 全绿
  （fps=1287、uidoc=6、hud=1 cards=1）；
- 回归 **full 19/19**（新平衡下 template-chain 死亡/复活/结算驱动链 + game-smoke
  + pkg-smoke 全过）；
- R 键本身为真人验收路径（smoke 无按键注入面），代码 = 既有 Die() 的边沿薄壳。

## 登记

- 波次密度为一次性拍板值（×1.3 / 25s），观感再调归后续用户反馈；
- `build/mac/game-fixture`（批④ 验收副本）未随迁——历史产物，回归夹具每轮
  从模板新拷贝，无消费面。
