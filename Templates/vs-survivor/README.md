# vs-survivor 模板（M5 批④）

吸血鬼幸存者式开局模板：8 向移动 + 直射弹（可升级穿透）+ 环绕刃 +
经验宝石磁吸 + 升级三选一（数字键 1/2/3 或点击卡片）+ 16 波导演
（t=380s Boss 波）+ HUD 四要素 + 死亡结算/最高分存档（死亡对话框点击复活）。

**测试自杀键 R**（验收反馈 2026-10-04 落地）：Run 态按 R = 立即死亡——首死出
复活卡、复活后再按 = 二死进结算（真人无冒烟侧压血钩子，站桩磨不死故加）。
**波次密度**（同日调整）：前 15 波数量 ×1.3、波距 35s→25s（Boss 565s→380s）。

由 `lemon-editor --gen-vs-template <dir>` 生成（改玩法请改 Game/ 下
脚本或场景后重新生成，勿手改 .prefab 内 guid）。

## 素材来源与许可

- `dungeon_*` 精灵表与 `*.anim`：yami-rpg-editor（MIT，Copyright (c) 2025
  Yami & Xuran & Contributors）——随模板再分发需在发布物保留版权声明
  （仓库根 THIRD_PARTY.md 已登记）。
- `gem/bullet/pierce/blade.png`：程序化生成（无版权负担）。
- `Assets/Audio/*.ogg`：Kenney 各 CC0 包 + OpenGameArt CC0（M6c 批④；
  逐件来源表见 `Samples/Assets/cc0-audio/README.md`，THIRD_PARTY.md
  已登记——CC0 无署名义务，登记仅为溯源）。

## 玩法锚点

- 流程（M6b 批③d-2 档1）：**单场景** `Main.scene` 只放常驻件（UI 六文档
+ Flow 实体）；玩家/导演在 `Prefabs/Player.prefab`/`Director.prefab`，
GameFlow 开局/重开时清场重挂（RunSweeper 按 tag 扫场销毁——重开 = C#
自律清场，零引擎改动）。流程 = 主菜单 → 一局 → Esc 暂停/设置 → 死亡
（首死复活对话/二死结算）→ 重开/回主菜单；**死亡策略归游戏侧**（模板
示例 = 每局一次复活，见 PlayerCombat.Die 分叉——改复活道具/表驱动只动
那一处）。
- 玩家：`Player.prefab` 挂三脚本（M6a 批⓪ scripts[]：流程 → 移动 →
战斗 → HUD 注册序 = 跨类型 Update 执行序）；一局共享态在 GameMain.Run。
- 波次：`Director.prefab` WaveDirector（Inspector 数组段可调参）。
- 数值表（M6a 批② T4）：升级池/武器参数/XP 曲线在 `Assets/tables/`
（upgrades.tab / weapons.tab / balance.tab——PlayerCombat.Start 读，
加升级项/换弹种/调环绕参数 = 改表不改代码；Excel/Numbers 改 CSV
（导出 UTF-8）拖回 AssetBrowser 覆盖再导入）。列契约见各表列头与
PlayerCombat.cs 头注释；表 GUID 生成期固定（改玩法勿动 .meta）。
- 三选一池：upgrades.tab 行序（固定序轮换，零 RNG = 回放友好）。
- 素材引用：.scene 双写 spriteGuid（真源）+ spriteId（进程内号）——
改名/移位/manifest 重建后打开场景自动归一（M6a 批⓪ T2）。
- 游戏 UI（M6b 批③d-1/③d-2）：六文档全走 .rml（HUD + 升级三选一/
死亡对话 + 主菜单/暂停/设置/结算——`Assets/UI/`，theme.rcss 主题 token
单源；场景 UI_* 实体挂 UIDocument 声明装载）。**换肤 = 改 theme.rcss 的
token 区**（色板/字号/间距，全 dp——画布缩放时 UI 物理比例恒定，720dp
设计基准）；改布局/文案 = 改 .rml/.rcss 资产，引擎零改动。数字键选择
已退役（点击选择）；设置两开关（飘字/血条）+ 音量四滑条（主/音乐/
音效/界面，M6c 批④）持久化于 Settings 档。
- 音频（M6c 批④）：BGM 开局起播（单槽 = 重开不叠曲）/ 命中·击杀·拾取·
升级·波次事件音 + UI 组按钮音（暂停中可响）；Esc 暂停 = BGM 声部级
挂起续响（恢复不回跳）。**编辑器内注意**：Esc 是编辑器惯例 = 退出 Play，
游戏内暂停请按 **P**（bit6 同映射别名；独立运行时 Esc 生效）。音频资产
在 `Assets/Audio/`，换音 = 换文件保名（.meta guid 不动，脚本零改动）；
音量即时生效 + Settings 档持久。工具栏暂停钮 = 编辑器检视冻结（音频同步
挂起，M6c 批④ 起联动），与游戏内暂停是两回事。
