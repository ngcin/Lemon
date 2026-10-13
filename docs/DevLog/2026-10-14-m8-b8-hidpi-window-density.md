# M8 批⑧：HiDPI 窗口密度（4K/Retina 编辑器字体发糊修复）

- 日期:2026-10-14（用户实报：编辑器字体在 4K 屏很糊；批文件
  [Plans/M8/2026-10-14-b8](../Plans/M8/2026-10-14-b8-hidpi-window-density.md)）
- 性质:用户报障插队批。改动 = Window.cpp（标志 + env 旁路）+
  editor-regression.sh（口径钉扎 export）两文件。

## 1. 根因

`Engine/Platform/Window.cpp` 建窗缺 `SDL_WINDOW_HIGH_PIXEL_DENSITY`——4K@2x
交换链停在逻辑分辨率（1280×720 点），整帧被 macOS 拉伸 2×：全 UI 糊、小字号
最明显；尺寸观感正常（1× 内容 ×2），故报"糊"不报"大"。编辑器日志
`swapchain: 1600x900` 为实锤。

有意思的是**编辑器整套 HiDPI 管线早已存在且在空转**：UI 后端每帧查
`SDL_GetWindowPixelDensity`（无标志恒 1.0）→ 字体 pointSize×density 栅格化 +
`ScaleAllSizes` + 跨屏拖动重烘焙；视口拾取显式点→像素换算（注释点名 Retina）；
RHI 本就按 `InPixels` 取交换链。只差窗口一个标志。

## 2. 修法

- `Window.cpp`：flags 增 `SDL_WINDOW_HIGH_PIXEL_DENSITY`；`LEMON_NO_HIGHDPI=1`
  旁路（`LEMON_NO_ACTIVATE` 同款约定同款实现——env 检查，不设 = 默认清晰）。
- `tools/editor-regression.sh`：`export LEMON_NO_HIGHDPI=1`——回归机器即 4K
  本机，编辑器 smoke 族固定 px 断言/截图判据/金回放读数必须维持 1× 逐位稳定。
- 自动受益（零改动）：编辑器 ImGui 字体、场景/精灵 2× 渲染、视口 RT（×dpi）、
  游戏壳 RT 族（跟交换链）、RmlUi 布局（dp 比率随 RT 高自动放大）。

## 3. 验证

- **双向尺寸实证**：无 env = swapchain 2560×1440（1280×720 点 ×2，HiDPI 生效）；
  钉扎 = 1280×720（与修复前逐位同口径）。
- 钉扎口径：fx-smoke（含 gamma 直通锚）/light/scene/game(900f)/editor
  asset-chain smoke（固定像素计数断言 grid=30447）全 OK；单测 34,930/ctest 4/4。
- **ctest 一轮红教训**：imgui-isolation 连注释字样都扫（`ImGui[A-Z]`）——
  Window.cpp 注释初版写 "ImGuiBackend" 触红，改措辞"编辑器 UI 后端"过。
  隔离纪律守到文档层，符合 M4 §3.2 设计意图。
- 4K 真人观感待用户（重开编辑器看字体 + 视口拖拽走一眼）。

## 4. 遗留登记

- RmlUi 烘焙字体 = 1× 栅格化位图被 dp 比率放大（布局正确、字形仍糊）——
  FontBake 密度感知列 v1.1 触发式（游戏内 UI 文字清晰度诉求落地时做）。
- ~~ImGui 样式跨屏拖动密度 2→1 不回缩（乘性叠加 quirk，同屏无感）——既有项。~~
  追修后消解（ApplyScale 不再乘性缩放，任意密度变化都重铺设计值）。
- 高密度路径无回归覆盖（钉 1× 换稳定）——真人 4K 验收兜底。

## 5. 追修（2026-10-14 同日晚）：界面双重放大

**实报**：首版激活后字体清晰了，但整个界面放大一倍、操作空间减半（用户截图
实证：工具栏图标/面板全部巨型化）。

**归因**：`ImGuiBackend::ApplyScale` 的 `ScaleAllSizes(density)` +
`RebuildFonts(pointSize×density)` 是 **1.91 时代的手工密度方案**（#25 注记的
产物），其前提是 DisplaySize 喂像素；而 SDL3 后端喂的是**点坐标** DisplaySize +
`DisplayFramebufferScale=2`。1.92.9b 核心在 `RendererHasTextures`（Vulkan 后端
在列）路径下**每帧自动按 FramebufferScale 置 FontRasterizerDensity**
（imgui.cpp:4813）——16pt 物理大小不变、按密度点对点栅格化。两套机制叠加 =
style ×2 + 字体基数 ×2 = 界面双重放大。

**修法**：`ApplyScale` 只保留 `scale` 记账（`DisplayScale()` 消费方 = 视口
RT 尺寸 ×dpi 等维持原语义），`SetupStyle(1.0f)` + `RebuildFonts(1.0f)` 铺设计
值，密度锐化全交核心自动。密度 1（钉扎回归）路径逐位不变（ScaleAllSizes(1)
本就是恒等）。

**验证**：构建零警告；编辑器重开 swapchain 3200×1800（用户已拖窗 3840×1852）；
钉扎口径 editor asset-chain smoke PASS；真人观感复验中（字体清晰 + 布局正常
两要件）。
