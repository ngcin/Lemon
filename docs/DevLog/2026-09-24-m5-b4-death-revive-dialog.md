# 2026-09-24 M5 批④后修④：模板死亡复活链断裂（P0）+ CopyUtf8 CJK 截断

## 现象（用户 M5 真人验收发现）

玩家死亡后 Console 连刷：

```
[lemon][error] behaviour 'PlayerBehaviour' Update: component Transform2D missing on entity
[lemon][error] behaviour 'PlayerBehaviour' disabled after 60 failing frames
```

"按 R 复活"无响应。用户初判 R 键与编辑器快捷键冲突——**部分成立但非根因**：编辑器确有
`R → Scale 工具` 绑定（`EditorApp.cpp` 工具快捷键段），但 GameView 聚焦时 bit5 采样正常，
**R 确实被采到、Revive 也执行了**——证据即报错风暴本身：`_dead=true` 时 Update 在读
Transform2D 之前就 return，风暴只可能发生在 Revive 把 `_dead` 置回 false 之后。

## 根因链

1. `Systems.cpp`（#10 命中）HP 归零 → 发 Death → **无条件 `scene.Destroy(hit)`**——
   玩家实体也被两阶段销毁；
2. 模板 `Die()` 弹"按 R 复活"，用户按 R → `Revive()` 跑完（`_dead=false`、恢复
   TimeScale、清 over 行）；
3. 下一帧起 Update 对**已销毁实体** `GetComponent<Transform2D>()` 每帧抛错；
4. 异常隔离机制 60 帧后自动禁用该行为（`ScriptBox.flags bit0`）——此后无人轮询任何
   键，复活彻底死路。

## 修复（四件）

1. **引擎：脚本实体死亡不自动销毁**（`Systems.cpp` 弹道/区域两处死亡点 + 弹道侧原有
   "当帧已死"守卫已足够防重复 Death）——`if (!scene.TryGet<scripting::ScriptBox>(hit))
   scene.Destroy(hit)`。语义：**带脚本的实体生死处置归脚本**（复活/重开走脚本逻辑）；
   无脚本数据实体照旧自动清场。零 schema 变化 → 金回放零重录（m5b2 三档实测
   mismatches=0）。
2. **交互改版（用户拍板：实际游戏不会按 R 复活，死后弹对话框点击复活）**：
   - SDK `Ui.ShowDialog(title, okLabel)`——卡片通道复用（`UiCards` B/C 传空），
     零 vtable 变化；GameView 卡片渲染**空标签槽不渲染按钮**（1~3 按钮自适应），
     数字键空槽无效（单按钮对话框按 2/3 不写越界 pick）；
   - 模板：`Die()` → `ShowDialog(title, "复活")`（幂等守卫 + 弃置在途升级卡）；
     `Update()` 死亡分支轮询 `CardPick()==0` → `Revive()`（补 `HideCards`）；
     R 键路径整体废弃（bit5/`InputButton.Confirm` 引擎面保留，SDK 通用确认语义）。
3. **模板三源同步**：生成器内嵌串（`EditorApp.cpp`）此前是**旧版**——M15 的
   `Subscribe` 助手修复只落了盘上 `Templates/vs-survivor/Game/PlayerBehaviour.cs`，
   未回写生成器（重生成会回退该修复，属潜伏漂移）。本次以新版为基线统一三处
   （内嵌串 / Templates / demo/svr-test），并重跑 `--gen-vs-template`（基号 104 与
   批④后修⑤一致；prefab/场景内部实体 guid 每代随机属生成器既定行为，资产 guid
   7e57 固定族不动，波表/武器引用不受扰）。
4. **副发现（本链测试抓获的潜伏 bug）**：SDK `CopyUtf8` 的截断回退**无条件**执行，
   吃掉 CJK 结尾完整串的最后一个字并停在孤立前导字节——"复活"→"复"。此前未暴露：
   模板 HUD 文案恰好都是 ASCII 结尾（"击杀 172"、"磁力 +25%"）。修复 = 仅真截断
   （`m < b.Length`）时回退，且孤立前导字节一并去掉。

## 回归防线

- `--smoke-template` 两段化：0–2100 帧切向风筝（攒击杀/升级链）→ 2100 帧起
  **压血 0.1 + 掐射击**站桩（实测自动炮火清怪快于刷怪、怪近不了身，纯站桩磨不死）→
  怪群近身 Hazard 真路径击杀 → 断言：死亡后玩家脚本实体**仍在场**（View 命中）、
  `ScriptBox.flags bit0` 未置（未被异常禁用）、对话框出现 → 注入 pick=0 → 复活成功
  （血回满 + TimeScale=1 + 对话框隐藏）。卡片 pick 注入改逐帧幂等（升级卡与死亡
  对话框同通道通用）；帧下限 1800→3000（回归脚本同步）。
- script-tests：`SaveCardsProbe` 增帧4/5（ShowDialog 单按钮 + pick=0 消费）；
  **1505 checks**（+20）。engine-tests **13218**。

## 验证（2026-09-24）

- ctest 3/3；金回放三档 `mismatches=0`（零重录——批②推论第三次机械验证）；
- `--smoke-template --frames 3000`：hud/saveLoad/wave/kills=175/levelUps/cards/
  **death(seen=YES revive=YES scriptOk=YES)** => OK；
- bench-survivor ×3：83 / 85 / 84 fps PASS（≥45 判据）；
- `tools/editor-regression.sh full`：**13/13**。

用户侧动作：`demo/svr-test` 已同步新 `PlayerBehaviour.cs` + README，重开编辑器即可
重测"10 分钟一局"验收（死亡 → 对话框点击复活）。
