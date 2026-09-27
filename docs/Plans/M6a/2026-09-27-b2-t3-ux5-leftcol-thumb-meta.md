# T3-UX5 左列段行缩略图 + 元信息（三图优化点①）

2026-09-27 · 批② 动画生产线 · 三图走查报告（DevLog 同日）五优化点之 1/5，
用户拍板从本条起步（成本最低：ThumbCache/页缩略图直染全部现成）。

## 背景

工作台 v3 左列段清单是纯文字行（Selectable 单行名字）——一集十几段时只能逐个
点开才知道内容。参考截图（Godot Animations 列）行带缩略图与帧数，扫一眼即知
每段是什么、多长。

## 决策

1. **行内容 = 首帧小图 + 名 + `N 帧 · X.XXs` 元信息**，两行式行高（`2×行高+7`），
   小图等比缩进（长边贴边）不撑破行高。元信息读**已存盘**内容（编辑中未保存
   改动不反映——与 Godot 口径一致）。
2. **行缓存键 = 段 clip guid，命中判据 = `AssetEntry.hash`（内容 FNV，Rescan 每次
   重算）**：保存/外部改动触发 Rescan → hash 变 → 下帧重读；未变零 IO。不做
   mtime 轮询、不做定时刷新——哈希键天然覆盖。换集（LoadSetFrom）与创建入口
   （ResetEditingState，热修②路径）整表清空。
3. **小图走 drawlist 直染（`AddImage`），不走 `ImGui::Image`**：Image 也是 item，
   盖在 Selectable 上会抢 HoveredId → 左列双击改名/右键菜单的 IsItemHovered 全断
   （胶片带 InvisibleButton 同款结论：交互件 + 覆盖内容 = 内容必须走 drawlist）。
   名字/元信息同理用 `AddText`。
4. **首帧解析复用页缩略图 UV 逻辑**：从 `DrawCellImage` 抽出 `CellImageParams`
   （tex/uv0/uv1/aspect），胶片带/预览（ImGui::Image）与左列行（drawlist）共用；
   `DrawCellImage` 行为零改动（回归面不变）。切片帧 = 页缩略图 UV 子矩形
   （缩略图与原图等比 → UV 不变）；整图帧 = 全幅。
5. 退化态文案：悬空行（段引用丢失）=「文件丢失」（名字后缀「（悬空）」保留）；
   坏档 =「坏档（右区看详情）」；空段 =「0 帧（右区加帧）」。小图失败画灰底
   圆角占位框，不报错不炸行。

## 改动

- `Editor/Panels/BuiltInPanels.h`：`SegRowInfo{hash,ok,frames,fps,first}` +
  `segRowCache_` + `LoadSegRow/DrawSegRowThumb` + 冒烟钩子 `SegRowFramesForTest`
  （新私有方法签名全 float——该头不引 imgui 类型，DrawCellImage 同约定）。
- `Editor/Panels/AnimationPanel.cpp`：
  - `CellImageParams` 文件内自由函数（匿名 ns 旁、EditorApp 全型可用）；
    `DrawCellImage` 改为薄壳。
  - `DrawLeftColumn` 行渲染重写：`Selectable("##segrow", sel, 0, ImVec2(0,rowH))`
    定交互（点选/双击改名/右键菜单代码原样迁移），内容三段 drawlist 直染。
    行高/小图边长按 `GetTextLineHeight()` 推导（DPI 自适应，v3.2 教训）。
  - `LoadSetFrom`/`ResetEditingState` 清 `segRowCache_`。
- `Editor/App/EditorApp.cpp`：smoke-anim f30 断言四段行元信息
  `walk=4 hit=2 edit=2 whole=1`（walk/hit/whole = 播种原值；edit = 播种 1 帧 +
  clip 编辑链 +1 的复合值——顺带锁住"元信息读存盘内容而非旧内存态"）；
  RESULT 增 `leftcol(meta=)` 位进 animOk。

## 验证

- smoke-anim `=> OK`：`leftcol(meta=YES)`，probe `walk=4 hit=2 edit=2 whole=1
  (expect 4/2/2/1)`，errors=0（热修③ 模态 queued=ON @f3 无 OFF 回落，未回归）。
- 回归 14/14（quick+full：ctest 3/3、smoke-ui、asset/anim/template/guid/script
  链、final、scene roundtrip）。
- 截图目检**未做**：本会话拉起的编辑器窗口被窗口服务挂到不可见 Space
  （单显示器、screencapture 多时机/带激活重试均只截到桌面）——非产品问题，
  像素观感归真人验收清单首条（见遗留）。

## 遗留

- 真人验收：开任一动画集看左列——首帧缩略图（等比、灰占位退化）、
  `N 帧 · X.XXs` 元信息、双击改名/右键菜单未被内容遮挡。
- 三图报告余四点：胶片带帧序号+拖拽换序 / 选帧对话框已选计数+清空 /
  列表模式行首小缩略图+模式记忆 / 大预览 scrub 条（优先级建议 3>2>4>5）。
- 元信息暂不含 LoopMode 图标（属性行已有下拉，避免行内信息过密）。
