# vs-survivor 模板（M5 批④）

吸血鬼幸存者式开局模板：8 向移动 + 直射弹（可升级穿透）+ 环绕刃 +
经验宝石磁吸 + 升级三选一（数字键 1/2/3 或点击卡片）+ 16 波导演
（t=565s Boss 波）+ HUD 四要素 + 死亡结算/最高分存档（R 复活）。

由 `lemon-editor --gen-vs-template <dir>` 生成（改玩法请改 Game/
PlayerBehaviour.cs 或场景后重新生成，勿手改 .prefab 内 guid）。

## 素材来源与许可

- `dungeon_*` 精灵表与 `*.clip`：yami-rpg-editor（MIT，Copyright (c) 2025
  Yami & Xuran & Contributors）——随模板再分发需在发布物保留版权声明
  （仓库根 THIRD_PARTY.md 已登记）。
- `gem/bullet/pierce/blade.png`：程序化生成（无版权负担）。

## 玩法锚点

- 玩家：`Player` 实体（PlayerBehaviour 单脚本；多脚本 scripts[] 属 M5 余项）。
- 波次：`Director` 实体 WaveDirector（Inspector 数组段可调参）。
- 三选一池：PlayerBehaviour.kOptions（固定序轮换，零 RNG = 回放友好）。
