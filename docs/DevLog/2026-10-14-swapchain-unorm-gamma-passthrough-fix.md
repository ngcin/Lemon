# 修复：交换链 SRGB 变体致全画面双重 gamma（灰白失原色）

- 日期:2026-10-14（用户实报：导入素材在资源浏览器/Scene 视口/游戏运行三处整体灰白）
- 性质:用户报障修复，非批次项。渲染内核（RHI）+ 两后端格式映射 + SpriteBatcher
  默认值 + fx-smoke 回归锚，共 6 文件。

## 1. 实报与现象

用户导入的深青色灌木素材（原图 RGB(64,112,104) 一族）在编辑器资产缩略图、
Scene 视口、游戏运行后全部变成灰白泛色（RGB(144,176,160) 一族），失去原色。
深色主题 UI 同被抬亮（kBgDeep 0.113 实显示 ≈0.30）。

## 2. 归因（像素级 + 代码 + 本机三层实证）

- **像素判别**：原图 vs 编辑器截图逐色对比——`v_out ≈ v_in^(1/2.2)` 多组吻合
  （(64,112,104)→(144,176,160)，预测 (136,175,170)，±8/255 在重采样容差内）；
  原图 alpha 全 255，排除 alpha 混合。= **出口多一次 linear→sRGB 编码**。
- **代码链**：02 §8（2026-10-11 拍板）= gamma 空间直通直乘（贴图 UNORM 原样 +
  composite gamma 直乘 + shader 无 gamma 项），但 `RHI.cpp` 交换链格式选择循环
  自 M1（d754e7d）优选 `VK_FORMAT_*_SRGB` 变体——`_SRGB` 附件使硬件对每次写入
  附加 linear→sRGB 编码，**编码无配对解码** ⇒ 全链双重 gamma。
- **本机实证**：`vulkaninfo` 确认 MoltenVK surface 提供 `B8G8R8A8_SRGB`（循环
  必中）。直写交换链的三个终点全部受染：编辑器 ImGuiBackend（管线格式 =
  SwapchainFormat()，UI/缩略图/视口图像全编码）、lemon-game 直写段 batcher、
  LightCompositor/PostFx 终点。
- **为何长期未暴露**：不是 M8 回归（偏好 M1 即在）。模板素材偏亮偏灰不敏感；
  夜战 0.30 环境光编码后 ≈0.58 仍被真人当"夜"验收通过；**冒烟像素断言域全在
  RT（刻意避开交换链 sRGB 干扰）——对出口编码结构性全盲**。

## 3. 修法（gamma 直通工作流对齐，最小改动）

- `RHI.h`：`Format` 枚举新增 `BGRA8Unorm`（交换链 UNORM 变体此前无表达）；
  `PipelineDesc::colorFormat` 默认值 `BGRA8UnormSrgb → BGRA8Unorm`。
- `RHI.cpp`：`ToVk`/`SwapchainFormat` 双向映射补 `BGRA8Unorm ↔ B8G8R8A8_UNORM`；
  交换链选择循环改**优选 UNORM 变体**（BGRA→RGBA），未命中才兜底 `_SRGB`
  （保运行，观感降级）；`DebugFetchCapture` 的 BGRA 解序本就覆盖 UNORM，零改动。
- `ImGuiBackend.cpp`/`RmlUiBackend.cpp`：本地最小 `ToVkFormat` 复制件同步补 case
  （漏补 = `VK_FORMAT_UNDEFINED` 进管线）。
- `SpriteBatcher.h`：`Init` 默认 `colorFormat` 与成员默认同步改 `BGRA8Unorm`
  （所有调用点本就显式传参，纯一致性）。
- 下游 LightCompositor/PostFx/编辑器视口全吃 `SwapchainFormat()`/`rtFormat`，
  自动跟随，零改动。

## 4. 回归锚（补盲区）

fx-smoke 增 **gamma 直通锚**：播种第 9 个实体（不透明授权色 (60,76,92)，64×64，
棋盘带下方），f60 基线交换链捕获断言出口像素逐通道 ±4（25 采样窗取 20）。
- 正向：UNORM 交换链 25/25 OK。
- **负向实证**：临时翻回 SRGB 偏好重建 → 0/25 FAIL（bloom/wave/offIdentical
  依旧全绿——差分断言族对绝对色值盲的实证）；还原后复绿。
- RESULT 行增 `gamma=` 字段进判定。

## 5. 验证

- Release 零警告（既有 `ld: duplicate libraries` 为链接行旧况，非本批）。
- 单测 34,930 OK；ctest 4/4。
- 验证层：lemon-game `--validate`（DYLD 回退绕行）零 VUID。
- smoke 族（vs-survivor 夹具照回归配方）：light-smoke / fx-smoke / game-smoke
  (900f) / scene-smoke 全 OK。注：fx-smoke 误传 svr-test 项目会 FAIL（主菜单
  UI 盖住断言域，夹具必须按回归脚本用模板拷贝）。
- **端到端像素验证**：Embrace 项目 Grass 场景 `--screenshot` 交换链 PNG——英雄
  暗轮廓源色 (34,34,34) 全屏 29,519 命中、其双重编码预测值 (102,102,102) 仅
  393；草地绿非蓝紫（BGRA→RGBA 解序正确）= 交换链确为 B8G8R8A8_UNORM 且直通。
- 全量 26 步回归未跑（改动面 = 交换链格式 + 断言域外的锚；核心步已逐一手工
  复跑同口径）。金回放不受影响（哈希域在模拟态，不在交换链）。

## 6. 遗留与登记

- **编辑器壳交换链像素断言盲区**：编辑器 smoke 族断言域仍全在 RT（ImGui 合成
  后的交换链无断言）。低频候选池登记（gamma 类缺陷已被游戏壳锚覆盖——两壳
  共用 CreateSwapchainObjects）。
- 仅 `_SRGB` 可用的 surface 走兜底 = 保运行观感降级（理论面：极旧 MoltenVK/
  部分远程桌面 surface；当代主流均双变体）。
- 用户观感复验待真人：重开编辑器确认素材/主题恢复原色（M7c 行动项① 血条
  素材观感复验同会话可顺带）。

**追记（2026-10-14 同日，真人面 + 逐位实验）**：用户对比图（上原图/下游戏内）
像素判别 Δ≤6/255（截图缩放+压缩噪声级；双重编码偏差 210+）——**真人验收过**。
另做**锚容差归零实验**：gamma 直通锚容差 ±4→0（逐位相等）复跑 fx-smoke 仍
25/25 命中——引擎链路（贴图→shader→交换链→回读）对颜色**比特级直通**，
用户可见残差全部来自链路之外（截图重采样/精灵缩放的线性采样/场景环境光）。
锚回归容差维持 ±4（跨机稳健），实验结论记此处。
