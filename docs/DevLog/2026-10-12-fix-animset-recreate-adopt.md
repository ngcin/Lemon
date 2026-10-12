# 修复：动画编辑器「移除出集后同名重建」被"已存在"拦死 + 错误提示被裁掉

- 日期:2026-10-12（用户实测报告当天；M8 批⑥ 压测/soak 并行期插入修复）
- 性质:用户报障修复（走查支持期同款通道），非批次项

## 1. 实报（用户原话口径）

创建动画集后在动画编辑器里新建动画 `run`，然后删除再创建 `run`，提示
「已存在 Assets/…」——**后半句被覆盖看不到**。

## 2. 根因（两层）

1. **行为陷阱**：左列垃圾桶图标 = 「从集移除（保留动画文件）」（设计如此，
   tooltip/右键菜单文案齐全），移除后 `Assets/<集名>/run.anim` 仍在盘上且
   DB 活档。再建同名 → `QuickCreateSegment → TryCreateClip` 撞
   `anim.err.exists_fmt`（防静默覆盖的 DB+磁盘双查）红字拒绝。用户视角
   "我删了它为什么还已存在"。另两条删除路径语义正常：右键「删除动画文件
   （进墓碑）」后重建走墓碑复活放行；「重命名」同步改文件名不残留。
2. **显示缺陷（报障主诉）**：该错误渲染在左列 `##setlist` 子窗底部
   （宽 = min(230, 30% 面板宽)），`TextColored` 单行不折行 → 长路径被子窗
   右缘裁掉，用户连"已存在的是哪个文件"都看不到。

## 3. 修法

- **`AnimationPanel::QuickCreateSegment`（行为）**：新建段撞目标位
  （`SetClipDir(集)/<名>.anim`）时，若该位是**本集子文件夹里的合法活档
  Clip 且未被本集引用** → 不落新文件，直接把既有段重新入集（墓碑复活同款
  语义：不覆盖盘上内容，旧帧随引用回来），成功后绿字
  `anim.set.adopted_fmt` 告知"文件仍在盘上（从集移除不删文件），已重新入集"。
  集保存失败回滚行（新建路径 M24 同款口径）。其余撞名（坏档/异态）仍走
  TryCreateClip 红字拒，防静默覆盖不变。三个创建通道中仅集内 inline 新建
  改语义（极简创建框/新建集首段的撞名仍报错——那边撞的是真孤儿）。
- **消息折行（显示）**：四处红/绿字按 `PushTextWrapPos(0)` 折行——
  ①左列底部状态行（本报障位）；②预览条 saveMsg_（写盘失败带绝对路径）；
  ③新建集模态 setErr_；④极简创建框 wizErr_（AlwaysAutoResize 纵向生长）。
- **i18n**：`anim.set.adopted_fmt` zh/en 双语（anim.json 各 +1 key）。
- **Run() 计时采集顺修**：smoke-anim 新链中途 `StopPlay()` 后关机处非
  Playing，play-roundtrip 的 enter/exit 采样被跳过归零（门禁退化为
  0<500 恒真）→ 计时改为无条件读 `Last{Enter,Exit}PlayMs()`（各 --play
  路径取值时点不变，逐位同旧值）。

## 4. 机器验收

- **smoke-anim 新增 f119–122 接回链**（帧预算 120→132，zh/en 两步）：
  f119 退 Play（链前段全程 Play 态，QuickCreate 有 D6 沙盒只读守卫；真人
  操作集也只在非 Play）+ 盘上播种孤儿 `anim-set/run.anim`（拷 anim-edit.anim
  不带 .meta = 新 guid）→ f120 重开播种集 → f121 以 "run" 走 inline 新建
  Enter 语义（`QuickCreateSegmentForTest` 新冒烟钩）→ f122 断言
  `.override` 5 段尾段 run 且 guid=孤儿档 + 面板选中该段 + 左列第 5 行
  元信息已画（旧路径此处 = 红字"已存在"、段不入集、target 不切）。
  结论行新增 `adopt=%s` 位。
- 双语言 `smoke-anim: … adopt=YES => OK` / `editor-smoke PASS`
  （play-roundtrip enter=0.9/0.8ms exit=0.5/0.4ms byte-exact=YES）。
- **full 回归 26 步**：24 直绿 + 2 红均命中已登记环境噪声家族——en 态
  pick/click 注入抖动（adopt=YES 等语义位全过，挂的是真实点击注入位）与
  bench-survivor present=896ms 尖刺（fps=61<76.5；2026-10-12 上午同款
  present 947ms 先例）。**两步单独复跑双绿**：en 全 YES => OK；bench
  fps=79 ≥ 76.5 => PASS（frameMax=26.93ms，尖刺消失）。26/26 有绿记录。
- 构建零警告；引擎内核零改动（纯编辑器面 + 冒烟钩）。

## 5. 遗留

- 真人复验本流程（建集 → 建 run → 垃圾桶移除 → 再建 run 应绿字接回且
  报错可见折行）待用户下次开编辑器顺手过。
- mac 大小写不敏感文件系统下「Run vs run」撞名仍走磁盘孤儿红字
  （`exists_on_disk_fmt`，现在可读全路径）——既有行为，未动。
