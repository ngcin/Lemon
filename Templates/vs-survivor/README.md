# vs-survivor 模板（M5 批④）

吸血鬼幸存者式开局模板：8 向移动 + 直射弹（可升级穿透）+ 环绕刃 +
经验宝石磁吸 + 升级三选一（数字键 1/2/3 或点击卡片）+ 16 波导演
（t=565s Boss 波）+ HUD 四要素 + 死亡结算/最高分存档（死亡对话框点击复活）。

由 `lemon-editor --gen-vs-template <dir>` 生成（改玩法请改 Game/ 下
脚本或场景后重新生成，勿手改 .prefab 内 guid）。

## 素材来源与许可

- `dungeon_*` 精灵表与 `*.anim`：yami-rpg-editor（MIT，Copyright (c) 2025
  Yami & Xuran & Contributors）——随模板再分发需在发布物保留版权声明
  （仓库根 THIRD_PARTY.md 已登记）。
- `gem/bullet/pierce/blade.png`：程序化生成（无版权负担）。

## 玩法锚点

- 玩家：`Player` 实体挂三脚本（M6a 批⓪ scripts[]：移动 → 战斗 → HUD，
注册序 = 跨类型 Update 执行序）；一局共享态在 GameMain.Run。
- 波次：`Director` 实体 WaveDirector（Inspector 数组段可调参）。
- 数值表（M6a 批② T4）：升级池/武器参数/XP 曲线在 `Assets/tables/`
（upgrades.tab / weapons.tab / balance.tab——PlayerCombat.Start 读，
加升级项/换弹种/调环绕参数 = 改表不改代码；Excel/Numbers 改 CSV
（导出 UTF-8）拖回 AssetBrowser 覆盖再导入）。列契约见各表列头与
PlayerCombat.cs 头注释；表 GUID 生成期固定（改玩法勿动 .meta）。
- 三选一池：upgrades.tab 行序（固定序轮换，零 RNG = 回放友好）。
- 素材引用：.scene 双写 spriteGuid（真源）+ spriteId（进程内号）——
改名/移位/manifest 重建后打开场景自动归一（M6a 批⓪ T2）。
