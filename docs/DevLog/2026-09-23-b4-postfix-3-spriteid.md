# 2026-09-23 批④后修③：换项目 spriteId 基号漂移——玩家/怪物全不渲染

用户报 demo/svr-test（vs-survivor 向导新建）Play 后玩家、怪物都不显示。诊断：
**不是渲染 bug，是 spriteId 记账整体错位**——该项目 manifest 里 hero 切片基号
148，而场景 Player 烘焙引用 116（差 31/32 = 上一个被打开项目的全部精灵数）。

- **根因**：`OpenProjectPipeline` 的基号 = `AtlasRegistry::SpriteCount()+1`，而
  注册表是**进程级、跨项目累计、从不复位**。同一编辑器会话里先开过任何项目
  （含**启动自动重开上次项目**——`--no-reopen` 才跳过），再新建/打开模板项目
  → 基号后移上个项目的精灵数 → 场景/prefab 里烘焙的数字引用全部悬空 →
  玩家/怪物/子弹全不渲染（HUD 是 ImGui 覆盖层照常显示，更具迷惑性）。
  `--smoke-template` 回归一直是独立进程首开（基号 104），从未命中该路径——
  又一条"冒烟环境太干净"的实证。
- **修复**（设备重建回调同配方）：`OpenProjectPipeline` 换项目时
  `Registry().Reset()` + `ProceduralAtlas::Build`（内置页 spriteId 1..N 复原，
  基号恒定）+ `AssetGpuCache::ClearPages`（新方法：旧项目导入页整体释放——
  槽位序 `firstSlot+pages_.size()` 必须与刚清零的注册表图集序在空表上重新对齐）
  + `ViewportRenderer::RebindProceduralIcons`（新方法：程序化页 ImGui 纹理
  注销重注册；设备重建路径一并复用）。首次打开 = 幂等重建同号；既有项目
  id 稳定性仍由 manifest 记账保证（同资产集 → 确定性扫描逐位一致）。
- **用户项目自愈**：删 `demo/svr-test/.lemon/manifest.json` 重开 → 确定性重记账
  （hero 切片基号 116 == 场景引用 116 逐位对齐；saves/bin 不受影响）。
- **回归防线**：smoke-template 末尾**同进程**再向导复制第二个模板项目打开，
  断言全部 sprite 资产的 (spriteId/sliceBase/sliceCount) 记账与第一个逐项全等
  （修复前必 DRIFTED）。此前 13 步回归没有任何一步覆盖"会话内二次开项目"。
- **回归**：editor-regression full **13/13**（含新防线 `second-project ids
  identical => OK`）；ctest 3/3。金回放/bench 不经编辑器项目开路径，零扰动。
