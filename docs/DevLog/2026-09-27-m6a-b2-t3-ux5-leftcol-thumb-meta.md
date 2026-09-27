# 2026-09-27 M6a 批② T3-UX5：左列段行缩略图 + 帧数/时长元信息

三图走查报告（同日）优化点 1/5 落地。事件流水 + 两课一记。

## 事件

1. 用户拍板从优化点① 起步（ThumbCache/页缩略图现成，成本最低）。
2. 实现（批文件见 Plans/M6a/2026-09-27-b2-t3-ux5-leftcol-thumb-meta.md）：
   `SegRowInfo` 行缓存（guid 键 + `AssetEntry.hash` 命中判据）→ `DrawCellImage`
   抽 `CellImageParams` 共用 → 左列行改两行式（Selectable 定交互 + drawlist
   直染小图/名/元信息）→ smoke f30 断言 + RESULT `leftcol(meta=)` 位。
3. 首跑断言挂：期望 walk=5 实得 4——我误把 clip 编辑链记在 anim.anim 上
   （实际改的是 anim-edit.anim = edit 段）。**probe 的 4/2/2/1 恰是磁盘真值，
   缓存链路本身一次跑通**；修期望后全绿。教训（冒烟断言老坑再现）：期望值
   要从播种代码逐行对账，不能凭上批记忆。
4. 回归 14/14 全绿（DrawCellImage 重构面被 smoke-ui/anim 链覆盖）。
5. 截图目检失败：本会话 GUI 上下文的编辑器窗口不进屏（窗口服务挂不可见
   Space；单显示器、多时机/带激活重试均壁纸）。登记为环境限制，像素观感
   转真人验收首条。

## 课

- **覆盖在交互件上的内容必须走 drawlist**：ImGui::Image/AddText 里的 Image
  一样注册 item、抢 HoveredId——左列 Selectable 上盖 Image 会静默断双击改名
  与右键菜单（不炸、不报错，纯交互失灵）。胶片带 InvisibleButton 先例 + 本条
  再证：**"交互件 + 覆盖内容"是 ImGui 布局的固定反模式，内容一律 drawlist**。
- **行级懒缓存用内容哈希做键**：Rescan 每次 `HashFile` 重算 `AssetEntry.hash`
  （导入判定复用既有机制）——保存/外部改动自动失效、未变零 IO，免去 mtime
  轮询与失效风暴两个发明点。右区 LoadFrom 的 `loadedHash_` 同款先例，左列
  只是把它推广到"一集 N 段"。
- （环境记录）ZCode 后台会话拉起的 SDL 窗口不进用户 Space：截图验证要靠
  会话内 Read→analyze_image 时需先确认窗口真的在屏（本次壁纸 md5 对比判明），
  后续批次遇"截图只有壁纸"直接转程序化断言 + 真人验收，不再重试窗口管理器。

## 状态

代码/断言/文档齐；未提交（待用户指令）。真人验收清单 +1：开集看左列
缩略图与元信息、双击改名/右键菜单未被遮挡。
