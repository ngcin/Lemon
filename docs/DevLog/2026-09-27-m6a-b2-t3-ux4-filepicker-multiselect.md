# 2026-09-27 · M6a 批② T3-UX4：FilePicker 系统式多选 + 缩略图视图

## 事件

用户实测（动画集创建链路三轮热修后收到的下一步反馈）：文件选择器只有文件列表、
只能单张点选，操作不便；参考 docs/Animation/select multi images from file system.png
（Godot 缩略图网格），要求 icons/list 双模式 + Ctrl+A + Shift 范围连选（系统一致）。
当日完成微批（[批文件](../Plans/M6a/2026-09-27-b2-t3-ux4-filepicker-multiselect.md)）。

要点：

- **选择语义**：单击清余/Ctrl 加选/Shift 锚点范围/Ctrl+A 全选（mac Ctrl 和弦 =
  Cmd 映射下双修饰都认）；目录不可选、单击高亮双击进入（原单击即导航误点清选集）；
  确认路径集按显示序归一（点击序 ≠ 帧序直觉）。
- **ThumbCache**：任意路径图片 → stb 解码+resize2 ≤160px → RHI 纹理 → ImGui
  描述符集。零 bindless 槽占用（与 kMaxTextureSlots 正交）；LRU 128 + 每帧 ≤2 张
  解码预算；项目内精灵借 AssetGpuCache thumb 零解码。stb_image_resize2 同仓库
  同许可无新登记。
- **选中态首版踩坑**：描边误用 ImGuiCol_ButtonActive——本主题它=kBgActive（背景
  灰），11 项全选零视觉反馈。截图目检发现，换 theme::kAccent 修复。教训：
  **主题语义色 ≠ 任意场景可用色**——ButtonActive 在按钮语境外（自绘瓦片）名不副实，
  自绘元素直接取主题设计色（kAccent=kBgMid/kBgActive 家族外的"主选亮蓝"）。
- **回归**：smoke-anim 注入链扩到选择器——真实点击（TestHooks 瓦片矩形）+ 语义
  直调（Shift 不便按住注入）+ 真实 chord（Ctrl+A），计数 1/8/11 三断言；
  RESULT `pick(click/shift/all)`。断言数字踩过一次：Assets 实有 11 图（漏算
  yami 播种图）——f65 计数探针定位，非机制问题。

## 附：三图走查产出（用户指定流程：docs/Animation/ 三图 + 实现对照）

1. 左列动画段行纯文字（无缩略图/帧数/时长）——animation panel.png 左列区。
2. 胶片带帧格无序号、无拖拽换序（换序仅键盘 ←→）——animation panel.png 底部条。
3. 选帧对话框有全选+选择序号，但无已选计数/清空——select frames from sprite
   sheet.png 右参数区。
4. 文件选择器列表模式仍纯文字行（本次只做了网格缩略图）——对照 picker2.png。
5. 大预览无播放位置 scrub（暂停后不能拖回看）——animation panel.png 预览区。

均登记为后续微批候选（优先级建议 1 > 3 > 2 > 4 > 5，段行缩略图复用 ThumbCache
成本最低收益最直接）。
