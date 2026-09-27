# yami-dungeon —— 默认素材包第一批（M5 批③）

来源：yami-rpg-editor `arpg-ts-chinese` 模板 `Assets/Dungeon Assets/`（06 §7 既定
"直接采用"底包）。**MIT 许可**（Copyright (c) 2025 Yami & Xuran & Contributors，
见仓库根 `THIRD_PARTY.md` 登记）——随模板再分发只需在发布物中保留版权声明。

## 清单（GUID 固定：随包携带，勿改——clip/场景引用锚点）

| 文件 | 尺寸 | 网格（.meta importer） | 帧数 |
|---|---|---|---|
| dungeon_hero_1.png | 144×32 | cell 16×32 × frames 9×1 | 9 |
| dungeon_hero_2.png | 144×32 | 同上 | 9 |
| dungeon_monster_2.png | 128×32 | cell 16×32 × frames 8×1 | 8 |
| dungeon_monster_7.png | 128×32 | 同上 | 8 |
| dungeon_boss_1.png | 256×48 | cell 32×48 × frames 8×1 | 8 |

帧数与 yami `Assets/角色/角色动画/*.anim` 的 hframes 核对一致（hero 9 / monster 8 /
boss 8）。

## clip 样例（.anim 格式见 06 §2.2 / M5.md §16.2 D2）

- `hero-walk.anim`（guid `5bd31a7c20000001`）：hero_1 全 9 帧 @8fps loop
- `monster-walk.anim`（`5bd31a7c20000002`）：monster_2 全 8 帧 @8fps loop
- `boss-idle.anim`（`5bd31a7c20000003`）：boss_1 全 8 帧 @4fps loop

## 用法（编辑器）

1. 把本目录整棵拷进项目 `Assets/`（.meta 随行 = GUID/切片配置不变）；
2. 场景实体加 `SpriteRenderer`（任意帧）+ `Animator2D`，clipId 槽下拉选
   hero-walk / monster-walk / boss-idle；
3. Play —— AnimatorSystem #13 按表驱动 spriteId（03 §5 帧映射）。

切片消费面 = clip（sprite 槽直接引用切片的 UI 归 M6 切片编辑器）。
