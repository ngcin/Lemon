# M8 批⑧ —— HiDPI 窗口密度（4K/Retina 编辑器发糊修复）

Status: done（2026-10-14 机器面收口同日；**含追修一轮**——首版激活后界面整体
双重放大（用户实报"操作空间非常小"）：`ApplyScale` 的 ScaleAllSizes +
pointSize×density 是 1.91 时代手工密度方案，与 1.92 核心的自动栅格密度
（NewFrame 按 FramebufferScale 置 FontRasterizerDensity，imgui.cpp
RendererHasTextures 路径）叠加。追修 = ApplyScale 只保留 scale 记账，style/字体
铺设计值，密度锐化全交核心自动。真人 4K 观感复验进行中（字体清晰 + 布局恢复正常
大小两要件）。用户报障实报：编辑器字体在 4K 屏发糊）

## 1. 输入条件

- **实报（2026-10-14 用户）**：编辑器字体在 4K 屏很糊；同会话前序 = 交换链
  UNORM 颜色修复（[DevLog](../../DevLog/2026-10-14-swapchain-unorm-gamma-passthrough-fix.md)）。
- **根因**：`Engine/Platform/Window.cpp` 建窗只带 `VULKAN|RESIZABLE`，缺
  `SDL_WINDOW_HIGH_PIXEL_DENSITY`——4K@2x 上交换链停在逻辑分辨率（实测
  1280×720 点），整帧被 macOS 拉伸 2× = 全 UI 糊、小字号最明显；尺寸观感正常
  （1× 内容 ×2 拉伸），故报"糊"不报"大"。
- **管线现状核证（全部已备、只差标志在空转）**：
  - RHI 交换链本就按 `SDL_GetWindowSizeInPixels` 取尺寸；
  - 编辑器 UI 后端每帧查 `SDL_GetWindowPixelDensity` → 字体按 pointSize×density
    栅格化 + `ScaleAllSizes`（`#25` 修正后仅一道缩放）+ 跨屏拖动自动重烘焙；
  - 视口拾取显式点→像素换算（ViewportPanels.cpp「DPI≠1（Retina）」注），
    视口 RT 尺寸 = 可用区 × dpi；
  - RmlUi 上下文 `SetDimensions(RT 像素)` + dp 比率随画布高走（布局自动放大）。

## 2. 文件/行级变更单

- **A 窗口层**：`Window.cpp` 建窗 flags 增 `SDL_WINDOW_HIGH_PIXEL_DENSITY`；
  env `LEMON_NO_HIGHDPI=1` 旁路（`LEMON_NO_ACTIVATE` 同款约定同款实现）。
- **B 回归口径钉扎**：`tools/editor-regression.sh` 头部 `export LEMON_NO_HIGHDPI=1`
  ——编辑器 smoke 族固定 px 断言/截图判据/金回放读数维持 1× 逐位稳定（回归
  机器即 4K 本机，不钉则全步挪位红）。手动跑 smoke 忘带 env 在 4K 机会红，
  属预期（口径=脚本统一）。
- 零改动的自动受益面：编辑器 ImGui 字体（机制全备即激活）、场景/精灵 2×
  渲染、编辑器视口 RT（跟随 dpi）、游戏壳 sceneRT/lightRT/gameRT（跟随交换链）、
  RmlUi 布局（dp 比率随 RT 高自动 ×2）。

## 3. 验证（2026-10-14）

- Release 零警告（ctest 红→绿一轮：imgui-isolation 扫注释字样，Window.cpp
  注释措辞规避 `ImGui[A-Z]`——隔离纪律连文档层都守，符合 M4 §3.2 设计意图）。
- **双向尺寸实证**：无 env = swapchain 2560×1440（1280×720 点 ×2，HiDPI 生效）
  ；`LEMON_NO_HIGHDPI=1` = 1280×720（钉扎生效，与修复前逐位同口径）。
- 钉扎口径 smoke 族：fx-smoke（含 gamma 直通锚）/light-smoke/scene-smoke/
  game-smoke(900f) + 编辑器 asset-chain smoke（含固定像素计数断言
  grid=30447）全 OK。
- 单测 34,930 OK / ctest 4/4。
- 4K 真人观感：**待用户**（重开编辑器看字体清晰度 + 视口交互/拖拽走一眼）。

## 4. 遗留与登记（不阻塞本批）

- **RmlUi 烘焙字体位图 2× 放大仍糊**（布局/尺寸正确，字形是 1× 栅格化位图
  被 dp 比率放大）——FontBake 密度感知（fontSizePx×density 重烘或图集多档）
  列 v1.1 触发式：**消费者出现即做**（游戏内 UI 文字清晰度诉求落地时）。
- ImGui 样式跨屏拖动密度 2→1 时 `ScaleAllSizes` 不回缩（乘性叠加 quirk，
  同屏使用无感）——既有项，本批不动。
- 高密度路径无回归覆盖（回归钉 1×）——编辑器 UI 字体密度正确性靠真人
  4K 验收兜底；若未来高密度路径出回归，再议密度感知断言步。
