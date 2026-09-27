# T3-UX4 · FilePicker 系统式多选 + 缩略图视图（微批）

**日期**：2026-09-27 · **状态**：done
**驱动**：用户实测反馈——文件选择器只有列表展示、只能单张点选，参考
docs/Animation/select multi images from file system.png（Godot 式缩略图网格），
要求 icons/list 双模式 + Ctrl+A 全选 + Shift 范围连选（系统一致）。

## 决策

1. **选择语义 = Finder/Explorer 三态**：单击清余选己（记锚）；Ctrl 加/移单项（记锚）；
   Shift 锚点范围连选（替换、锚不动=连续 Shift 换端）；Ctrl/Cmd+A 全选文件
   （mac 侧编辑器把 Ctrl 和弦映射 Cmd——ImGuiBackend 约定，双修饰都认）。
   目录不可选；范围/全选跳过目录条目。**确认路径集按显示序归一**
   （OrderedMultiSel——Ctrl 逐张点选的点击序 ≠ 直觉的帧序）。
2. **多选模式目录单击改高亮、双击进入**（原单击即导航，误点清选集——网格尤甚；
   列表同步改口径，两视图一致）。
3. **缩略图 = ThumbCache 独立通道**：stb 解码 → stb_image_resize2 缩到 ≤160px 边 →
   RHI 纹理 → ImGui_ImplVulkan_AddTexture 描述符集。**不占 bindless 槽/不进 Atlas**
   （ImGui 采样直连描述符集——与 kMaxTextureSlots 预算正交）。LRU 128 逐出
   （WaitIdle 同热重导口径）；逐帧预算 ≤2 张解码摊平大目录首屏。**项目内已导入
   精灵直接借 AssetGpuCache.Thumbnail**（零解码；borrowed 条目逐出不销毁借用纹理）。
   stb_image_resize2 = 同仓库同许可（THIRD_PARTY 无新登记项，StbImpl.cpp 加实现 TU）。
4. **视图**：OpenMulti 默认 Icons 网格（104px 瓦片：图区 4:3 fit + 名字省略号居中 +
   原图尺寸次行），列表/缩略图切换常驻路径行。ImGui 描述符池 256→512
   （网格叠加 LRU + 资产页 + 视口句柄）。
5. **选中描边 = theme::kAccent**（首版误用 ImGuiCol_ButtonActive——本主题它=kBgActive
   背景灰，11 项全选却零视觉反馈；截图目检发现）。

## 改动

- `Tooling/ThumbCache.h/.cpp`（新，core）：Init/Clear 随 gpuAssets（换项目同点）；
  Get(路径)→ImTextureID+原图尺寸；Tick() 预算入口。
- `Tooling/FilePicker.h/.cpp`：View{List,Icons}+切换钮；ApplyMultiClick 语义核心
  （真实点击处理器与测试共用入口）；OrderedMultiSel/ConfirmMulti；DrawIconEntries
  网格（InvisibleButton+drawlist 手绘+kAccent 选中态+TestHooks picker.tileN 注入位）；
  DrawListEntries 抽出；Ctrl+A。
- `Panels/AnimationPanel`：StartImageFilePick(app) 抽取（菜单与冒烟共用）+
  Picker 测试缝。
- `EditorApp`：ThumbCache 生命周期接线；smoke-anim 播种 9 张 pick*.png，
  f50 开选择器→f55/56 真实点击 tile0→f60 Shift 语义直调→f64 Ctrl/Cmd+A 真实
  chord 注入→计数 1/8/11 断言；RESULT `pick(click/shift/all)` 位。
- `ImGuiBackend.cpp` 池 512；`StbImpl.cpp` resize2 实现；CMake ThumbCache 入 core。

## 验证

smoke-anim `pick(click=YES shift=YES all=YES) => OK`（含热修③ create/flow 位全绿）；
quick 6/6；截图目检：5 列网格真实渲染缩略图（2×2 测试图/sheet 原比例）、11 项
2px 亮蓝圆角描边、footer 计数与提示文案。

## 遗留（三图分析产出，另立批次）

左列段行缩略图+元信息 / 胶片带帧序号+拖拽换序 / 选帧对话框已选计数+清空 /
列表模式行首小缩略图+模式记忆 / 大预览 scrub 条——见 DevLog 同日报告。
